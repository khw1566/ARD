import os
import sys
import time
import json
import socket
import threading
import base64
import asyncio
import requests
import numpy as np
import soundfile as sf
import edge_tts
from datetime import datetime
from flask import Flask, request, jsonify, render_template, Response, send_from_directory
from flask_cors import CORS

import config
from config import (
    CONFIG, device_stats, last_seen, motor_state, drive_settings,
    robot3axis_state, last_connection_state, device_korean_names,
    broadcast_event, broadcast_system_log, get_status, resource_path,
    SimpleSecurity, save_config_to_env, load_ai_persona, save_ai_persona,
    update_gemini_config, update_motor_state, get_local_ip
)

import network_manager
from network_manager import (
    music_player, radio_streamer, voice_queue_manager, audio_mirror_streamer,
    tts_to_pcm_and_send, process_udp_frame, send_tcp_command_async,
    play_emergency_stop_voice, send_end_of_speech_beep, get_whisper_model,
    ensure_sherpa_model, get_sherpa_tts, is_ffmpeg_available, resample_linear,
    AUDIO_UDP_PORT, SAMPLE_RATE
)

app = Flask(__name__, 
            template_folder=resource_path('templates'), 
            static_folder=resource_path('static'))

CORS(app)

RADIO_STATIONS = [
    {"name": "CBS 음악FM (93.9 MHz - 가요/팝 명가)", "url": "https://m-aac.cbs.co.kr/mweb_cbs939/_definst_/cbs939.stream/playlist.m3u8"},
    {"name": "KBS Cool FM (89.1 MHz - 대중음악/예능)", "url": "http://kbs-kong.gscdn.com/coolfm/_definst_/coolfm.stream/playlist.m3u8"},
    {"name": "SBS Power FM (107.7 MHz - 종합예능)", "url": "http://radiolive.sbs.co.kr/powerpc/powerfm.stream/playlist.m3u8"},
    {"name": "MBC FM4U (91.9 MHz - 감성팝/예능)", "url": "http://mini-fm.imbc.com/fm4u/_definst_/fm4u.stream/playlist.m3u8"},
    {"name": "YTN 라디오 (94.5 MHz - 24시간 실시간 뉴스)", "url": "https://ytnradio.ytn.co.kr/ytnradio/_definst_/ytnradio.stream/playlist.m3u8"},
    {"name": "EBS 교육방송 (104.5 MHz - 어학/지성)", "url": "http://ebs-aac.gscdn.com/ebs_fm/_definst_/ebs_fm.stream/playlist.m3u8"},
    {"name": "Lofi Girl Live (24시간 공부용 비트)", "url": "http://stream.zeno.fm/0r0xa792u0eut"},
    {"name": "SomaFM Groove Salad (세련된 전자 앰비언트)", "url": "http://ice1.somafm.com/groovesalad-128-mp3"},
    {"name": "Cafe del Mar (정통 스페인 칠아웃)", "url": "http://stream.cafedelmar.com:8000/stream"}
]


@app.route('/api/startup_status')
@app.route('/api/startup/status')
def get_startup_status():
    idx = max(0, min(config._current_startup_idx, len(config.STARTUP_STEPS) - 1))
    return jsonify(config.STARTUP_STEPS[idx])

@app.route('/api/log', methods=['POST'])
def receive_log():
    try:
        data = request.get_json(silent=True)
        if not data:
            return jsonify({"status": "error", "message": "No JSON data"}), 400
            
        log_src = data.get("source", "unknown")
        log_msg = data.get("message", "")
        
        s_lower = log_src.lower()
        if s_lower == "transmitter": s_lower = "imagesendermcu"
        if s_lower == "receiver": s_lower = "hmi_mcu"
        
        if s_lower in last_seen:
            last_seen[s_lower] = time.time()
            display_name = "ImageSenderMCU" if s_lower == "imagesendermcu" else "HMI_MCU" if s_lower == "hmi_mcu" else "DriveMCU"
            remote_ip = request.remote_addr
            device_stats[display_name]["ip"] = remote_ip
            
            if s_lower == "imagesendermcu":
                CONFIG["imagesendermcu_ip"] = remote_ip
            elif s_lower == "drivemcu":
                CONFIG["drivemcu_ip"] = remote_ip
            elif s_lower == "hmi_mcu":
                CONFIG["hmi_mcu_ip"] = remote_ip
        
        log_payload = {
            "type": "log",
            "source": log_src,
            "message": log_msg,
            "timestamp": datetime.now().strftime("%H:%M:%S")
        }
        
        for q in config.clients:
            q.append(log_payload)
            
        return jsonify({"status": "success"})
    except Exception as e:
        print(f"Log API Error: {str(e)}")
        return jsonify({"status": "error", "message": str(e)}), 500

@app.route('/api/heartbeat', methods=['POST'])
def receive_heartbeat():
    try:
        data = request.get_json(silent=True)
        if not data:
            print("Heartbeat API Error: No JSON data received")
            return jsonify({"status": "error"}), 400
            
        hb_source = data.get("source", "")
        print(f"[{datetime.now().strftime('%H:%M:%S')}] Heartbeat received from {hb_source} (IP: {request.remote_addr})")
        
        s_norm = hb_source.lower().replace("_", "").replace("-", "")
        
        target_key = None
        if "imagesender" in s_norm or "trans" in s_norm: 
            target_key = "imagesendermcu"
        elif "hmi" in s_norm or "receiv" in s_norm:
            target_key = "hmi_mcu"
        elif "drive" in s_norm or "motor" in s_norm:
            target_key = "drivemcu"
        elif "sound" in s_norm or "voice" in s_norm:
            target_key = "soundmcu"
        elif "robot3" in s_norm or "3axis" in s_norm:
            target_key = "robot3axismcu"
        elif "control" in s_norm:
            target_key = "controlmcu"
        elif "ard_server" in s_norm:
            return jsonify({"status": "ignored"})
        
        if target_key:
            prev_state = last_connection_state.get(target_key, "Offline")
            
            if target_key in last_seen:
                last_seen[target_key] = time.time()
            else:
                last_seen[target_key] = time.time()
            
            if target_key == "controlmcu":
                last_seen["soundmcu"] = time.time()
                
            if prev_state == "Offline":
                last_connection_state[target_key] = "Online"
                if target_key == "controlmcu":
                    ssid = data.get("ssid", "Unknown")
                    msg = f"접속완료! 에스 에스 아이 디는 {ssid}입니다"
                    threading.Thread(target=tts_to_pcm_and_send, args=(msg,), daemon=True).start()
                else:
                    dev_ko = device_korean_names.get(target_key)
                    if dev_ko:
                        msg = f"{dev_ko}이 연결되었습니다."
                        threading.Thread(target=tts_to_pcm_and_send, args=(msg,), daemon=True).start()
            
            display_name = (
                "ImageSenderMCU" if target_key == "imagesendermcu" else 
                "HMI_MCU" if target_key == "hmi_mcu" else 
                "SoundMCU" if target_key == "soundmcu" else 
                "Robot3AxisMCU" if target_key == "robot3axismcu" else
                "ControlMCU" if target_key == "controlmcu" else
                "DriveMCU"
            )
            remote_ip = data.get("ip", request.remote_addr)
            
            device_stats[display_name]["rssi"] = data.get("rssi", 0)
            device_stats[display_name]["ip"] = remote_ip
            device_stats[display_name]["temp"] = round(data.get("temp", 0.0), 1)
            
            if display_name == "ControlMCU":
                device_stats["ControlMCU"]["mode"] = data.get("mode", "A")
                device_stats["ControlMCU"]["sub_mode"] = data.get("sub_mode", 1)
                device_stats["ControlMCU"]["status"] = data.get("status", "Idle")
                CONFIG["soundmcu"]["ip"] = remote_ip
                device_stats["SoundMCU"]["ip"] = remote_ip
            elif display_name == "ImageSenderMCU":
                CONFIG["imagesendermcu_ip"] = remote_ip
            elif display_name == "DriveMCU":
                CONFIG["drivemcu_ip"] = remote_ip
                device_stats["DriveMCU"]["gps_lat"] = data.get("gps_lat", 0.0)
                device_stats["DriveMCU"]["gps_lng"] = data.get("gps_lng", 0.0)
                device_stats["DriveMCU"]["tof_dist"] = data.get("tof_dist", 0)
                if "env_temp" in data:
                    device_stats["DriveMCU"]["env_temp"] = data.get("env_temp")
                    device_stats["DriveMCU"]["env_humid"] = data.get("env_humid")
                    device_stats["DriveMCU"]["env_press"] = data.get("env_press")
                
                motor_state["tof_dist"] = device_stats["DriveMCU"]["tof_dist"]
                
                payload = {"type": "status", "status": get_status()}
                for q in config.clients:
                    q.append(payload)
  
            elif display_name == "HMI_MCU":
                CONFIG["hmi_mcu_ip"] = remote_ip
            elif display_name == "SoundMCU":
                CONFIG["soundmcu"]["ip"] = remote_ip
                device_stats["SoundMCU"]["status"] = data.get("status", "Idle")
                
            if display_name == "ControlMCU":
                payload = {"type": "status", "status": get_status()}
                for q in config.clients:
                    q.append(payload)
  
        return jsonify({"status": "ok"})
    except Exception as e:
        print(f"Heartbeat API Error: {str(e)}")
        return jsonify({"status": "error"}), 500

