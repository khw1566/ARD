from flask import Blueprint, request, jsonify, Response
import time
import threading
import io
import json
import base64
from datetime import datetime
from PIL import Image
import google.generativeai as genai

from config.config_manager import config
from core.state_manager import state_manager
from network.sse_manager import sse_manager
from network.tcp_client import send_tcp_command_async
from audio.tts_engine import tts_to_pcm_and_send

control_bp = Blueprint('control', __name__)

drive_pid_target = 100
drive_settings = {
    'hb_interval_ms': 500,
    'i2c_clock_hz': 100000,
    'pid_mode': True,
    'pid_target_tof': 150,
    'z_step': 50
}
last_hmi_image = None

def play_emergency_stop_voice():
    tts_to_pcm_and_send("비상 정지 버튼이 눌렸습니다. 모든 장치를 정지합니다.", priority=0)

def send_end_of_speech_beep(ip):
    # Dummy implementation for beep if required, otherwise just use TTS
    pass

def execute_drive_sequence(direction, target_ip):
    # Keep the logic simple or adapt from original
    pass

@control_bp.route('/api/control/update', methods=['POST'])
def update_control_state():
    try:
        global drive_pid_target
        data = request.get_json(silent=True)
        if not data: return jsonify({"status": "error"}), 400
        
        state_manager.last_seen["controlmcu"] = time.time()
        state_manager.last_seen["soundmcu"] = time.time()
        remote_ip = request.remote_addr
        state_manager.device_stats["ControlMCU"]["ip"] = remote_ip
        state_manager.device_stats["ControlMCU"]["rssi"] = data.get("rssi", 0)
        state_manager.device_stats["ControlMCU"]["temp"] = data.get("temp", 0.0)
        
        config["soundmcu"]["ip"] = remote_ip
        state_manager.device_stats["SoundMCU"]["ip"] = remote_ip
        
        prev_mode = state_manager.device_stats["ControlMCU"].get("mode", "A")
        prev_sub_mode = state_manager.device_stats["ControlMCU"].get("sub_mode", 1)
        prev_joystick = state_manager.device_stats["ControlMCU"].get("joystick", {})

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
            sse_manager.broadcast_system_log(log_msg, "ControlMCU")
            
            if sub_mode != prev_sub_mode:
                if sub_mode == 2:
                    sse_manager.broadcast_system_log("Voice Input Button Pressed. Starting PC Mic...", "ControlMCU")
                    send_tcp_command_async(remote_ip, 5002, "mic_ready", "ControlMCU")
                    sse_manager.broadcast_event({"type": "control_mic", "action": "start"})
                else:
                    sse_manager.broadcast_system_log("Voice Input Button Released. Stopping PC Mic...", "ControlMCU")
                    send_tcp_command_async(remote_ip, 5002, "mic_done", "ControlMCU")
                    sse_manager.broadcast_event({"type": "control_mic", "action": "stop"})
            
        state_manager.device_stats["ControlMCU"]["mode"] = mode
        state_manager.device_stats["ControlMCU"]["sub_mode"] = sub_mode
        state_manager.device_stats["ControlMCU"]["joystick"] = joystick
        
        cmd = None
        target_mcu = None
        steps = 50
        
        if mode == "A": 
            target_mcu = "Robot3AxisMCU"
            if joystick.get("up"): cmd = f"rz{steps}"
            elif joystick.get("down"): cmd = f"rz-{steps}"
            elif joystick.get("left"): cmd = f"rx-{steps}"
            elif joystick.get("right"): cmd = f"rx{steps}"
            
        elif mode == "N": 
            target_mcu = "Robot3AxisMCU"
            if joystick.get("up"): cmd = f"ry{steps}"
            elif joystick.get("down"): cmd = f"ry-{steps}"
            elif joystick.get("left") and state_changed: cmd = "grab open"
            elif joystick.get("right") and state_changed: cmd = "grab close"
                
        elif mode == "B" and state_changed: 
            target_mcu = "DriveMCU"
            if joystick.get("up"): 
                drive_pid_target += 5
                cmd = f"mzt{drive_pid_target}"
                threading.Thread(target=tts_to_pcm_and_send, args=(f"Z축 목표 거리를 {drive_pid_target}으로 올립니다.",), daemon=True).start()
            elif joystick.get("down"):
                drive_pid_target = max(0, drive_pid_target - 5)
                cmd = f"mzt{drive_pid_target}"
                threading.Thread(target=tts_to_pcm_and_send, args=(f"Z축 목표 거리를 {drive_pid_target}으로 내립니다.",), daemon=True).start()
            elif joystick.get("left") or joystick.get("right"):
                direction = "LEFT" if joystick.get("left") else "RIGHT"
                threading.Thread(target=execute_drive_sequence, args=(direction, state_manager.device_stats["DriveMCU"]["ip"]), daemon=True).start()
                cmd = None

        if cmd and target_mcu:
            target_ip = state_manager.device_stats[target_mcu]["ip"]
            if target_ip and target_ip != "Unknown":
                if ',' in cmd and target_mcu == "DriveMCU":
                    cmds = cmd.split(',')
                    for c in cmds:
                        send_tcp_command_async(target_ip, 5002, c, target_mcu)
                        time.sleep(0.01)
                else:
                    send_tcp_command_async(target_ip, 5002, cmd, target_mcu)
                
                if state_changed:
                    sse_manager.broadcast_system_log(f"ControlMCU -> {target_mcu}: {cmd}", "System")
                    
            if target_mcu == "Robot3AxisMCU":
                try:
                    s_val = 0
                    if cmd.startswith("rx") or cmd.startswith("ry") or cmd.startswith("rz"):
                        s_val = int(cmd[2:])
                    
                    if cmd.startswith("rx"): 
                        state_manager.robot3axis_state["x_steps"] += s_val
                        state_manager.robot3axis_state["x_deg"] += (s_val * 0.5)
                    elif cmd.startswith("ry"): 
                        state_manager.robot3axis_state["y_steps"] += s_val
                        state_manager.robot3axis_state["y_mm"] += (s_val * -0.043)
                    elif cmd.startswith("rz"): 
                        state_manager.robot3axis_state["z_steps"] += s_val
                        state_manager.robot3axis_state["z_mm"] += (s_val * -0.228)
                    elif cmd == "grab open":
                        state_manager.robot3axis_state["grab_state"] = "open"
                    elif cmd == "grab close":
                        state_manager.robot3axis_state["grab_state"] = "closed"
                    
                    sse_manager.broadcast_event({"type": "robot3axis_update", "state": state_manager.robot3axis_state})
                except: pass
            elif target_mcu == "DriveMCU" and state_changed:
                state_manager.update_motor_state(cmd)
                        
        sse_manager.broadcast_event({"type": "status", "status": state_manager.get_status()})
        return jsonify({"status": "success"})
    except Exception as e:
        print(f"Control API Error: {e}")
        return jsonify({"status": "error", "message": str(e)}), 500

