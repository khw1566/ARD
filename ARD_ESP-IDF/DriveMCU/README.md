# ARD DriveMCU (ESP-IDF 5.5)

기존 `ARD_software/DriveMCU` (PlatformIO + Arduino) 펌웨어를 ESP-IDF 5.5로 다시 작성한 버전입니다.
보드·배선·명령 프로토콜(TCP 5002, HTTP 5000 하트비트/로그)은 그대로라서 PC 프로그램과 HMI는 수정 없이 붙습니다.
기존 코드는 수정하지 않았습니다.

## 빌드 / 업로드
```powershell
.\build.ps1                     # idf.py build
.\build.ps1 flash-monitor -Port COM5
.\build.ps1 menuconfig          # ARD DriveMCU 메뉴: ToF 사용, 기본 PC 주소
```
ESP-IDF는 `C:\esp\v5.5.5\esp-idf`, 도구는 `C:\Users\work\.espressif`를 사용합니다 (P4 프로젝트와 동일).
mDNS 컴포넌트는 첫 빌드 때 `managed_components/`로 내려받습니다.

스텝 생성기 PC 시뮬레이션 테스트: `test\host\run_tests.cmd` (MSVC).

## 바뀐 점
| 항목 | 기존 (PlatformIO) | 신규 (ESP-IDF) |
| --- | --- | --- |
| 스텝 펄스 | 1 ms마다 `AccelStepper::run()` 폴링 → 축당 최대 약 1000 step/s, 태스크 지연이 곧 펄스 지터 | GPTimer 20 kHz 인터럽트(코어 1, IRAM)가 펄스 생성, 최대 10000 step/s. 1 kHz 플래너가 사다리꼴 가감속 계산, 목표 스텝에서 정확히 정지 |
| 스케줄러 | `loop()` + 여러 태스크가 `millis()` 폴링, 모터 태스크가 뮤텍스를 계속 점유 | 모션 1 kHz `vTaskDelayUntil` 고정주기, 센서 100 Hz 고정주기, 나머지는 큐/블로킹 대기. 네트워크는 코어 0, 모션은 코어 1 전용 |
| 비상정지 | 모터 뮤텍스를 얻을 때까지 대기 | 인터럽트 쪽 상태를 즉시 0으로 만들고 드라이버 EN 해제 후 큐 정리. STOP은 다른 명령 처리 중이어도 바로 실행 |
| 수동조작 | SW1/SW2 버튼, 가변저항, 수동 메뉴·OLED 화면·메뉴 효과음 | 제거 (GPIO13/14/34 미사용) |
| I2C | `loop()`에서 OLED 전체 전송(약 90 ms)이 ToF 주기를 밀어냄 | 센서 태스크가 버스를 단독 소유, OLED는 10 ms마다 한 페이지(128바이트)씩 나눠 전송 |
| 감시 | 없음 | 태스크 워치독(motion/sensors/tcp_cmd, 5 s), 30초마다 힙·스택 여유 출력 |
| 귀환 시간 | `last_homing_ms`가 항상 0 | `home` 완료 시간 측정해 하트비트·ping에 보고 |

## 발견해서 고친 기존 버그
- **`stop` 명령이 정지하지 않음**: 기존 `processCommand`는 `s<값>`(X 속도) 검사를 `stop`보다 먼저 해서 `stop`이 X 최대속도를 0으로 바꿨습니다. 이제 `x`/`stop`이 먼저 처리됩니다.
- **HMI가 Z 보정값을 덮어씀**: HMI의 오픈루프 `STP +50/-50` 버튼이 보내는 `mzs+50`을 기존 펌웨어가 Steps/mm = 50으로 받아들였습니다. 부호가 붙은 `mzs`는 이제 거부하고 로그만 남깁니다. 보정은 `mzs43.48`처럼 부호 없는 값으로만 바뀝니다.
- ToF 값이 200 ms 넘게 갱신되지 않으면 레일 PID가 오래된 값으로 움직이지 않고 Z를 정지합니다.

## 그대로인 부분과 남은 확인 사항
- 명령 목록과 검증(`CommandValidation.h`), TCP 응답(`ACK:OK`, `ERR:REJECTED`, `ping`, `status` JSON), Wi-Fi 설정 포털(`ard-drive-xxxxxx` AP, `/network`, NVS `ard-network`)은 기존과 같습니다. 기존 `drive-config` 네임스페이스의 Wi-Fi 정보도 한 번 가져옵니다.
- ToF 레일 PID는 결정에 따라 막아두었습니다. `mzm1`은 항상 거부되고, `mzt/mzp/mzi/mzd` 값만 저장됩니다. 다시 쓰려면 `menuconfig`에서 ToF와 `Allow ToF rail PID`를 둘 다 켜야 합니다.
- ToF(VL53L0X)는 기존처럼 기본 꺼짐입니다. `menuconfig`에서 켜면 Pololu 라이브러리 포팅본으로 거리만 읽어 보고합니다.
- ESP32(클래식)는 ESP-IDF 5에서 칩 온도 센서 드라이버가 없어 하트비트의 `temp`는 보내지 않습니다(PC는 0으로 표시).
- GPS 위치는 마지막 유효 위치가 5초 넘게 갱신되지 않으면 무효로 봅니다(기존 TinyGPS는 한 번 잡히면 계속 유효).
- 설정 AP의 DNS 리다이렉트(캡티브 포털)는 옮기지 않았습니다. `http://192.168.4.1/network`로 접속하세요.
- 실제 보드에서 모터 구동·센서·Wi-Fi 동작은 아직 확인하지 않았습니다. 빌드와 스텝 생성기 시뮬레이션만 확인했습니다.

## 파일
- `main/app_main.cpp` 부팅 순서와 태스크 배치표
- `main/stepper_engine.*` 타이머 인터럽트 스텝 생성 + 가감속 플래너
- `main/motion.*` 이동 큐, 명령 간 지연, 귀환, 레일 PID
- `main/commands.cpp`, `main/command_servers.cpp` 명령 해석, 시리얼 콘솔, TCP 5002
- `main/telemetry.*` PC 하트비트/로그/A-GPS (HTTP 5000)
- `main/sensors.*`, `main/display.*`, `ssd1306.*`, `bmp280.*`, `aht20.h`, `ard_wire.*` I2C 장치
- `main/gps.*`, `main/buzzer.*`, `main/net/` Wi-Fi·mDNS·설정 포털
- `main/third_party/` VL53L0X(Pololu, MIT 계열), 5x7 글꼴(Adafruit GFX, BSD) 원본 라이선스 포함