@app.route('/api/hmi/last_image', methods=['GET'])
def get_hmi_last_image():
    if not network_manager.last_hmi_image:
        return "No image", 404
    return Response(network_manager.last_hmi_image, mimetype='image/jpeg')

@app.route('/api/imagesendermcu/command', methods=['POST'])
@app.route('/api/transmitter/command', methods=['POST'])
def send_imagesendermcu_command():
    data = request.json
    cmd = data.get("command")
    if cmd:
        cmd = cmd.lower()
        print(f"[{datetime.now().strftime('%H:%M:%S')}] ImageSender Command: {cmd}")
        
        if cmd == "c_sens":
            sensors = ["OV2640", "OV3660", "OV5640"]
            cur = CONFIG["imagesendermcu"]["camera_model"]
            idx = (sensors.index(cur) + 1) % len(sensors) if cur in sensors else 0
            CONFIG["imagesendermcu"]["camera_model"] = sensors[idx]
        elif cmd == "c_res":
            res_list = ["VGA", "SVGA", "XGA", "SXGA", "UXGA"]
            cur = CONFIG["imagesendermcu"]["resolution"]
            idx = (res_list.index(cur) + 1) % len(res_list) if cur in res_list else 0
            CONFIG["imagesendermcu"]["resolution"] = res_list[idx]
        elif cmd == "c_qlt":
            qlt_list = [5, 10, 20, 40]
            cur = CONFIG["imagesendermcu"]["jpeg_quality"]
            idx = (qlt_list.index(cur) + 1) % len(qlt_list) if cur in qlt_list else 0
            CONFIG["imagesendermcu"]["jpeg_quality"] = qlt_list[idx]
        elif cmd == "c_mdl":
            models = ["gemini-2.0-flash", "gemini-1.5-flash"]
            cur = CONFIG.get("model_name", "gemini-2.0-flash")
            idx = (models.index(cur) + 1) % len(models) if cur in models else 0
            CONFIG["model_name"] = models[idx]
        else:
            CONFIG["imagesendermcu"]["command"] = cmd
            if cmd == "capture":
                threading.Thread(target=tts_to_pcm_and_send, args=("캡쳐하였습니다!",), daemon=True).start()
            
        broadcast_system_log(f"ImageSender command/setting updated: {cmd}", "System")
        save_config_to_env()
        broadcast_event({"type": "status", "status": get_status()})
        return jsonify({"status": "success", "command": cmd, "config": CONFIG["imagesendermcu"]})
    return jsonify({"status": "error"}), 400

@app.route('/api/imagesendermcu/config', methods=['GET'])
def get_imagesender_config():
    cmd = CONFIG["imagesendermcu"]["command"]
    CONFIG["imagesendermcu"]["command"] = None
    
    print(f"[{datetime.now().strftime('%H:%M:%S')}] ImageSender polling config: Model={CONFIG['imagesendermcu']['camera_model']}, Res={CONFIG['imagesendermcu']['resolution']}, Cmd={cmd}")
    
    return jsonify({
        "camera_model": CONFIG["imagesendermcu"]["camera_model"],
        "resolution": CONFIG["imagesendermcu"]["resolution"],
        "jpeg_quality": CONFIG["imagesendermcu"]["jpeg_quality"],
        "transmission_mode": CONFIG["imagesendermcu"]["transmission_mode"],
        "command": cmd
    })

@app.route('/api/status', methods=['GET'])
def get_status_api():
    return jsonify(get_status())

@app.route('/api/status/stream')
def events():
    def event_stream():
        q = []
        config.clients.append(q)
        try:
            last_status_check = 0
            while True:
                now = time.time()
                if now - last_status_check > 5:
                    yield f"data: {json.dumps({'type': 'status', 'status': get_status()})}\n\n"
                    last_status_check = now

                while q:
                    data = q.pop(0)
                    if 'type' not in data:
                        data['type'] = 'update'
                    yield f"data: {json.dumps(data)}\n\n"

                time.sleep(0.05)
        finally:
            config.clients.remove(q)
    return Response(event_stream(), mimetype="text/event-stream")

@app.route('/')
def index():
    return render_template('index.html', config=CONFIG, server_ip=get_local_ip())

@app.route('/api/config', methods=['GET', 'POST'])
def handle_config():
    global CONFIG
    if request.method == 'POST':
        data = request.json
        CONFIG["api_key"] = data.get("api_key", CONFIG["api_key"])
        CONFIG["model_name"] = data.get("model_name", CONFIG["model_name"])
        
        new_ip = data.get("imagesendermcu_ip") or data.get("transmitter_ip")
        if new_ip:
            CONFIG["imagesendermcu_ip"] = new_ip
            
        CONFIG["hmi_mcu_ip"] = data.get("hmi_mcu_ip", CONFIG["hmi_mcu_ip"])
        CONFIG["drivemcu_ip"] = data.get("drivemcu_ip", CONFIG["drivemcu_ip"])
        
        CONFIG["gear_ratio_x"] = float(data.get("gear_ratio_x", CONFIG["gear_ratio_x"]))
        CONFIG["gear_ratio_y"] = float(data.get("gear_ratio_y", CONFIG["gear_ratio_y"]))
        CONFIG["gear_ratio_z"] = float(data.get("gear_ratio_z", CONFIG.get("gear_ratio_z", 1.0)))
        CONFIG["motor_delay"] = int(data.get("motor_delay", CONFIG["motor_delay"]))
        CONFIG["drivemcu_gps"] = data.get("drivemcu_gps", CONFIG.get("drivemcu_gps", False))
        
        if "camera_model" in data:
            CONFIG["imagesendermcu"]["camera_model"] = data["camera_model"]
        if "resolution" in data:
            CONFIG["imagesendermcu"]["resolution"] = data["resolution"]
        if "jpeg_quality" in data:
            CONFIG["imagesendermcu"]["jpeg_quality"] = int(data["jpeg_quality"])
        if "transmission_mode" in data:
            CONFIG["imagesendermcu"]["transmission_mode"] = data["transmission_mode"]
        
        CONFIG["imagesendermcu"]["command"] = "UPDATE"
        CONFIG["hmi_image_quality"] = int(data.get("hmi_image_quality", CONFIG["hmi_image_quality"]))
        CONFIG["soundmcu"]["gemini_api_key"] = data.get("soundmcu_api_key", CONFIG["soundmcu"]["gemini_api_key"])
        
        update_gemini_config()
        save_config_to_env()
        broadcast_system_log(f"Settings Applied: Cam({CONFIG['imagesendermcu']['camera_model']})", "System")
        return jsonify({"status": "success", "config": CONFIG})
    return jsonify(CONFIG)