@control_bp.route('/api/drivemcu/command', methods=['POST'])
def send_drive_command():
    global drive_settings
    data = request.json
    cmd = data.get("command")
    if cmd:
        if cmd == "CMD_HOME":
            steps_x = -state_manager.motor_state["mx_steps"]
            steps_y = -state_manager.motor_state["my_steps"]
            cmd = f"b{steps_x},{steps_y}"
            sse_manager.broadcast_system_log(f"[Home] Restoring X:{steps_x}, Y:{steps_y}", "System")

        settings_changed = False
        try:
            if cmd.startswith('mzh'):
                val = int(cmd[3:])
                drive_settings['hb_interval_ms'] = val
                settings_changed = True
                sse_manager.broadcast_system_log(f"[HMI] Heartbeat interval set: {val}ms", "System")
            elif cmd.startswith('mzc'):
                val = int(cmd[3:])
                drive_settings['i2c_clock_hz'] = val
                settings_changed = True
                sse_manager.broadcast_system_log(f"[HMI] I2C clock set: {val}Hz", "System")
            elif cmd.startswith('mzm'):
                val = cmd[3:]
                drive_settings['pid_mode'] = (val == '1')
                settings_changed = True
                sse_manager.broadcast_system_log(f"[HMI] PID mode: {'ON' if drive_settings['pid_mode'] else 'OFF'}", "System")
            elif cmd.startswith('mzt'):
                val = int(cmd[3:])
                drive_settings['pid_target_tof'] = val
                settings_changed = True
                sse_manager.broadcast_system_log(f"[HMI] PID target ToF: {val}mm", "System")
            elif cmd.startswith('mzs'):
                val_str = cmd[3:]
                if val_str.startswith('+') or val_str.startswith('-'):
                    delta = int(val_str)
                    drive_settings['z_step'] = max(10, drive_settings['z_step'] + delta)
                    settings_changed = True
                    sse_manager.broadcast_system_log(f"[HMI] Z step: {drive_settings['z_step']}", "System")
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

        state_manager.update_motor_state(cmd)
        
        if config.get("drivemcu_ip"):
            send_tcp_command_async(config["drivemcu_ip"], 5002, cmd, "DriveMCU")
        else:
            sse_manager.broadcast_system_log(f"Drive command queued (No IP): {cmd}", "System")
            
        return jsonify({"status": "success", "command": cmd, "settings": drive_settings})
    return jsonify({"status": "error"}), 400

