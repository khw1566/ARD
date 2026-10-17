import time
from config.config_manager import config

class StateManager:
    """Manages the application's global state and device statistics."""
    def __init__(self):
        self.last_hmi_image = None
        
        self.last_seen = {
            "imagesendermcu": 0,
            "controlmcu": 0,
            "drivemcu": 0,
            "hmi_mcu": 0,
            "soundmcu": 0,
            "robot3axismcu": 0,
            "transmitter": 0
        }

        self.device_stats = {
            "ImageSenderMCU": {"ip": "Unknown", "rssi": -99, "temp": 0.0},
            "ControlMCU": {"ip": "Unknown", "rssi": -99, "temp": 0.0, "mode": "A", "sub_mode": 1, "joystick": {}},
            "DriveMCU": {"ip": "Unknown", "rssi": -99, "temp": 0.0},
            "HMI_MCU": {"ip": "Unknown", "rssi": -99, "temp": 0.0},
            "SoundMCU": {"ip": "Unknown", "rssi": -99, "temp": 0.0, "status": "Idle"},
            "Robot3AxisMCU": {"ip": "Unknown", "rssi": -99, "temp": 0.0},
            "last_analysis": {"object": "Unknown", "category": "Unknown"}
        }

        self.motor_state = {
            "mx_steps": 0,
            "my_steps": 0,
            "mx_deg": 0.0,
            "my_deg": 0.0
        }

        self.robot3axis_state = {
            "x_steps": 0, "y_steps": 0, "z_steps": 0,
            "x_deg": 0.0, "y_mm": 0.0, "z_mm": 0.0,
            "x_speed": 4000, "y_speed": 4000, "z_speed": 4000,
            "grab_state": "open"
        }

        self.drive_sequence_state = "IDLE"
        self.drive_pid_target = 150
        
        self.config = config

    def get_device_status(self, device_key):
        """Returns the status of a specific device based on last seen time."""
        if self.device_stats[device_key]["ip"] == "Unknown":
            return "Offline"
            
        key_map = {
            "ImageSenderMCU": "imagesendermcu",
            "ControlMCU": "controlmcu",
            "DriveMCU": "drivemcu",
            "HMI_MCU": "hmi_mcu",
            "SoundMCU": "soundmcu",
            "Robot3AxisMCU": "robot3axismcu"
        }
        
        last = self.last_seen.get(key_map.get(device_key, ""), 0)
        
        if device_key == "ControlMCU" and last > 0:
            if time.time() - last > 5:
                self.device_stats["ControlMCU"]["joystick"] = {}
                return "Offline"
                
        threshold = 30 # STATUS_THRESHOLD
        if time.time() - last > threshold:
            return "Offline"
        return "Online"

    def get_status(self):
        """Returns the complete system status."""
        return {
            "connections": {
                "imagesendermcu": self.get_device_status("ImageSenderMCU"),
                "controlmcu": self.get_device_status("ControlMCU"),
                "drivemcu": self.get_device_status("DriveMCU"),
                "hmi_mcu": self.get_device_status("HMI_MCU"),
                "soundmcu": self.get_device_status("SoundMCU"),
                "robot3axismcu": self.get_device_status("Robot3AxisMCU")
            },
            "stats": self.device_stats,
            "motor": self.motor_state,
            "robot3axis": self.robot3axis_state,
            "config": {
                "imagesendermcu": self.config.get("imagesendermcu", {}),
                "soundmcu": self.config.get("soundmcu", {})
            }
        }

    def update_motor_state(self, cmd):
        """Updates motor state based on command string."""
        if not cmd:
            return
            
        if cmd == "CMD_HOME":
            self.motor_state["mx_steps"] = 0
            self.motor_state["my_steps"] = 0
            self.motor_state["mx_deg"] = 0.0
            self.motor_state["my_deg"] = 0.0
            return
            
        if cmd.startswith('b'):
            try:
                parts = cmd[1:].split(',')
                if len(parts) == 2:
                    dx = int(parts[0])
                    dy = int(parts[1])
                    self.motor_state["mx_steps"] += dx
                    self.motor_state["my_steps"] += dy
                    
                    self.motor_state["mx_deg"] = round(self.motor_state["mx_deg"] + (dx * 1.8), 2)
                    self.motor_state["my_deg"] = round(self.motor_state["my_deg"] + (dy * 1.8), 2)
            except Exception as e:
                print(f"Error parsing motor command '{cmd}': {e}")

# Create a singleton instance
state_manager = StateManager()
