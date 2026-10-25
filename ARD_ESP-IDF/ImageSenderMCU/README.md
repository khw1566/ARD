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
- `ARD_NET: Wi-Fi connected: ...`는 공유기 접속과 DHCP 성공입니다. PC 앱 연결은 별도로
  `PC_LINK: PC server connected: <PC주소>:5000`와 실제 보드 하트비트로 확인합니다.
  PC 서버가 실행 중이어야 하며 `/network`의 PC 서버 주소가 현재 PC IPv4와 일치해야 합니다.
- PC HTTP 요청 실패 시 대상 주소·HTTP 결과·현재 Wi-Fi 상태를 최대 10초 간격으로 출력합니다.

## 카메라 버퍼 및 반복 로그

- PSRAM 사용 시 JPEG 프레임마다 `max(256 KiB, 너비 × 높이 / 2)`를 예약합니다.
  SVGA에서는 기존 자동 크기 96,000바이트 대신 262,144바이트이며, 버퍼는 1개입니다.
  실제 JPEG 크기·센서 신호 문제가 해결되었는지는 촬영으로 별도 확인해야 합니다.
- 단발 촬영에 맞춰 `fb_count=1`, `CAMERA_GRAB_WHEN_EMPTY`를 사용합니다. 촬영 프레임을
  전송하는 동안 백그라운드의 연속 촬영을 멈춰 DMA 복사와 Wi-Fi의 메모리 경쟁을 줄입니다.
  [설치 버전의 공식 드라이버 설명](https://components.espressif.com/components/espressif/esp32-camera/versions/2.1.8/readme)도
  2개 이상 버퍼에서 연속 촬영하며 CPU·메모리 부하가 증가함을 설명합니다.
- 카메라 드라이버 작업은 core 1, Wi-Fi는 기존 core 0을 사용합니다. `sdkconfig`와
  새 설정용 `sdkconfig.defaults` 모두 `CONFIG_CAMERA_CORE1=y`입니다.
- XCLK는 OV3660 드라이버가 별도 PLL 분기를 둔 16 MHz로 설정합니다. 센서의 기본 ISP/노출 설정을
  유지하고 DMA 시작 후 애플리케이션에서 추가 노출·샤픈·노이즈 제거 레지스터를 덮어쓰지 않습니다.
- `FB-OVF`는 드라이버가 ROM 출력으로 직접 기록하므로 일반 태그 로그 레벨로 숨겨지지 않습니다.
  `cmake/camera_diagnostics.cmake`가 두 출력 위치만 교체한 소스를 빌드 디렉터리에 생성합니다.
  다운로드한 `managed_components`는 수정하지 않습니다. 첫 오류와 이후 최대 5초마다 누적 횟수를 출력합니다.
  SCCB·초기화·JPEG 시작 마커 오류는 유지합니다.
- 설치된 드라이버 구조가 달라지면 설정 단계에서 명확한 오류로 중단하여 패치 누락을 방지합니다.
- 코드 생성 검사(펌웨어 빌드 없음):
  `python test/test_camera_diagnostics.py --cmake <설치된 cmake.exe 경로>`
- 카메라 래퍼 PC 검사: `python test/run_camera_tests.py`. 실제 `main/camera.c`를 가짜 SDK로
  실행해 단일 버퍼 설정, 오래된 프레임 반환, 새 프레임의 전송 중 소유권, 재시도·초기화 실패를 검사합니다.
  Wi-Fi 테스트와 같은 `shared/test/host_crt`와 설치된 Unity Clang을 사용하며,
  `ARD_TEST_CLANG_BIN`으로 다른 Clang/lld 경로를 지정할 수 있습니다.
- 2026-10-25 제공된 깨진 사진은 PC 원본 JPEG에서도 재현되었고, Pillow 및 TJpgDec 모두 끝까지
  디코딩했습니다. JPEG 마커/디코딩 성공이나 전송 CRC 일치만으로 촬영 화질을 보장할 수 없습니다.
  위 수정은 캡처 부하를 줄이는 조치이며, 새 펌웨어에서 같은 SVGA 장면을 반복 촬영해 확인해야 합니다.

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