@control_bp.route('/api/hmi/info', methods=['GET'])
def get_hmi_info():
    from ai.llm_manager import conversation_history
    chat_logs = []
    for entry in conversation_history[-6:]:
        chat_logs.append({
            "r": "U" if entry["role"] == "User" else "A",
            "t": entry["text"][:100]
        })
    
    last_analysis = {
        "obj": state_manager.device_stats.get("last_analysis", {}).get("object", "-"),
        "cat": state_manager.device_stats.get("last_analysis", {}).get("category", "-")
    }

    return jsonify({
        "chat": chat_logs,
        "motor": state_manager.motor_state,
        "analysis": last_analysis,
        "status": {
            "drive": state_manager.get_device_status("DriveMCU"),
            "sound": state_manager.get_device_status("SoundMCU")
        }
    })

# --- Automatic Sorting Sequence Controller ---
sorting_lock = threading.Lock()
sorting_status = {
    "is_running": False,
    "step": "idle",
    "step_num": 0,
    "total_steps": 5,
    "step_desc": "대기 중",
    "object_name": "-",
    "material": "-",
    "target_angle": 0,
    "dump_angle": 45,
    "progress": 0,
    "error": None
}

def broadcast_sorting_update():
    """웹 UI에 선별 진행 상태 브로드캐스트"""
    sse_manager.broadcast_event({
        "type": "sorting_update",
        "status": sorting_status
    })

