# ImageSenderMCU (ESP-IDF 5.5)
ESP32-S3 N16R8 카메라 보드 펌웨어입니다. `ARD_software/ImageSenderMCU`(Arduino/PlatformIO)를 대체합니다. 기존 코드는 수정하지 않았습니다.

## 빌드와 업로드
- 빌드: PowerShell에서 `.\build.ps1`
- 업로드+모니터: `.\build.ps1 flash COM13`
- ESP-IDF 위치는 `C:\esp\v5.5.5\esp-idf`, 도구는 `C:\Users\work\.espressif`를 사용합니다 (ImageSenderMCU_P4와 같은 설치).
- 처음 빌드할 때 `esp32-camera`, `mdns` 컴포넌트를 내려받습니다.

## 시리얼 명령 (115200, UART 포트)
- `c` 촬영 후 전송, `w` 네트워크 설정 안내 출력, `s` 전송 통계 출력

## 네트워크
- 기존과 같은 `ard-image-xxxxxx` 설정 AP와 `/network` 페이지를 제공합니다. NVS `ard-network/settings`를 그대로 읽으므로 기존 보드는 Wi-Fi를 다시 입력하지 않아도 됩니다.
- 포트: HTTP 5000(설정 폴링·하트비트·로그·TCP 모드 업로드), UDP 5001(이미지), TCP 5003(즉시 촬영 명령)

## 이미지 전송 프로토콜 v2 (UDP 5001)
- 정의: `components/ard_proto/include/ard_img_proto.h`, PC 측은 `ARD_MAIN/backend/network/image_protocol.py`
- DATA 헤더 20바이트: magic `AI`, 버전 2, 타입, frame_id, chunk_idx, chunk_count, payload_len, total_len, frame CRC32. 청크는 1400바이트 고정입니다.
- 32청크마다 POLL을 보내고 PC가 수신 비트맵을 담은 STATUS로 답합니다. 빠진 청크만 다시 보냅니다(선택적 재전송). 32청크 창이 흐름제어 역할도 합니다.
- PC는 모든 청크와 CRC32가 맞을 때만 이미지를 분석으로 넘기고 COMPLETE를 보냅니다. CRC가 틀리면 전체를 한 번 다시 받습니다.
- PC가 응답하지 않으면 ESP32는 실패로 기록하고 PC 로그에 이유를 남깁니다.

## 모듈 인덱스
- `main/main.c`: 초기화, 촬영 작업(카메라 소유), 시리얼 명령
- `main/camera.c`: 카메라 초기화, 해상도 변경 시 재초기화, 품질 즉시 반영, 오래된 프레임 버리고 새 프레임 획득
- `main/img_tx.c`: 프로토콜 v2 송신
- `main/pc_link.c`: 설정 폴링(3초), 하트비트(10초), 로그, TCP 모드 업로드
- `main/cmd_server.c`: TCP 5003 `c`/`capture`/`ping`
- `main/app_config.h`, `main/board_pins.h`: 포트·주기·전송 파라미터, 카메라 핀
- `components/ard_net`: Wi-Fi AP+STA, mDNS, 설정 페이지 (공용 `network_settings.h` 등 포함)
- `components/ard_proto`: 프로토콜 헤더와 CRC32