@app.route('/api/robot3axis/command', methods=['POST'])
def send_robot3axis_command():
    data = request.json
    cmd = data.get("command")
    is_absolute = data.get("absolute", False)
    
    if all(k in data for k in ("x", "y", "z")):
        if is_absolute:
            threading.Thread(target=tts_to_pcm_and_send, args=("3축로봇 피 티 피 제어 작동합니다.",), daemon=True).start()
            tx_deg = float(data["x"])
            ty_mm = float(data["y"])
            tz_mm = float(data["z"])
            
            target_x_steps = int(tx_deg * 2.0)
            target_y_steps = int(ty_mm * 1000.0 / -43.0)
            target_z_steps = int(tz_mm * 1400.0 / -320.0)
            
            sx = target_x_steps - robot3axis_state["x_steps"]
            sy = target_y_steps - robot3axis_state["y_steps"]
            sz = target_z_steps - robot3axis_state["z_steps"]
        else:
            threading.Thread(target=tts_to_pcm_and_send, args=("3축로봇 단일스탭 제어 작동합니다.",), daemon=True).start()
            sx = int(data["x"])
            sy = int(data["y"])
            sz = int(data["z"])
            
        cmd = f"a{sx},{sy},{sz}"
        
        robot3axis_state["x_steps"] += sx
        robot3axis_state["x_deg"] += (sx * 90.0) / 180.0
        robot3axis_state["y_steps"] += sy
        robot3axis_state["y_mm"] += (sy * -43.0) / 1000.0
        robot3axis_state["z_steps"] += sz
        robot3axis_state["z_mm"] += (sz * -320.0) / 1400.0
    
    if cmd:
        if not cmd.startswith("a"):
            cmd_lower = cmd.lower()
            if cmd_lower == "stop":
                threading.Thread(target=play_emergency_stop_voice, daemon=True).start()
            elif cmd_lower == "home":
                sx = -robot3axis_state["x_steps"]
                sy = -robot3axis_state["y_steps"]
                sz = -robot3axis_state["z_steps"]
                cmd = f"a{sx},{sy},{sz}"
                
                broadcast_system_log(f"[Robot3Axis Home] Returning to Origin: X:{sx}, Y:{sy}, Z:{sz}", "System")
                
                robot3axis_state["x_steps"] = 0
                robot3axis_state["y_steps"] = 0
                robot3axis_state["z_steps"] = 0
                robot3axis_state["x_deg"] = 0.0
                robot3axis_state["y_mm"] = 0.0
                robot3axis_state["z_mm"] = 0.0
            elif cmd_lower == "grab open":
                robot3axis_state["grab_state"] = "open"
                threading.Thread(target=tts_to_pcm_and_send, args=("그리퍼 오픈!",), daemon=True).start()
            elif cmd_lower == "grab close":
                robot3axis_state["grab_state"] = "closed"
                threading.Thread(target=tts_to_pcm_and_send, args=("그리퍼 클로즈!",), daemon=True).start()
            elif cmd_lower.startswith("rxs"):
                delta = int(cmd[3:])
                robot3axis_state["x_speed"] = max(100, robot3axis_state["x_speed"] + delta)
                cmd = f"sx{robot3axis_state['x_speed']}"
            elif cmd_lower.startswith("rys"):
                delta = int(cmd[3:])
                robot3axis_state["y_speed"] = max(100, robot3axis_state["y_speed"] + delta)
                cmd = f"sy{robot3axis_state['y_speed']}"
            elif cmd_lower.startswith("rzs"):
                delta = int(cmd[3:])
                robot3axis_state["z_speed"] = max(100, robot3axis_state["z_speed"] + delta)
                cmd = f"sz{robot3axis_state['z_speed']}"
            elif cmd_lower.startswith("sx"):
                robot3axis_state["x_speed"] = int(cmd[2:])
            elif cmd_lower.startswith("sy"):
                robot3axis_state["y_speed"] = int(cmd[2:])
            elif cmd_lower.startswith("sz"):
                robot3axis_state["z_speed"] = int(cmd[2:])
            elif cmd_lower.startswith("rx"):
                threading.Thread(target=tts_to_pcm_and_send, args=("3축로봇 단일스탭 제어 작동합니다.",), daemon=True).start()
                steps = int(cmd[2:])
                robot3axis_state["x_steps"] += steps
                robot3axis_state["x_deg"] += (steps * 90.0) / 180.0
            elif cmd_lower.startswith("ry"):
                threading.Thread(target=tts_to_pcm_and_send, args=("3축로봇 단일스탭 제어 작동합니다.",), daemon=True).start()
                steps = int(cmd[2:])
                robot3axis_state["y_steps"] += steps
                robot3axis_state["y_mm"] += (steps * -43.0) / 1000.0
            elif cmd_lower.startswith("rz"):
                threading.Thread(target=tts_to_pcm_and_send, args=("3축로봇 단일스탭 제어 작동합니다.",), daemon=True).start()
                steps = int(cmd[2:])
                robot3axis_state["z_steps"] += steps
                robot3axis_state["z_mm"] += (steps * -320.0) / 1400.0
            
        target_ip = device_stats["Robot3AxisMCU"]["ip"]
        if "Robot3AxisMCU" in device_stats and target_ip != "Unknown":
            send_tcp_command_async(target_ip, 5002, cmd, "Robot3Axis")
        else:
            broadcast_system_log(f"Robot command queued (No IP): {cmd}", "System")
                
        broadcast_event({"type": "robot3axis_update", "state": robot3axis_state})
                
        return jsonify({"status": "success", "command": cmd, "state": robot3axis_state})
    return jsonify({"status": "error"}), 400

