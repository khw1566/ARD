- 작성일: 2026-10-16
- 작성자: Claude
- 대상: 구 SoundMCU(ESP32-C3 SuperMini) 잔존 항목 → ESP32-S3 ControlMCU로 대체
- 상태: 신형 코드(ARD_MAIN, ARD_ESP-IDF\HMI_MCU) 수정 완료, 서버 스모크 테스트 통과. HMI 펌웨어는 문자열만 바꿔 빌드는 HMI 스레드 빌드 때 함께 확인 필요. 레거시 폴더는 목록만 작성(삭제 안 함).

## 1. 결론

- SoundMCU 전용 펌웨어 폴더는 어디에도 없습니다. 음성 입출력(I2S 마이크·스피커, UDP 5005, TCP 5002)은 이미 ESP32-S3 ControlMCU가 하고 있었고, 서버가 ControlMCU 하트비트를 받으면 가상의 "SoundMCU"도 온라인으로 표시하던 이름만 남은 상태였습니다.
- 그래서 "SoundMCU"라는 장치·설정·API를 없애고, 음성 설정과 상태를 ControlMCU 아래로 합쳤습니다.

## 2. 변경 내역 (신형 코드)

### ARD_MAIN/backend/app.py (이미지 스레드 소유, 직전 재확인 후 수정)
| 항목 | 전 | 후 |
| :--- | :--- | :--- |
| 음성 설정 | `CONFIG["soundmcu"]` | `CONFIG["controlmcu"]` (ip, volume, VAD/PTT, TTS/STT 설정) |
| 최상위 IP 키 | `soundmcu_ip`, `controlmcu_ip` 중복 | 삭제, `CONFIG["controlmcu"]["ip"]` 하나로 |
| 장치 상태 | `device_stats["SoundMCU"]`, `last_seen/last_connection_state["soundmcu"]` | 삭제, 음성 상태는 `device_stats["ControlMCU"]["status"]` |
| `/api/status` | `soundmcu` 블록 | 삭제, `controlmcu.voice_mode`, `controlmcu.status` 추가 |
| `/api/hmi/info` | `"sound": SoundMCU 상태` | `"control": ControlMCU 상태` |
| 하트비트 | source에 sound/voice가 있으면 `soundmcu` | `controlmcu`로 매핑, 보드가 status를 안 보내면 음성 상태를 덮어쓰지 않음 |
| 라우트 | `/api/soundmcu/{chat,stop,status,settings,voices,test_emotion_tts,config}` | `/api/controlmcu/voice/...` |
| 신규 라우트 | 없음 (HMI가 404) | `/api/controlmcu/command`: `vad`/`ptt`는 음성 모드 전환, 그 외는 ControlMCU TCP 5002로 전달 |
| 호환 라우트 | | `/api/soundmcu/stop` 유지 (현재 보드에 올라간 PlatformIO ControlMCU 펌웨어가 호출, ESP-IDF 포팅 후 제거) |
| 함수명 | `update_soundmcu_status` 등 | `update_voice_status`, `get_controlmcu_voice_status` 등 |

함께 고친 버그
- `CONFIG["soundmcu"]`에 `volume` 키가 없어 볼륨을 한 번 바꾸기 전까지 `save_config_to_env()`가 KeyError로 조용히 실패했습니다(.env 저장 안 됨). `volume` 기본값을 .env에서 읽도록 추가했습니다.
- 설정 저장 시 화면에 없는 `soundmcu_api_key` 입력칸 값("")이 음성용 Gemini 키를 빈 값으로 덮어썼습니다. 값이 있을 때만 바꾸도록 했습니다.

### ARD_MAIN/backend/.env
- `SOUND_API_KEY` → `CONTROLMCU_API_KEY`, `SOUNDMCU_VOLUME` → `CONTROLMCU_VOLUME`, 중복 `SOUNDMCU_IP` 삭제(`CONTROLMCU_IP`와 같은 값). 코드는 옛 이름도 대체값으로 읽습니다.

### ARD_MAIN/backend/ai_persona.txt
- "컨트롤MCU(SoundMCU)" → "컨트롤MCU(ESP32-S3)".

