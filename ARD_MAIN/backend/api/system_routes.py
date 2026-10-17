from flask import Blueprint, request, jsonify
from config.config_manager import config, config_manager
from core.state_manager import state_manager
from network.sse_manager import sse_manager
from ai.llm_manager import pending_hmi_actions
from audio.music_player import music_player

system_bp = Blueprint('system', __name__)

@system_bp.route('/api/status', methods=['GET'])
def api_status():
    status = state_manager.get_status()
    music_status = music_player.get_status()
    # Add music status into the response payload
    status["music"] = music_status
    return jsonify(status)

@system_bp.route('/api/config', methods=['GET'])
def get_config():
    return jsonify(config)

@system_bp.route('/api/config', methods=['POST'])
def update_config():
    data = request.json
    for k, v in data.items():
        if k in config:
            if isinstance(config[k], dict) and isinstance(v, dict):
                config[k].update(v)
            else:
                config[k] = v
                
    config_manager.save_config()
    sse_manager.broadcast_system_log("설정이 업데이트 되었습니다.", "System")
    sse_manager.broadcast_event({"type": "status", "status": state_manager.get_status()})
    return jsonify({"status": "success", "config": config})

@system_bp.route('/api/hmi/actions', methods=['GET'])
def get_pending_actions():
    return jsonify({"actions": pending_hmi_actions})

@system_bp.route('/api/hmi/actions/<action_id>', methods=['POST'])
def execute_hmi_action(action_id):
    global pending_hmi_actions
    data = request.json
    approved = data.get("approved", False)
    
    action_to_exec = None
    for action in pending_hmi_actions:
        if action["id"] == action_id:
            action_to_exec = action
            break
            
    if not action_to_exec:
        return jsonify({"status": "error", "message": "Action not found"}), 404
        
    pending_hmi_actions = [a for a in pending_hmi_actions if a["id"] != action_id]
    
    if approved:
        try:
            sse_manager.broadcast_system_log(f"HMI 승인됨: {action_to_exec['desc']}", "System")
            action_to_exec["callback"]()
            return jsonify({"status": "success"})
        except Exception as e:
            return jsonify({"status": "error", "message": str(e)}), 500
    else:
        sse_manager.broadcast_system_log(f"HMI 거절됨: {action_to_exec['desc']}", "System")
        return jsonify({"status": "rejected"})