@app.route('/api/control/update', methods=['POST'])
def update_control_state_route():
    try:
        data = request.get_json(silent=True)
        if not data: return jsonify({"status": "error"}), 400
        
        last_seen["controlmcu"] = time.time()
        last_seen["soundmcu"] = time.time()
        remote_ip = request.remote_addr
        device_stats["ControlMCU"]["ip"] = remote_ip
        device_stats["ControlMCU"]["rssi"] = data.get("rssi", 0)
        device_stats["ControlMCU"]["temp"] = data.get("temp", 0.0)
        
        CONFIG["soundmcu"]["ip"] = remote_ip
        device_stats["SoundMCU"]["ip"] = remote_ip
        
        prev_mode = device_stats["ControlMCU"].get("mode", "A")
        prev_sub_mode = device_stats["ControlMCU"].get("sub_mode", 1)
        prev_joystick = device_stats["ControlMCU"].get("joystick", {})

        mode = data.get("mode", "A")
        sub_mode = data.get("sub_mode", 1)
        joystick = data.get("joystick", {})
        
        joy_changed = (joystick.get("up") != prev_joystick.get("up") or
                       joystick.get("down") != prev_joystick.get("down") or
                       joystick.get("left") != prev_joystick.get("left") or
                       joystick.get("right") != prev_joystick.get("right"))
        state_changed = (mode != prev_mode or sub_mode != prev_sub_mode or joy_changed)
        
        if state_changed:
            joy_inputs = []
            if joystick.get("up"): joy_inputs.append("UP")
            if joystick.get("down"): joy_inputs.append("DOWN")
            if joystick.get("left"): joy_inputs.append("LEFT")
            if joystick.get("right"): joy_inputs.append("RIGHT")
            joy_str = ", ".join(joy_inputs) if joy_inputs else "RELEASED"
            
            log_msg = f"Input Event -> Toggle Mode: {mode} | Push Switch (Sub): {sub_mode} | Joystick: {joy_str}"
            broadcast_system_log(log_msg, "ControlMCU")
            
        device_stats["ControlMCU"]["mode"] = mode
        device_stats["ControlMCU"]["sub_mode"] = sub_mode
        device_stats["ControlMCU"]["joystick"] = joystick
        
        cmd = None
        target_mcu = None
        steps = 100
        
        if mode == "A":
            target_mcu = "Robot3AxisMCU"
            if sub_mode == 1:
                if joystick.get("up"): cmd = f"rz{steps}"
                elif joystick.get("down"): cmd = f"rz-{steps}"
                elif joystick.get("left"): cmd = f"rx-{steps}"
                elif joystick.get("right"): cmd = f"rx{steps}"
            else:
                if joystick.get("up"): cmd = f"ry{steps}"
                elif joystick.get("down"): cmd = f"ry-{steps}"
                elif joystick.get("left"): cmd = "grab open"
                elif joystick.get("right"): cmd = "grab close"
                
        elif mode == "B":
            target_mcu = "DriveMCU"
            if sub_mode == 1:
                if joystick.get("up"): cmd = f"df {steps}"
                elif joystick.get("down"): cmd = f"db {steps}"
                elif joystick.get("left"): cmd = f"dl {steps}"
                elif joystick.get("right"): cmd = f"dr {steps}"
            else:
                if joystick.get("up"): cmd = f"mxr{steps},myr-{steps}"
                elif joystick.get("down"): cmd = f"mxr-{steps},myr{steps}"
        else:
            target_mcu = None
            cmd = None
 
        if cmd and target_mcu:
            target_ip = device_stats[target_mcu]["ip"]
            if target_ip != "Unknown":
                try:
                    tcp_sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
                    tcp_sock.settimeout(1.0)
                    tcp_sock.connect((target_ip, 5002))
                    if ',' in cmd and target_mcu == "DriveMCU":
                        cmds = cmd.split(',')
                        for c in cmds:
                            tcp_sock.sendall((c + "\n").encode())
                            time.sleep(0.01)
                    else:
                        tcp_sock.sendall((cmd + "\n").encode())
                    tcp_sock.close()
                    broadcast_system_log(f"ControlMCU -> {target_mcu}: {cmd}", "System")
                    
                    if target_mcu == "Robot3AxisMCU":
                        try:
                            s_val = 0
                            if cmd.startswith("rx") or cmd.startswith("ry") or cmd.startswith("rz"):
                                s_val = int(cmd[2:])
                            
                            if cmd.startswith("rx"): 
                                robot3axis_state["x_steps"] += s_val
                                robot3axis_state["x_deg"] += (s_val * 0.5)
                            elif cmd.startswith("ry"): 
                                robot3axis_state["y_steps"] += s_val
                                robot3axis_state["y_mm"] += (s_val * -0.043)
                            elif cmd.startswith("rz"): 
                                robot3axis_state["z_steps"] += s_val
                                robot3axis_state["z_mm"] += (s_val * -0.228)
                            broadcast_event({"type": "robot3axis_update", "state": robot3axis_state})
                        except: pass
                    elif target_mcu == "DriveMCU":
                        update_motor_state(cmd)
                        
                except Exception as e:
                    broadcast_system_log(f"Control Link Failed: {e}", "Error")

        payload = {"type": "status", "status": get_status()}
        for q in config.clients:
            q.append(payload)
            
        return jsonify({"status": "success"})
    except Exception as e:
        print(f"Control API Error: {e}")
        return jsonify({"status": "error", "message": str(e)}), 500

@app.route('/api/drivemcu/command', methods=['POST'])
def send_drive_command():
    global drive_settings
    data = request.json
    cmd = data.get("command")
    if cmd:
        if cmd == "CMD_HOME":
            steps_x = -motor_state["mx_steps"]
            steps_y = -motor_state["my_steps"]
            cmd = f"b{steps_x},{steps_y}"
            broadcast_system_log(f"[Home] Restoring X:{steps_x}, Y:{steps_y}", "System")

        settings_changed = False
        try:
            if cmd.startswith('mzh'):
                val = int(cmd[3:])
                drive_settings['hb_interval_ms'] = val
                settings_changed = True
                broadcast_system_log(f"[HMI] Heartbeat interval set: {val}ms", "System")
            elif cmd.startswith('mzc'):
                val = int(cmd[3:])
                drive_settings['i2c_clock_hz'] = val
                settings_changed = True
                broadcast_system_log(f"[HMI] I2C clock set: {val}Hz", "System")
            elif cmd.startswith('mzm'):
                val = cmd[3:]
                drive_settings['pid_mode'] = (val == '1')
                settings_changed = True
                broadcast_system_log(f"[HMI] PID mode: {'ON' if drive_settings['pid_mode'] else 'OFF'}", "System")
            elif cmd.startswith('mzt'):
                val = int(cmd[3:])
                drive_settings['pid_target_tof'] = val
                settings_changed = True
                broadcast_system_log(f"[HMI] PID target ToF: {val}mm", "System")
            elif cmd.startswith('mzs'):
                val_str = cmd[3:]
                if val_str.startswith('+') or val_str.startswith('-'):
                    delta = int(val_str)
                    drive_settings['z_step'] = max(10, drive_settings['z_step'] + delta)
                    settings_changed = True
                    broadcast_system_log(f"[HMI] Z step: {drive_settings['z_step']}", "System")
        except Exception as e:
            print(f"[Drive CMD Parse] {e}")

        is_movement = False
        if cmd == "x":
            threading.Thread(target=play_emergency_stop_voice, daemon=True).start()
        elif cmd == "CMD_HOME" or cmd.startswith('b'):
            is_movement = True
        elif cmd.startswith('mz') and not cmd.startswith(('mzh', 'mzc', 'mzm', 'mzt')):
            is_movement = True
            
        if is_movement:
            threading.Thread(target=tts_to_pcm_and_send, args=("분류구조 작동합니다.",), daemon=True).start()

        CONFIG["drivemcu_command"] = cmd
        update_motor_state(cmd)
        broadcast_event({"type": "drive_update", "state": motor_state, "settings": drive_settings})
        
        if CONFIG["drivemcu_ip"]:
            send_tcp_command_async(CONFIG["drivemcu_ip"], 5002, cmd, "DriveMCU")
        else:
            broadcast_system_log(f"Drive command queued (No IP): {cmd}", "System")
            
        return jsonify({"status": "success", "command": cmd, "settings": drive_settings})
    return jsonify({"status": "error"}), 400

