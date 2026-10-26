import os
import json
import traceback

class ConfigManager:
    """Manages the application configuration."""
    def __init__(self, config_file="config.json", persona_file="ai_persona.txt"):
        self.config_file = config_file
        self.persona_file = persona_file

        self.config = {
            "api_key": os.getenv("GEMINI_API_KEY", ""),
            "model_name": "gemini-2.5-flash-lite",
            "system_instruction": "You are a helpful AI assistant. Always respond concisely and kindly in Korean.",

            "imagesendermcu_ip": "Unknown",
            "controlmcu_ip": "Unknown",
            "drivemcu_ip": "Unknown",
            "hmi_mcu_ip": "Unknown",
            "soundmcu_ip": "Unknown",
            "robot3axismcu_ip": "Unknown",

            "imagesendermcu": {
                "command": "IDLE",
                "camera_model": "OV3660",
                "resolution": "SVGA",
                "jpeg_quality": 12,
                "transmission_mode": "UDP",
                "status": "Unknown"
            },

            "soundmcu": {
                "ip": "Unknown",
                "volume": 12,
                "mode": "VAD",
                "sensitivity": 200,
                "status": "Idle",
                "stt_engine": "Whisper-Local",
                "tts_engine": "Edge-TTS",
                "tts_voice": "ko-KR-SunHiNeural",
                "tts_pitch": 0,
                "tts_rate": 0,
                "tts_effect": "None",
                "tts_style_degree": 1.5,
                "openai_api_key": "",
                "azure_api_key": "",
                "azure_region": "koreacentral",
                "gemini_api_key": os.getenv("SOUND_API_KEY", os.getenv("GEMINI_API_KEY", "")),
                "public_personality": "이름은 아로(ARO)입니다. 친절하고 명랑한 인공지능 로봇입니다.",
                "private_instructions": "욕설이나 부적절한 말에 단호하게 거절하세요. 감정 태그를 사용하여 응답하세요."
            },

            "drivemcu_command": None
        }

    def load_config(self):
        """Loads configuration from the JSON file."""
        if os.path.exists(self.config_file):
            try:
                with open(self.config_file, "r", encoding="utf-8") as f:
                    saved_config = json.load(f)
                    self._update_dict(self.config, saved_config)
                    print(f"Loaded config from {self.config_file}")
            except Exception as e:
                print(f"Error loading config: {e}")
                traceback.print_exc()

    def _update_dict(self, target, source):
        for k, v in source.items():
            if isinstance(v, dict) and k in target and isinstance(target[k], dict):
                self._update_dict(target[k], v)
            else:
                target[k] = v

    def save_config(self):
        """Saves configuration to the JSON file and exports important variables to the environment."""
        try:
            with open(self.config_file, "w", encoding="utf-8") as f:
                json.dump(self.config, f, ensure_ascii=False, indent=4)
            os.environ["GEMINI_API_KEY"] = self.config.get("api_key", "")
        except Exception as e:
            print(f"Error saving config: {e}")

    def load_ai_persona(self):
        if os.path.exists(self.persona_file):
            try:
                with open(self.persona_file, "r", encoding="utf-8") as f:
                    persona_text = f.read().strip()
                    if persona_text:
                        self.config["soundmcu"]["public_personality"] = persona_text
                        print(f"Loaded AI Persona from {self.persona_file}")
            except Exception as e:
                print(f"Error loading AI persona: {e}")

    def save_ai_persona(self, content):
        try:
            with open(self.persona_file, "w", encoding="utf-8") as f:
                f.write(content)
            self.config["soundmcu"]["public_personality"] = content.strip()
            self.save_config()
            return True
        except Exception as e:
            print(f"Error saving AI persona: {e}")
            return False

# Create a singleton instance to be used across modules
config_manager = ConfigManager()
config = config_manager.config
