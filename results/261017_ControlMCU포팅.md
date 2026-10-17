- 작성일: 2026-10-17
- 작성자: Claude
- 대상: ControlMCU(ESP32-S3 N16R8) PlatformIO 펌웨어 → ESP-IDF 5.5 포팅
- 상태: `ARD_ESP-IDF\ControlMCU` 빌드 통과(ESP-IDF 5.5.5, 경고 0, 앱 0xFD0D0 바이트, 파티션 33% 여유). 실기기 플래시 전.

## 1. 구조

| 파일 | 역할 |
| :--- | :--- |
| `main/inputs.c` | 3단 모드 스위치, 음성 버튼, 조이스틱 (5 ms 스캔, 20 ms 디바운스) |
| `main/led.c` | WS2812 모드 색(N 주황, A 파랑, B 초록, Wi-Fi 끊김 빨강), 버튼 LED |
| `main/audio.c` | UDP 5005 PCM 수신 → 지터 버퍼 → I2S(MAX98357A), 볼륨 NVS 저장, 부팅 멜로디 |
| `main/pc_link.c` | `/api/control/update`, `/api/heartbeat`, `/api/controlmcu/voice/stop` |
| `main/cmd_server.c` | TCP 5002 명령 |
| `components/ard_net` | HMI/JC1060 포팅본과 같은 Wi-Fi·mDNS·설정 페이지 컴포넌트 |

핀, 포트, JSON 필드는 기존 펌웨어와 같습니다. 상세 표는 `ARD_ESP-IDF\ControlMCU\README.md`.

## 2. 개선 사항

1. 보드 자체 인터넷 라디오 제거: PC가 이미 ffmpeg로 라디오를 받아 UDP 5005로 PCM을 보내고 있어 중복이었습니다. ESP32-audioI2S 라이브러리와 하드코딩된 방송국 10개가 빠졌고, TCP `radio=`는 PC에서 재생하라는 오류를 돌려줍니다. PC 코드는 `radio=`를 보내지 않습니다.
2. HMI Ctrl 탭 `MUTE`/`PLAY` 동작: 기존 펌웨어는 이 명령을 무시했습니다. 음소거 시 앰프 SD도 꺼서 대기 잡음이 사라집니다.
3. 지터 버퍼 96 ms와 언더런 시 무음: 패킷이 늦으면 딸깍거리던 문제를 줄입니다.
4. `stop` 후 0.5초간 도착하는 패킷 폐기: 정지 직후 말이 잠깐 이어지던 현상 방지.
5. 볼륨 NVS 저장: 재부팅마다 12로 돌아가던 문제.
6. 입력 디바운스, 모드 전환 즉시 전송(기존 최대 2초 지연), 서브 모드를 래칭 버튼 상태와 일치.
7. HTTP 클라이언트 재사용, 조이스틱 유지 중 하트비트 중복 전송 제거(기존 120 ms마다 요청 2개).
8. TCP 5002 무응답 클라이언트 5초 후 종료, 시리얼 정지 경로를 `/api/controlmcu/voice/stop`으로 변경.
9. 하트비트에 `firmware:"esp-idf"`, `volume`, `muted`, 오디오 패킷·언더런 카운터 추가(서버는 모르는 키를 무시).

## 3. 서버(ARD_MAIN) 쪽 확인 필요

- 볼륨 이중 적용: PC가 TTS·라디오·미러링 PCM에 `volume/21`을 곱해 보내고, 보드도 같은 볼륨을 다시 곱합니다(기존 펌웨어도 동일). 볼륨 12면 실제 크기는 약 33%입니다. 보드가 볼륨을 받아 저장하므로 PC 쪽 곱셈을 빼는 것을 권합니다. `app.py`는 이미지 스레드 소유라 손대지 않았습니다.
- `/api/soundmcu/stop` 호환 라우트: 새 펌웨어를 올리면 호출하지 않으니 제거할 수 있습니다.
- 서버 `audio_receive_thread`는 보드 마이크 PCM을 기대하지만 기존·신규 펌웨어 모두 마이크가 없습니다(PC 마이크 사용).

## 4. 하드웨어 확인 필요

- N16R8 모듈에서 GPIO35/36은 옥탈 PSRAM 버스라 PSRAM을 끈 상태로 빌드했습니다(기존 Arduino 빌드도 꺼져 있었음). PSRAM을 켜면 모드 스위치가 동작하지 않습니다.
- 플래시 16 MB QIO로 설정했습니다. 실제 모듈이 다르면 `sdkconfig.defaults`를 바꿔야 합니다.
