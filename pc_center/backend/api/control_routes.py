from flask import Blueprint, request, jsonify, Response
import time
import threading
from datetime import datetime

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