def execute_sorting_pipeline():
    """수동식 작동 선별 시퀀스 스레드:
    1. 대기/시작 -> 2. 카메라 촬영/영상 획득 -> 3. Gemini 2.5 Flash AI 추론
    -> 4. 목표 각도 선회 (Yaw) -> 5. 덤핑 배출 (Roll) -> 6. 원위치 복귀 (Homing)
    """
    global sorting_status
    with sorting_lock:
        sorting_status["is_running"] = True
        sorting_status["error"] = None
        sorting_status["progress"] = 5
        sorting_status["step"] = "capturing"
        sorting_status["step_num"] = 1
        sorting_status["step_desc"] = "영상 획득 및 카메라 촬영 중"
        broadcast_sorting_update()
        sse_manager.broadcast_system_log("[Sorting] 선별 시작: 영상 획득 단계", "Sorting")

        # 1. 촬영 요청 (ImageSenderMCU)
        config["imagesendermcu"]["command"] = "capture"
        threading.Thread(target=tts_to_pcm_and_send, args=("물체를 감지하고 인공지능 분석을 시작합니다.",), daemon=True).start()
        
        # 캡처 완료 대기
        time.sleep(1.8)

        # 2. AI 추론
        sorting_status["step"] = "inference"
        sorting_status["step_num"] = 2
        sorting_status["progress"] = 30
        sorting_status["step_desc"] = "Gemini 2.5 Flash 재질 분류 분석 중"
        broadcast_sorting_update()
        sse_manager.broadcast_system_log("[Sorting] Gemini 2.5 Flash 재질 추론 중...", "Sorting")

        api_key = config.get("api_key", "")
        model_name = config.get("model_name", "gemini-2.5-flash-lite")
        result_obj = "Unknown"
        result_mat = "Others"

        raw_img = state_manager.last_hmi_image
        img_pil = None
        if raw_img:
            try:
                img_pil = Image.open(io.BytesIO(raw_img))
            except Exception as e:
                print(f"[Sorting] Image parse error: {e}")

        if api_key:
            try:
                genai.configure(api_key=api_key)
                model = genai.GenerativeModel(model_name)
                prompt = (
                    "Identify the main object in this image and strictly categorize its material into ONE of these 5 categories: [Plastic, Metal, Glass, Paper, Others]. "
                    "Output ONLY JSON: {\"object\": \"name\", \"category\": \"one of the 5 categories\"}"
                )
                if img_pil:
                    response = model.generate_content([prompt, img_pil])
                else:
                    # 영상 부재 시 기본 분류 테스트 프롬프트
                    response = model.generate_content([prompt + " If image is not clear, assume recyclable Plastic."])
                
                res_clean = response.text.replace('```json', '').replace('```', '').strip()
                parsed = json.loads(res_clean)
                result_obj = parsed.get("object", "Waste Item")
                result_mat = parsed.get("category", "Plastic")
            except Exception as ex:
                sse_manager.broadcast_system_log(f"[Sorting] AI 추론 오류: {ex}. 기본 분류(Plastic)로 진행합니다.", "Warning")
                result_obj = "재활용품"
                result_mat = "Plastic"
        else:
            result_obj = "페트병(시뮬레이션)"
            result_mat = "Plastic"

        sorting_status["object_name"] = result_obj
        sorting_status["material"] = result_mat
        state_manager.device_stats["last_analysis"] = {"object": result_obj, "category": result_mat}

        # 3. 재질별 방위각 매핑 (0~360도)
        # Metal -> 90도 (우측), Plastic -> 180도 (후면), Paper -> 270도 (좌측), Glass/Others -> 0도 (전면 직진)
        mat_angles = {
            "Metal": 90,
            "Plastic": 180,
            "Paper": 270,
            "Glass": 0,
            "Others": 0
        }
        target_yaw = mat_angles.get(result_mat, 0)
        dump_roll = 45 # 덤핑 배출 각도 45도
        sorting_status["target_angle"] = target_yaw
        sorting_status["dump_angle"] = dump_roll

        # 음성 안내
        mat_korean = {"Metal": "금속 캔", "Plastic": "플라스틱 페트", "Paper": "종이", "Glass": "유리", "Others": "일반 폐기물"}.get(result_mat, result_mat)
        threading.Thread(target=tts_to_pcm_and_send, args=(f"{mat_korean}으로 판별되었습니다. {target_yaw}도 수거함으로 선회 배출합니다.",), daemon=True).start()

        # 4. 차동 기구 선회 제어 (Yaw Rotation)
        sorting_status["step"] = "rotating"
        sorting_status["step_num"] = 3
        sorting_status["progress"] = 55
        sorting_status["step_desc"] = f"배출함 {target_yaw}° 위치로 디스크 선회 중"
        broadcast_sorting_update()

        # 기구학 스텝 계산: 100 steps = PI rad = 180도
        # Yaw 회전: motorX = +steps, motorY = +steps (dr 방향)
        yaw_steps = int(round((target_yaw / 180.0) * 100))
        gear_ratio_x = float(config.get("gear_ratio_x", 1.0))
        gear_ratio_y = float(config.get("gear_ratio_y", 1.0))

        if yaw_steps != 0:
            mx_steps = int(round(yaw_steps * gear_ratio_x))
            my_steps = int(round(yaw_steps * gear_ratio_y))
            cmd_yaw = f"b{mx_steps},{my_steps}"
            drive_ip = config.get("drivemcu_ip")
            if drive_ip:
                send_tcp_command_async(drive_ip, 5002, cmd_yaw, "DriveMCU")
            state_manager.update_motor_state(cmd_yaw)
            sse_manager.broadcast_system_log(f"[Sorting] 선회 명령 전송: {cmd_yaw} (Target: {target_yaw}°)", "Sorting")
            time.sleep(2.0)
        else:
            time.sleep(0.5)

        # 5. 전도 덤핑 배출 (Roll Tilt)
        sorting_status["step"] = "dumping"
        sorting_status["step_num"] = 4
        sorting_status["progress"] = 75
        sorting_status["step_desc"] = f"디스크 {dump_roll}° 전도 덤핑 배출 중"
        broadcast_sorting_update()

        # Roll 전도: motorX = +steps, motorY = -steps (df 방향)
        dump_steps = int(round((dump_roll / 180.0) * 100))
        mx_dump = int(round(dump_steps * gear_ratio_x))
        my_dump = int(round(-dump_steps * gear_ratio_y))
        cmd_dump = f"b{mx_dump},{my_dump}"
        drive_ip = config.get("drivemcu_ip")
        if drive_ip:
            send_tcp_command_async(drive_ip, 5002, cmd_dump, "DriveMCU")
        state_manager.update_motor_state(cmd_dump)
        sse_manager.broadcast_system_log(f"[Sorting] 덤핑 배출 명령 전송: {cmd_dump} (Dump: {dump_roll}°)", "Sorting")
        time.sleep(2.0) # 배출 낙하 대기

        # 덤핑 복귀 (Roll Un-tilt)
        mx_undump = -mx_dump
        my_undump = -my_dump
        cmd_undump = f"b{mx_undump},{my_undump}"
        if drive_ip:
            send_tcp_command_async(drive_ip, 5002, cmd_undump, "DriveMCU")
        state_manager.update_motor_state(cmd_undump)
        sse_manager.broadcast_system_log(f"[Sorting] 덤핑 복귀 명령 전송: {cmd_undump}", "Sorting")
        time.sleep(1.8)

        # 6. 원위치 복귀 (Homing)
        sorting_status["step"] = "homing"
        sorting_status["step_num"] = 5
        sorting_status["progress"] = 90
        sorting_status["step_desc"] = "디스크 및 시스템 원점 복귀 중"
        broadcast_sorting_update()

        if yaw_steps != 0:
            mx_home = -mx_steps
            my_home = -my_steps
            cmd_home = f"b{mx_home},{my_home}"
            if drive_ip:
                send_tcp_command_async(drive_ip, 5002, cmd_home, "DriveMCU")
            state_manager.update_motor_state(cmd_home)
            sse_manager.broadcast_system_log(f"[Sorting] 원점 선회 복귀 명령 전송: {cmd_home}", "Sorting")
            time.sleep(2.0)

        # 7. 완료
        sorting_status["is_running"] = False
        sorting_status["step"] = "complete"
        sorting_status["step_num"] = 5
        sorting_status["progress"] = 100
        sorting_status["step_desc"] = f"선별 및 배출 완료 ({result_mat})"
        broadcast_sorting_update()
        threading.Thread(target=tts_to_pcm_and_send, args=("선별 및 배출이 완료되었습니다. 대기 상태로 복귀합니다.",), daemon=True).start()
        sse_manager.broadcast_system_log(f"[Sorting] 선별 시퀀스 종료. 객체: {result_obj}, 재질: {result_mat}", "Sorting")

@control_bp.route('/api/sorting/start', methods=['POST'])
def start_sorting_sequence():
    """수동 선별 시퀀스 시작 트리거 API"""
    if sorting_status["is_running"]:
        return jsonify({"status": "busy", "message": "선별 작업이 이미 진행 중입니다."}), 409
    threading.Thread(target=execute_sorting_pipeline, daemon=True).start()
    return jsonify({"status": "started", "message": "선별 시퀀스가 시작되었습니다."})

@control_bp.route('/api/sorting/status', methods=['GET'])
def get_sorting_status():
    """선별 작업 현재 상태 조회 API"""
    return jsonify({"status": "success", "sorting": sorting_status})