@app.route('/api/hmi/info', methods=['GET'])
def get_hmi_info():
    chat_logs = []
    for entry in network_manager.conversation_history[-6:]:
        chat_logs.append({
            "r": "U" if entry["role"] == "User" else "A",
            "t": entry["text"][:100]
        })
    
    last_analysis = {
        "obj": device_stats.get("last_analysis", {}).get("object", "-"),
        "cat": device_stats.get("last_analysis", {}).get("category", "-")
    }

    return jsonify({
        "chat": chat_logs,
        "motor": motor_state,
        "analysis": last_analysis,
        "status": {
            "drive": config.get_device_status("DriveMCU"),
            "sound": config.get_device_status("SoundMCU")
        }
    })

@app.route('/api/time', methods=['GET'])
def get_gps_time():
    gps_epoch = datetime(1980, 1, 6)
    now_utc = datetime.utcnow()
    diff = now_utc - gps_epoch
    weeks = diff.days // 7
    tow_ms = (diff.days % 7 * 86400 + diff.seconds) * 1000 + (diff.microseconds // 1000)
    return jsonify({
        "week": weeks,
        "tow_ms": tow_ms,
        "utc": now_utc.strftime("%Y-%m-%d %H:%M:%S")
    })

@app.route('/api/soundmcu/chat', methods=['POST'])
def manual_chat():
    data = request.json
    text = data.get("text", "")
    client_turn_id = data.get("turn_id", "")
    if not text:
        return jsonify({"status": "error", "message": "No text"}), 400
    
    def run_chat_pipeline():
        try:
            t_start = time.perf_counter()
            turn_id = client_turn_id if client_turn_id else f"t_{int(t_start * 1000)}"
            
            network_manager.update_soundmcu_status("Processing")
            ai_response = network_manager.get_gemini_conversational_response(text)
            t_ai_end = time.perf_counter()
            ai_ms = int((t_ai_end - t_start) * 1000)
            
            broadcast_system_log(f"AI (Manual): {ai_response[:30]}...", "Gemini")
            
            audio_url = tts_to_pcm_and_send(ai_response)
            t_tts_end = time.perf_counter()
            tts_ms = int((t_tts_end - t_ai_end) * 1000)
            total_ms = int((t_tts_end - t_start) * 1000)
            
            res_payload = {
                "type": "update",
                "turn_id": turn_id,
                "analysis": {"type": "voice", "role": "AI", "text": ai_response, "audio_url": audio_url},
                "timings": {"stt": 0, "ai": ai_ms}
            }
            for q in config.clients: q.append(res_payload)
            
            if CONFIG["soundmcu"]["ip"]:
                network_manager.update_soundmcu_status("Speaking")
                
            final_payload = {
                "type": "update",
                "turn_id": turn_id,
                "timings": {
                    "stt": 0,
                    "ai": ai_ms,
                    "tts": tts_ms,
                    "total": total_ms
                },
                "audio_url": audio_url
            }
            for q in config.clients: q.append(final_payload)

            network_manager.update_soundmcu_status("Idle")
        except Exception as e:
            print(f"Chat Pipeline Error: {e}")
            network_manager.update_soundmcu_status("Idle")

    threading.Thread(target=run_chat_pipeline, daemon=True).start()
    return jsonify({"status": "queued"})

@app.route('/api/soundmcu/test_emotion_tts', methods=['POST'])
def test_emotion_tts():
    try:
        data = request.json or {}
        text = data.get("text", "안녕하세요! 오늘 기분이 정말 어떠신가요?")
        style = data.get("style", "cheerful")
        style_degree = float(data.get("style_degree", 1.5))
        
        api_key = CONFIG["soundmcu"].get("azure_api_key", "")
        region = CONFIG["soundmcu"].get("azure_region", "koreacentral")
        voice_id = CONFIG["soundmcu"]["tts_voice"]
        pitch_val = CONFIG["soundmcu"]["tts_pitch"]
        rate_val = CONFIG["soundmcu"]["tts_rate"]
        
        if not api_key:
            return jsonify({"status": "error", "message": "Azure Speech API Key가 설정되지 않았습니다."}), 400
            
        base_dir = os.path.dirname(os.path.abspath(__file__))
        audio_dir = os.path.join(base_dir, "static", "audio")
        os.makedirs(audio_dir, exist_ok=True)
        audio_file = os.path.join(audio_dir, "test_emotion.wav")
        
        pitch_str = f"{'+' if pitch_val >= 0 else ''}{pitch_val}%"
        rate_str = f"{'+' if rate_val >= 0 else ''}{rate_val}%"
        
        ssml = (
            f"<speak version='1.0' xml:lang='ko-KR' xmlns='http://www.w3.org/2001/10/synthesis' xmlns:mstts='https://www.w3.org/2001/mstts'>"
            f"<voice name='{voice_id}'>"
            f"<mstts:express-as style='{style}' styledegree='{style_degree}'>"
            f"<prosody pitch='{pitch_str}' rate='{rate_str}'>{text}</prosody>"
            f"</mstts:express-as>"
            f"</voice></speak>"
        )
        
        url = f"https://{region}.tts.speech.microsoft.com/cognitiveservices/v1"
        headers = {
            "Ocp-Apim-Subscription-Key": api_key,
            "Content-Type": "application/ssml+xml",
            "X-Microsoft-OutputFormat": "riff-16khz-16bit-mono-pcm",
            "User-Agent": "ARD-PC-Center"
        }
        
        res = requests.post(url, headers=headers, data=ssml.encode('utf-8'), timeout=10)
        if res.status_code == 200:
            with open(audio_file, "wb") as f:
                f.write(res.content)
            audio_url = f"/static/audio/test_emotion.wav?t={int(time.time())}"
            return jsonify({"status": "success", "audio_url": audio_url})
        else:
            return jsonify({"status": "error", "message": f"Azure HTTP Error: {res.status_code} - {res.text}"}), 500
    except Exception as e:
        return jsonify({"status": "error", "message": str(e)}), 500

@app.route('/api/pc_mic/voice', methods=['POST'])
def receive_pc_mic_voice():
    try:
        audio_data = request.data
        if not audio_data:
            return jsonify({"status": "error", "message": "No audio data"}), 400
            
        chunk = np.frombuffer(audio_data, dtype=np.int16)
        
        if len(chunk) < 1600:
            return jsonify({"status": "error", "message": "Audio too short"}), 400

        broadcast_system_log(f"PC Mic Input: {len(chunk)} samples received", "Sound")
        
        network_manager.update_soundmcu_status("Processing")
        for q in config.clients:
            q.append({"type": "user_speaking", "speaking": False})
            
        threading.Thread(target=network_manager.process_voice_command, args=(chunk,)).start()
        
        return jsonify({"status": "success"})
    except Exception as e:
        broadcast_system_log(f"PC Mic Voice Processing Failed: {e}", "Error")
        return jsonify({"status": "error", "message": str(e)}), 500

@app.route('/api/soundmcu/stop', methods=['POST'])
def stop_all_audio():
    try:
        voice_queue_manager.stop()
        music_player.stop()
        radio_streamer.stop()
        
        target_ip = CONFIG.get("soundmcu", {}).get("ip")
        if target_ip and target_ip != "Unknown":
            send_tcp_command_async(target_ip, 5002, "stop", "SoundMCU")
            
        broadcast_system_log("모든 오디오(음성/음악/라디오) 출력이 즉시 강제 중단되었습니다.", "Sound")
        return jsonify({"status": "success", "message": "All audio stopped successfully."})
    except Exception as e:
        broadcast_system_log(f"오디오 즉각 정지 처리 중 오류 발생: {e}", "Error")
        return jsonify({"status": "error", "message": str(e)}), 500

@app.route('/api/music/play', methods=['POST'])
def play_specific_music():
    try:
        data = request.json
        filename = data.get("filename", "")
        
        voice_queue_manager.stop()
        
        if filename:
            status = music_player.get_status()
            if not status.get("is_loaded") or music_player.filename != filename:
                music_player.stop()
                time.sleep(0.05)
                
                if getattr(sys, 'frozen', False):
                    base_dir = os.path.dirname(sys.executable)
                else:
                    base_dir = os.path.dirname(os.path.abspath(__file__))
                music_file = os.path.join(base_dir, "MusicData", filename)
                
                if not os.path.exists(music_file):
                    return jsonify({"status": "error", "message": f"File not found: {filename}"}), 404
                    
                broadcast_system_log(f"음악 파일 즉시 교체 로딩 중: {filename}", "Sound")
                music_player.load(filename, music_file)
                
        if music_player.play():
            broadcast_system_log(f"음악 재생 시작: {music_player.filename}", "Sound")
            return jsonify({"status": "success", "message": f"Playing {music_player.filename}", "status_data": music_player.get_status()})
        else:
            return jsonify({"status": "error", "message": "No music loaded to play."}), 400
            
    except Exception as e:
        broadcast_system_log(f"음악 재생 중 오류: {e}", "Error")
        return jsonify({"status": "error", "message": str(e)}), 500

@app.route('/api/music/pause', methods=['POST'])
def pause_music():
    try:
        music_player.pause()
        broadcast_system_log("배경 음악이 일시 중지되었습니다.", "Sound")
        return jsonify({"status": "success", "message": "Music paused.", "status_data": music_player.get_status()})
    except Exception as e:
        return jsonify({"status": "error", "message": str(e)}), 500

@app.route('/api/music/stop', methods=['POST'])
def stop_music():
    try:
        music_player.stop()
        broadcast_system_log("배경 음악 재생이 정지되었습니다.", "Sound")
        return jsonify({"status": "success", "message": "Music stopped.", "status_data": music_player.get_status()})
    except Exception as e:
        return jsonify({"status": "error", "message": str(e)}), 500

@app.route('/api/music/seek', methods=['POST'])
def seek_music():
    try:
        data = request.json
        progress = data.get("progress", 0.0)
        music_player.seek(progress)
        status = music_player.get_status()
        broadcast_system_log(f"음악 재생 위치 이동: {progress * 100:.1f}% ({status['position']:.1f}초)", "Sound")
        return jsonify({"status": "success", "message": f"Seeked to {progress * 100:.1f}%", "status_data": status})
    except Exception as e:
        return jsonify({"status": "error", "message": str(e)}), 500

@app.route('/api/music/eq', methods=['POST'])
def set_music_eq():
    try:
        data = request.json
        bass = float(data.get("bass", 0.0))
        mid = float(data.get("mid", 0.0))
        treble = float(data.get("treble", 0.0))
        
        music_player.set_eq(bass, mid, treble)
        broadcast_system_log(f"이퀄라이저 설정 변경: Bass {bass:+.1f}dB, Mid {mid:+.1f}dB, Treble {treble:+.1f}dB", "Sound")
        return jsonify({"status": "success", "message": "Equalizer updated.", "status_data": music_player.get_status()})
    except Exception as e:
        return jsonify({"status": "error", "message": str(e)}), 500

@app.route('/api/music/status', methods=['GET'])
def get_music_status_api():
    try:
        return jsonify({"status": "success", "status_data": music_player.get_status()})
    except Exception as e:
        return jsonify({"status": "error", "message": str(e)}), 500

@app.route('/api/radio/play', methods=['POST'])
def play_radio_station():
    try:
        data = request.json
        station_idx = data.get("station_idx", None)
        custom_url = data.get("custom_url", None)
        custom_name = data.get("custom_name", "Custom Station")
        
        target_ip = CONFIG.get("soundmcu", {}).get("ip")
        if not target_ip or target_ip == "Unknown":
            return jsonify({"status": "error", "message": "SoundMCU IP is not synchronized. Please connect the board."}), 400
            
        stream_url = ""
        station_name = ""
        
        if custom_url:
            stream_url = custom_url.strip()
            station_name = custom_name.strip() or "Custom Station"
        elif station_idx is not None:
            idx = int(station_idx)
            if 0 <= idx < len(RADIO_STATIONS):
                stream_url = RADIO_STATIONS[idx]["url"]
                station_name = RADIO_STATIONS[idx]["name"]
            else:
                return jsonify({"status": "error", "message": "Invalid station index"}), 400
        else:
            return jsonify({"status": "error", "message": "No station_idx or custom_url provided"}), 400
            
        if not stream_url:
            return jsonify({"status": "error", "message": "Stream URL is empty"}), 400
            
        voice_queue_manager.stop()
        music_player.stop()
        
        success = radio_streamer.start(station_name, stream_url)
        if success:
            broadcast_system_log(f"라디오 스트리밍 개시 완료: {station_name}", "Sound")
            return jsonify({"status": "success", "message": f"Streaming {station_name}", "status_data": radio_streamer.get_status()})
        else:
            return jsonify({"status": "error", "message": "Failed to start live radio streaming process."}), 500
            
    except Exception as e:
        broadcast_system_log(f"라디오 재생 요청 중 오류: {e}", "Error")
        return jsonify({"status": "error", "message": str(e)}), 500

@app.route('/api/radio/stop', methods=['POST'])
def stop_radio_station():
    try:
        radio_streamer.stop()
        broadcast_system_log("라디오 스트리밍이 즉시 강제 중단되었습니다.", "Sound")
        return jsonify({"status": "success", "message": "Radio streaming stopped.", "status_data": radio_streamer.get_status()})
    except Exception as e:
        return jsonify({"status": "error", "message": str(e)}), 500

@app.route('/api/radio/status', methods=['GET'])
def get_radio_status():
    try:
        return jsonify({"status": "success", "status_data": radio_streamer.get_status(), "stations": RADIO_STATIONS})
    except Exception as e:
        return jsonify({"status": "error", "message": str(e)}), 500

@app.route('/api/music/list', methods=['GET'])
def list_music_files():
    try:
        if getattr(sys, 'frozen', False):
            base_dir = os.path.dirname(sys.executable)
        else:
            base_dir = os.path.dirname(os.path.abspath(__file__))
        music_dir = os.path.join(base_dir, "MusicData")
        
        if not os.path.exists(music_dir):
            os.makedirs(music_dir, exist_ok=True)
            
        files = [f for f in os.listdir(music_dir) if f.lower().endswith(('.mp3', '.wav', '.ogg'))]
        return jsonify({"status": "success", "files": files})
    except Exception as e:
        return jsonify({"status": "error", "message": str(e)}), 500

import yt_dlp

@app.route('/api/youtube/play', methods=['POST'])
def play_youtube_audio():
    try:
        data = request.json
        url = data.get("url", "")
        if not url:
            return jsonify({"status": "error", "message": "No YouTube URL provided"}), 400
            
        broadcast_system_log(f"유튜브 오디오 다운로드 시작: {url}", "Sound")
        
        if getattr(sys, 'frozen', False):
            base_dir = os.path.dirname(sys.executable)
        else:
            base_dir = os.path.dirname(os.path.abspath(__file__))
            
        audio_dir = os.path.join(base_dir, "static", "audio")
        os.makedirs(audio_dir, exist_ok=True)
        
        import time
        unique_timestamp = int(time.time())
        output_base = os.path.join(audio_dir, f"yt_download_{unique_timestamp}")
        output_file = output_base + ".wav"
        
        for old_file in os.listdir(audio_dir):
            if old_file.startswith("yt_download_") and old_file.endswith(".wav"):
                try:
                    os.remove(os.path.join(audio_dir, old_file))
                except Exception:
                    pass
                    
        ydl_opts = {
            'format': 'bestaudio/best',
            'outtmpl': output_base + '.%(ext)s',
            'postprocessors': [{
                'key': 'FFmpegExtractAudio',
                'preferredcodec': 'wav',
                'preferredquality': '192',
            }],
            'keepvideo': False,
            'quiet': False,
            'no_warnings': False,
            'nocheckcertificate': True,
            'headers': {
                'User-Agent': 'Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/120.0.0.0 Safari/537.36',
                'Accept': 'text/html,application/xhtml+xml,application/xml;q=0.9,*/*;q=0.8',
                'Accept-Language': 'ko-KR,ko;q=0.9,en-US;q=0.8,en;q=0.7',
            }
        }
        
        try:
            with yt_dlp.YoutubeDL(ydl_opts) as ydl:
                ydl.download([url])
        except Exception as dl_err:
            broadcast_system_log(f"yt-dlp 다운로드 실행 오류: {dl_err}", "Error")
            return jsonify({"status": "error", "message": f"Download extraction failed: {dl_err}"}), 500
            
        if not os.path.exists(output_file):
            actual_wav = None
            for file_in_dir in os.listdir(audio_dir):
                if file_in_dir.startswith(f"yt_download_{unique_timestamp}") and file_in_dir.endswith(".wav"):
                    actual_wav = os.path.join(audio_dir, file_in_dir)
                    break
            if actual_wav and os.path.exists(actual_wav):
                if os.path.abspath(actual_wav) != os.path.abspath(output_file):
                    try:
                        os.rename(actual_wav, output_file)
                    except Exception:
                        output_file = actual_wav
                else:
                    output_file = actual_wav
            else:
                return jsonify({"status": "error", "message": "YouTube audio WAV extraction file not found on disk."}), 500
            
        broadcast_system_log("유튜브 오디오 추출 및 WAV 패키징 완료, 재생 준비 중...", "Sound")
        
        audio_data, fs = sf.read(output_file)
        if len(audio_data.shape) > 1:
            audio_data = audio_data[:, 0]
            
        pcm_data_float = resample_linear(audio_data, fs, SAMPLE_RATE)
        pcm_data = (pcm_data_float * 32767).astype(np.int16)
        
        silence_padding = np.zeros(1600, dtype=np.int16)
        final_pcm = np.concatenate([silence_padding, pcm_data])
        
        voice_queue_manager.add_pcm(final_pcm, priority=0)
        
        broadcast_system_log("유튜브 오디오 스트리밍 재생 시작", "Sound")
        return jsonify({"status": "success", "message": "YouTube audio streaming initiated successfully."})
        
    except Exception as e:
        broadcast_system_log(f"유튜브 재생 오류: {e}", "Error")
        return jsonify({"status": "error", "message": str(e)}), 500

@app.route('/api/audio/mirror/start', methods=['POST'])
def start_audio_mirror():
    try:
        success = audio_mirror_streamer.start()
        if success:
            return jsonify({"status": "success", "message": "PC 사운드 가로채기 스트리밍이 시작되었습니다."})
        else:
            return jsonify({"status": "error", "message": "PC 사운드 가로채기 스트리밍 시작 실패"}), 500
    except Exception as e:
        return jsonify({"status": "error", "message": str(e)}), 500

@app.route('/api/audio/mirror/stop', methods=['POST'])
def stop_audio_mirror():
    try:
        audio_mirror_streamer.stop()
        return jsonify({"status": "success", "message": "PC 사운드 가로채기 스트리밍이 정지되었습니다."})
    except Exception as e:
        return jsonify({"status": "error", "message": str(e)}), 500

@app.route('/api/audio/mirror/status', methods=['GET'])
def get_audio_mirror_status():
    try:
        return jsonify({"status": "success", "status_data": audio_mirror_streamer.get_status()})
    except Exception as e:
        return jsonify({"status": "error", "message": str(e)}), 500

@app.route('/api/soundmcu/status', methods=['GET'])
def get_soundmcu_status():
    return jsonify({
        "status": "success",
        "config": CONFIG["soundmcu"]
    })

@app.route('/api/soundmcu/settings', methods=['POST'])
def update_soundmcu_settings():
    data = request.json
    if "public_personality" in data:
        CONFIG["soundmcu"]["public_personality"] = data["public_personality"]
    if "private_instructions" in data:
        CONFIG["soundmcu"]["private_instructions"] = data["private_instructions"]
    if "tts_engine" in data:
        CONFIG["soundmcu"]["tts_engine"] = data["tts_engine"]
    if "tts_voice" in data:
        CONFIG["soundmcu"]["tts_voice"] = data["tts_voice"]
    if "tts_pitch" in data:
        CONFIG["soundmcu"]["tts_pitch"] = int(data["tts_pitch"])
    if "tts_rate" in data:
        CONFIG["soundmcu"]["tts_rate"] = int(data["tts_rate"])
    if "tts_effect" in data:
        CONFIG["soundmcu"]["tts_effect"] = data["tts_effect"]
    if "stt_engine" in data:
        CONFIG["soundmcu"]["stt_engine"] = data["stt_engine"]
    if "openai_api_key" in data:
        CONFIG["soundmcu"]["openai_api_key"] = data["openai_api_key"]
    if "azure_api_key" in data:
        CONFIG["soundmcu"]["azure_api_key"] = data["azure_api_key"]
    if "azure_region" in data:
        CONFIG["soundmcu"]["azure_region"] = data["azure_region"]
    if "mode" in data:
        CONFIG["soundmcu"]["mode"] = data["mode"]
    if "sensitivity" in data:
        CONFIG["soundmcu"]["sensitivity"] = int(data["sensitivity"])
    if "volume" in data:
        vol = int(data["volume"])
        CONFIG["soundmcu"]["volume"] = vol
        os.environ["SOUNDMCU_VOLUME"] = str(vol)
        target_ip = CONFIG.get("soundmcu", {}).get("ip")
        if target_ip and target_ip != "Unknown":
            send_tcp_command_async(target_ip, 5002, f"vol={vol}", "SoundMCU")
            try:
                udp_sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
                udp_sock.sendto(f"vol={vol}".encode(), (target_ip, network_manager.AUDIO_UDP_PORT))
            except Exception as e:
                print(f"[Volume UDP] Failed to send: {e}")
    if "ip" in data and data["ip"]:
        ip_val = data["ip"]
        CONFIG["soundmcu"]["ip"] = ip_val
        CONFIG["controlmcu_ip"] = ip_val
        device_stats["SoundMCU"]["ip"] = ip_val
        device_stats["ControlMCU"]["ip"] = ip_val
    
    save_config_to_env()
    return jsonify({"status": "success"})

@app.route('/api/soundmcu/voices', methods=['GET'])
def get_available_voices():
    engine_name = request.args.get('engine', CONFIG["soundmcu"]["tts_engine"])
    
    voices = []
    try:
        if engine_name == "Edge-TTS":
            async def list_edge_voices():
                all_voices = await edge_tts.VoicesManager.create()
                ko_voices = all_voices.find(Locale="ko-KR")
                return [{"id": v["ShortName"], "name": f"{v['FriendlyName']} ({v['Gender']})"} for v in ko_voices]
            voices = asyncio.run(list_edge_voices())
        elif engine_name == "OpenAI":
            voices = [
                {"id": "alloy", "name": "Alloy (Neutral)"},
                {"id": "echo", "name": "Echo (Mature Male)"},
                {"id": "fable", "name": "Fable (Expressive)"},
                {"id": "onyx", "name": "Onyx (Deep Male)"},
                {"id": "nova", "name": "Nova (Bright Female)"},
                {"id": "shimmer", "name": "Shimmer (Soft Female)"}
            ]
        elif engine_name == "Sherpa":
            voices = [{"id": "vits-ko-rkss", "name": "Korean VITS (Local Free)"}]
        elif engine_name == "gTTS":
            voices = [{"id": "ko", "name": "Google Korean (Standard)"}]
        elif engine_name == "Azure-TTS":
            voices = [
                {"id": "ko-KR-SunHiNeural", "name": "SunHi (Female - Neural)"},
                {"id": "ko-KR-InJoonNeural", "name": "InJoon (Male - Neural)"},
                {"id": "ko-KR-JiMinNeural", "name": "JiMin (Female)"},
                {"id": "ko-KR-SeoHyeonNeural", "name": "SeoHyeon (Female)"},
                {"id": "ko-KR-BongJinNeural", "name": "BongJin (Male)"}
            ]
        elif engine_name == "SAPI":
            voices = [{"id": "default", "name": "System Default (PowerShell)"}]
    except Exception as e:
        print(f"Error fetching voices for {engine_name}: {e}")
        
    return jsonify({"status": "success", "voices": voices})

@app.route('/api/ai_persona', methods=['GET', 'POST'])
def ai_persona_api():
    if request.method == 'GET':
        try:
            with open(config.AI_PERSONA_FILE, "r", encoding="utf-8") as f:
                content = f.read()
            return jsonify({"status": "success", "content": content})
        except Exception as e:
            return jsonify({"status": "error", "message": str(e)}), 500
    else:
        data = request.json
        content = data.get("content", "")
        if not content:
            return jsonify({"status": "error", "message": "No content"}), 400
        if save_ai_persona(content):
            broadcast_system_log("AI 페르소나 파일이 저장되었습니다.", "System")
            return jsonify({"status": "success"})
        return jsonify({"status": "error", "message": "Save failed"}), 500

@app.route('/outputs/audio/<path:filename>')
def serve_audio_file(filename):
    broadcast_system_log(f"브라우저 오디오 요청 수신: {filename}", "Server")
    if getattr(sys, 'frozen', False):
        base_dir = os.path.dirname(sys.executable)
    else:
        base_dir = os.path.dirname(os.path.abspath(__file__))
        
    audio_path = os.path.join(base_dir, 'static', 'audio')
    
    mimetype = "audio/wav"
    if filename.endswith(".mp3"): mimetype = "audio/mpeg"
    elif filename.endswith(".ogg"): mimetype = "audio/ogg"
    
    return send_from_directory(audio_path, filename, mimetype=mimetype)

@app.route('/api/soundmcu/config', methods=['POST'])
def update_soundmcu_config():
    data = request.json
    if "mode" in data:
        CONFIG["soundmcu"]["mode"] = data["mode"]
    if "sensitivity" in data:
        CONFIG["soundmcu"]["sensitivity"] = int(data["sensitivity"])
    if "status" in data and CONFIG["soundmcu"]["mode"] == "PTT":
        network_manager.update_soundmcu_status(data["status"])
    return jsonify({"status": "success", "config": CONFIG["soundmcu"]})

@app.route('/upload', methods=['POST'])
def upload_image():
    try:
        img_bytes = request.data
        if not img_bytes: 
            return jsonify({"error": "No data"}), 400
            
        size_kb = round(len(img_bytes) / 1024, 1)
        broadcast_system_log(f"Received image via TCP (HTTP) - {size_kb} KB", "Network")
        
        last_seen["imagesendermcu"] = time.time()
        remote_ip = request.remote_addr
        device_stats["ImageSenderMCU"]["ip"] = remote_ip
        CONFIG["imagesendermcu_ip"] = remote_ip
        
        threading.Thread(target=process_udp_frame, args=(img_bytes, "TCP"), daemon=True).start()
        
        return jsonify({"status": "received via http", "size": len(img_bytes)})
    except Exception as e:
        return jsonify({"error": str(e)}), 500

@app.route('/api/shutdown', methods=['POST'])
def shutdown():
    print("[Server] Shutdown requested from UI. Closing server...")
    
    def kill_server():
        time.sleep(0.5)
        print("[Server] Finalizing shutdown. Goodbye!")
        os._exit(0)
        
    threading.Thread(target=kill_server, daemon=True).start()
    return jsonify({"status": "success", "message": "Server is shutting down..."})

@app.route('/api/test_melody', methods=['POST'])
def test_melody():
    try:
        target_ip = CONFIG.get("soundmcu", {}).get("ip")
        if not target_ip:
            return jsonify({"status": "error", "message": "ControlMCU IP is not synchronized. Please boot or connect the board."}), 400
            
        def generate_tone(freq, dur_ms):
            n = int(SAMPLE_RATE * dur_ms / 1000)
            t = np.linspace(0, dur_ms / 1000.0, n, endpoint=False)
            wave = (np.sin(2 * np.pi * freq * t) * 8000).astype(np.int16)
            return wave
            
        tones = [
            generate_tone(523, 150),
            generate_tone(659, 150),
            generate_tone(784, 150),
            generate_tone(1046, 250)
        ]
        melody_data = np.concatenate(tones)
        
        def send_udp_audio_async(data_to_send, target_ip):
            try:
                sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
                chunk_size = 1024
                chunks = range(0, len(data_to_send), chunk_size // 2)
                broadcast_system_log(f"MCU 멜로디 테스트 전송 시작: {len(chunks)} 패킷", "MCU")
                
                packet_duration = 0.032
                start_time = time.perf_counter()
                
                for idx, i in enumerate(chunks):
                    chunk = data_to_send[i:i + chunk_size // 2].tobytes()
                    sock.sendto(chunk, (target_ip, AUDIO_UDP_PORT))
                    
                    target_time = start_time + (idx + 1) * packet_duration
                    sleep_dur = target_time - time.perf_counter()
                    if sleep_dur > 0:
                        time.sleep(sleep_dur)
                        
                silent_chunk = bytes(chunk_size)
                for idx_flush in range(5):
                    sock.sendto(silent_chunk, (target_ip, AUDIO_UDP_PORT))
                    target_time = start_time + (len(chunks) + idx_flush + 1) * packet_duration
                    sleep_dur = target_time - time.perf_counter()
                    if sleep_dur > 0:
                        time.sleep(sleep_dur)
                        
                broadcast_system_log("MCU 멜로디 테스트 전송 완료", "MCU")
            except Exception as e:
                broadcast_system_log(f"MCU 멜로디 전송 실패: {e}", "Error")
                
        threading.Thread(target=send_udp_audio_async, args=(melody_data, target_ip)).start()
        return jsonify({"status": "success", "message": "Melody test initiated."})
    except Exception as e:
        return jsonify({"status": "error", "message": str(e)}), 500

@app.route('/api/test_tts', methods=['POST'])
def test_tts():
    try:
        data = request.get_json(silent=True) or {}
        text = data.get("text", "안녕하세요. 지능형 로봇 시스템 테스트 중입니다.")
        target_ip = CONFIG.get("soundmcu", {}).get("ip")
        if not target_ip:
            return jsonify({"status": "error", "message": "ControlMCU IP is not synchronized. Please boot or connect the board."}), 400
            
        broadcast_system_log(f"TTS 테스트 전송 요청: '{text}'", "MCU")
        tts_to_pcm_and_send(text)
        
        return jsonify({"status": "success", "message": f"TTS test '{text}' initiated."})
    except Exception as e:
        return jsonify({"status": "error", "message": str(e)}), 500