### ARD_MAIN/frontend
- `audio-voice-controller.js`, `main.js`, `media-controller.js`: fetch 경로 `/api/controlmcu/voice/...`, `updateSoundMCUUI`/`saveSoundMCUConfig` → `updateControlMCUVoiceUI`/`saveControlMCUVoiceConfig`, `soundmcu_api_key` → `controlmcu_api_key`.
- `templates/components/sidebar.html`: `config.soundmcu.*` → `config.controlmcu.*`, `config.controlmcu_ip` → `config.controlmcu.ip`.
- `hmi-simulator.js` (HMI 스레드 소유): 탭 "Sound" → "Ctrl", 장치명 "ControlMCU", 상태 키 `controlmcu`(`voice_mode`, `status`), 명령 경로 `/api/controlmcu/command`.

### ARD_ESP-IDF/HMI_MCU (HMI 스레드 소유, 문자열만 최소 수정)
- `main/hmi_net.c`: 상태 키 `soundmcu` → `controlmcu`, 모드 필드 `voice_mode`, 명령 경로 `/api/controlmcu/command`.
- `main/hmi_ui.c`: 탭 이름 "Sound" → "Ctrl", 장치명 "SoundMCU" → "ControlMCU", 주석 1줄.
- `main/hmi_model.h`: 주석 1줄.
- `README.md`: 404 메모를 새 경로 설명으로 교체.
- 내부 식별자(`DEV_SOUND`, `TARGET_SOUND`, `sound_mode`, `info_sound`)는 HMI 스레드가 같은 파일을 고치는 중이라 그대로 두었습니다.

### 검증
- `python -m py_compile app.py` 통과, `node --check` 수정한 JS 4개 통과.
- Flask test client: `/api/controlmcu/voice/status` 200, `/api/controlmcu/command {ptt}` 200, `/api/status`에 `soundmcu` 없음·`controlmcu.voice_mode` 있음, ControlMCU 하트비트 후 음성 IP 갱신, `/` 렌더링 200.
- `tests/` 28개 중 27개 통과. 실패 1개(`test_sorting_integration`의 `trigger_image_capture` NameError)는 이미지 전송 변경 쪽 테스트로 이번 수정과 무관합니다.
- 실기기 확인 전입니다.

## 3. 남은 항목 (삭제·추가 결정 필요)

### 신형 코드 안의 미사용 모듈 (ARD_MAIN/backend)
`app.py`가 import하지 않는 분리 리팩터링 잔재로, `soundmcu` 이름이 그대로 남아 있습니다. 실행에는 영향 없습니다.
- `api/audio_routes.py`, `api/control_routes.py`, `api/system_routes.py`
- `audio/music_player.py`, `audio/stt_engine.py`, `audio/tts_engine.py`, `audio/voice_queue.py`
- `ai/llm_manager.py`, `config/config_manager.py`, `core/state_manager.py`
- (참고) `network/tcp_client.py`, `network/sse_manager.py`도 위 모듈만 사용

### 레거시 폴더 (ARD_software, 읽기 전용, 삭제 안 함)
- `ControlMCU/src/main.cpp`: `/api/soundmcu/stop` 호출(서버 호환 라우트로 유지 중). ControlMCU는 아직 ESP-IDF 포팅이 없습니다.
- `HMI_MCU/src/main.cpp`, `HMI_MCU/gen_main.py`: 구 HMI의 SoundMCU 탭.
- `pc_center/` 전체(2.5 GB): ARD_MAIN의 원본 복사본, 같은 soundmcu 코드.
- 문서: `ARD_Project_Summary_KR.md`(2.3절 ESP32-C3 SoundMCU 하드웨어), `Total_Document.txt`, `results/260525_진행상황보고서.md`, `results/260910_코드인덱싱.md`.
- 다이어그램: `D2/ARD_System_Architecture.d2`, `D2/HMI_MCU.d2/.svg`, `D2/PC Center.d2/.svg`, `D2/PC_Center_Frontend.d2/.svg`, `D2/STT n TTS Logic.d2`.
- 루트 `results/intermediate_extracted.txt`: 추출 텍스트에 SoundMCU 언급.

### 별도로 발견한 점
- `app.py` CONFIG에 Azure Speech 키 기본값이 소스에 하드코딩되어 있습니다. `.env`로만 읽도록 바꾸는 게 안전합니다.
