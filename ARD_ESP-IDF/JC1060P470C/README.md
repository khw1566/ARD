# ARD JC1060P470C 통합 펌웨어 (ESP-IDF 5.5.5)

GUITION JC1060P470C (ESP32-P4 + ESP32-C6-MINI-1U, 7" 1024x600 JD9165 MIPI-DSI, GT911 터치) 한 장으로
기존 **HMI_MCU**(CYD 2.8" 터치 화면)와 **DriveMCU**(스테퍼·센서·GPS)를 대체하고,
**ControlMCU** 조이스틱 대신 **BLE 게임패드**로 조작합니다.

> 실물 보드 없이 작성했습니다. 빌드는 통과했지만 화면·터치·Wi-Fi·BLE·모터 동작은 검증하지 않았습니다.
> 아래 "확인 필요" 항목을 첫 부팅 때 반드시 점검하세요.

## 빌드 / 업로드

```powershell
.\build.ps1                     # 처음이면 set-target esp32p4 후 build
.\build.ps1 flash -Port COM9    # P4 업로드 + 모니터
.\build.ps1 menuconfig          # "ARD JC1060P470C" 메뉴: 패널 V1/V2, 핀, ToF, 게임패드
```

2026-10-17: `idf.py set-target esp32p4` + `idf.py build` 통과, 경고 0.
앱 1,745,488 bytes, 6 MB factory 파티션의 72% 여유.

**C6 보조 펌웨어**: ESP-Hosted 2.12.2 slave가 C6에 있어야 Wi-Fi와 BLE가 동작합니다.
ImageSenderMCU_P4와 같은 버전이므로 `ARD_software/ImageSenderMCU_P4/firmware-output/c6`의 이미지를
그대로 쓸 수 있습니다(그 빌드의 slave 기본값에 BT 컨트롤러 + VHCI가 켜져 있음). C6가 비어 있으면
Wi-Fi/BLE만 실패하고 화면·모터·시리얼 명령은 계속 동작합니다.

## 구조

| 파일 | 역할 | 출처 |
|---|---|---|
| `bsp_display.*`, `panel_init.c` | DSI 패널, GT911, 백라이트 PWM, LVGL 9 태스크 | 신규 |
| `ui.*` | 1024x600 UI: 상단바(항상 보이는 E-STOP), Drive/Robot/Cam/Voice/System 탭, AI 승인 팝업 | HMI_MCU 화면 재구성 |
| `motion.*`, `stepper_engine.*` | 1 kHz 플래너 + 20 kHz 스텝 ISR, 큐, 레일 PID, **조그(데드맨)** | ESP-IDF DriveMCU 포팅본 스냅샷(2026-10-16) + P4 수정 |
| `commands.*`, `command_servers.cpp` | DriveMCU 텍스트 명령(UART0, TCP 5002) 그대로 | 동일 |
| `sensors.*`, `gps.*`, `buzzer.*`, `telemetry.*` | AHT20/BMP280/VL53L0X, NEO-6M, 부저, PC 하트비트(source=DriveMCU) | 동일, OLED 제거 |
| `pc_link.*` | /api/status 폴링, HMI_MCU 하트비트, 다른 보드 명령 전달, 승인, 카메라 JPEG | HMI_MCU 네트워크 태스크 |
| `gamepad.*`, `hid_gamepad_parser.*` | C6 경유 BLE HID 호스트(NimBLE), 리포트 디스크립터 해석 | 신규 |
| `controls.*` | 게임패드 → 조그/명령 매핑 | ControlMCU 대체 |
| `components/ard_net` | SoftAP + STA, /network 설정 페이지, mDNS | HMI_MCU ESP-IDF 포팅본 복사 |

DriveMCU 포팅본은 다른 스레드가 작업 중이라 복사본을 수정했습니다. 그쪽이 바뀌면 이 폴더에 다시 반영해야 합니다.

## 조작 방식: 게임패드

기본값대로 **C6를 통한 BLE HID 게임패드**를 택했습니다. C6가 ESP-Hosted VHCI로 BT 컨트롤러를 제공하고
P4에서 NimBLE + esp_hid가 동작합니다. 별도 하드웨어가 필요 없고 기존 ImageSender C6 펌웨어를 재사용할 수 있어서입니다.
(유선/동글 USB 게임패드는 P4 USB 호스트로 가능하지만 이번 범위에서는 만들지 않았습니다.)

| 입력 | DRIVE 모드 (로컬 모터, PC 경유 없음) | ROBOT 모드 (PC 경유 Robot3Axis) |
|---|---|---|
| 왼쪽 스틱 상/하 | 디스크 전/후진 (X+, Y−) | ry ± |
| 왼쪽 스틱 좌/우 | 디스크 회전 (X, Y 같은 방향) | rx ± |
| 오른쪽 스틱 상/하 | Z 레일 | rz ± |
| LT 누름 | 정밀(25 %) | |
| 십자키 상/하 | 조그 속도 10~100 % | |
| A | | 그리퍼 열기/닫기 |
| **B** | **비상정지** (명령 `x`와 동일) | 동일 |
| START | 비상정지 후 재무장 | 동일 |
| Y | DRIVE/ROBOT 전환 | 동일 |
| X 1초 유지 | X/Y 원점 | 로봇 home |
| LB / RB | 화면 탭 이전/다음 | 동일 |

안전 장치
- 조그는 300 ms 안에 갱신되지 않으면 감속 정지(데드맨). 패드 연결이 끊기면 즉시 0으로.
- BLE 연결 감시 시간 600 ms를 요청해 범위를 벗어난 패드를 빨리 감지합니다(패드가 거부하면 패드 값 사용).
- 비상정지 후에는 START(또는 화면 RE-ARM)를 누르기 전까지 조그가 무시됩니다.
- PC 명령 큐/원점 동작이 진행 중이면 조그는 무시됩니다.
- 저장된 패드가 있으면 그 패드만 재연결합니다. 새 패드는 System 탭의 "Pair new"(60초) 후 연결.

버튼 번호는 Xbox Wireless 컨트롤러(BLE)의 HID 번호 기준입니다. 다른 패드는 System 탭에 눌린 버튼 번호가
보이므로 `controls.cpp`의 `kButton*` 값을 맞추세요. 오른쪽 스틱은 Rx/Ry가 있으면 그것을, 없으면 Z/Rz를 씁니다.

## 기존 대비 개선점

- 보드 2장(+조이스틱 보드) → 1장. 화면에서 누른 Drive 명령이 PC를 거치지 않고 바로 실행됩니다.
- 아날로그 스틱 연속 조그(속도 비례, 가감속, 데드맨). 기존은 고정 스텝 버튼만 있었습니다.
- 7" 화면에서 마지막 촬영 이미지를 P4 하드웨어 JPEG 디코더로 표시(기존 40 KB 제한 → 512 KB).
- HMI의 레일 "STP +50/-50" 버튼이 `mzs+50`을 보내 Steps/mm 보정값을 덮어쓰던 문제: 이제 `mzr±스텝`(레일 이동)을 보냅니다.
- AI 승인 팝업이 PC에서 처리되면 화면 팝업도 자동으로 닫힙니다.
- P4 스텝 ISR이 GPIO 32 이상(두 번째 뱅크)도 지원하도록 수정, Y/Z EN을 X EN과 공유 가능.

## 확인 필요 (실물 도착 후)

| 항목 | 현재 값 | 근거 / 증상 |
|---|---|---|
| 패널 리비전 | V1 | 뒷면 라벨에 "V2"가 있으면 menuconfig에서 V2. 틀리면 흰 화면·세로 노이즈 띠. 초기화 표는 ESPHome `mipi_dsi/models/guition.py` |
| LCD 리셋 핀 | GPIO0 | JC1060P470C_I_W_Y 회로도 인용(ESPHome PR). 일부 설정은 GPIO5 |
| 백라이트 | GPIO23 PWM, 비반전 | 커뮤니티 설정 둘이 반전 여부가 다름 → 밝기가 거꾸로면 `ARD_BACKLIGHT_ACTIVE_LOW` |
| 터치 방향 | 변환 없음 | 좌표가 어긋나면 menuconfig의 swap/mirror. 리셋/INT 핀은 건드리지 않고 0x5D/0x14 자동 탐지 |
| DSI PHY LDO | LDO3 2500 mV | Espressif P4 기준 설계 가정 |
| C6 SDIO | CLK18 CMD19 D0~3=14~17, RST54, **10 MHz** | 커뮤니티 설정과 일치. 빠른 클럭은 화면 줄무늬 보고 있음 |
| P4 칩 리비전 | v1.x용 | 부팅 로그의 rev가 3.x면 sdkconfig 리비전 설정 변경 필요 |
| 모터/부저/GPS 핀 | 확장 헤더: X 32/33, Y 45/46, Z 47/20, EN 1(공유), 부저 2, GPS RX3/TX4 | 사용자 매뉴얼의 2x13 헤더 GPIO 목록 기준. 배선에 맞춰 menuconfig에서 변경 |
| I2C 센서 | 보드 I2C(7/8)에 연결 | 터치·코덱·RTC와 주소 충돌 없음(AHT20 0x38, BMP280 0x76/77, VL53L0X 0x29) |

## 네트워크

- 호스트/AP 이름 `ard-jc1060-xxxxxx`, 비밀번호 `ARD-xxxxxx!`, 설정 `http://192.168.4.1/network` (System 탭에도 표시).
- PC에는 `DriveMCU`(1초)와 `HMI_MCU`(10초) 하트비트를 모두 보내므로 기존 대시보드가 두 보드 모두 온라인으로 봅니다.
- TCP 5002 명령 서버와 UART0 콘솔은 DriveMCU와 같은 프로토콜입니다.

참고: [ESPHome Guition 패널 정의](https://github.com/esphome/esphome/blob/dev/esphome/components/mipi_dsi/models/guition.py),
[HA 포럼 JC1060P470 스레드](https://community.home-assistant.io/t/guition-esp32-p4-jc1060p470/959144),
[V2 패널 PR](https://github.com/agillis/esphome-modular-lvgl-buttons/pull/127),
[CNX 보드 소개](https://www.cnx-software.com/2025/05/06/7-inch-esp32-p4-wireless-touchscreen-display-supports-guition-designer-for-lvgl-arduino-and-esp-idf-programming/).
