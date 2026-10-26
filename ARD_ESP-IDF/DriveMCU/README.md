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

## 연속 조그 (2026-10-26)
- `j<x>,<y>,<z>`: 모터별 목표 속도, 각 축 최대 속도(`mxs`/`mys`, Z는 기본 500 step/s)의 -1000~1000‰. 축 가속도로 부드럽게 바뀝니다.
- 400ms 안에 다시 보내지 않으면 감속 정지합니다(데드맨). `j0,0,0`은 감속 정지입니다. `x`/`stop`은 기존 비상정지입니다.
- 이동 큐·귀환이 동작 중이면 거부합니다(상태 `program:true`). 레일 PID가 켜져 있어도 X/Y 조그는 동작하고, Z(레일) 조그가 0이 아니면 PID를 끕니다(`Rail PID OFF: rail jog`). PID를 켜면 Z 조그만 멈춥니다. 조그 중에는 큐의 다음 이동을 시작하지 않습니다.
- 초당 약 10회 들어오므로 명령 효과음과 시리얼 `Executing:` 출력은 생략합니다.
- TCP `status` JSON에 `jogging`을 추가했습니다. PC는 이 키가 있으면 조그를, 없으면 기존 20스텝 방식을 씁니다.

## 현재 위치를 원점으로 설정 (2026-10-26)
- `zero`: 모터를 움직이지 않고 X/Y/Z 현재 스텝 위치를 0으로 바꿉니다. 이후 `home`은 이 위치로 돌아옵니다.
- 이동·조그·귀환 중이거나 큐가 남아 있으면 `ERR:REJECTED`로 거부합니다. 모든 축이 멈춘 뒤 보냅니다.
- PC 프로그램의 "현재 위치를 원점으로 설정" 버튼이 이 명령을 보냅니다.

## Z축 레일 PID 복귀 (2026-10-26)
- 레일 구동을 개루프 스텝에서 ToF PID(`mzt<mm>` + `mzm1`)로 되돌렸습니다. `sdkconfig`와 Kconfig 기본값에서 ToF와 레일 PID를 켰습니다.
- 레일을 반대쪽에 반전 장착해서 스텝과 ToF 거리의 관계가 바뀌었을 수 있습니다. `mzv<1|-1>`로 부호를 정합니다. `1`은 원 설계(+스텝 → 거리 증가), `-1`은 반전 장착(+스텝 → 거리 감소, 기본값)입니다. PID가 켜져 있으면 거부합니다.
- `mzv`와 `mzs`(Steps/mm)는 NVS(`ard-rail`)에 저장되어 재부팅 후에도 유지됩니다. 값이 바뀔 때만 기록합니다.
- 폭주 보호: PID 오차가 켠 뒤(또는 목표 변경 뒤) 가장 작았던 값보다 6mm 커지면 PID를 끄고 Z를 감속 정지한 뒤 로그를 남깁니다. 방향 부호가 틀렸을 때 레일 끝까지 달려가지 않게 합니다. 시뮬레이션에서 정상 이동 840회 중 오작동 0회, 부호가 틀렸을 때 약 10mm 안에서 멈췄습니다.
- `mzm1`은 진행 중인 조그를 멈춥니다. PID가 켜져 있으면 조그는 계속 거부합니다. `mzm0`은 남은 PID 목표를 감속 정지합니다. `zero`는 PID 중에도 정지 상태면 동작하고 PID는 거리 기준이라 영향이 없습니다.
- `status` JSON에 `tof_ok`, `tof_status`, `rail_target`, `rail_dir`, `rail_settled`(PID 켜짐 + ±2mm 안 + Z 정지)를 추가했습니다. PC 선별 시퀀스와 레일 도구가 사용합니다.
- 실측 보정은 PC 프로그램 구동 보드 탭의 "Z축 레일 측정·레벨링 도구"에서 합니다.

## Z축 안전 한계 (2026-10-27)
- 레일 작동 범위를 ToF 70~120 mm로 제한합니다(기본값, NVS `ard-rail`의 `lim`/`lmin`/`lmax`, 값이 바뀔 때만 기록).
- `mzl<min>,<max>`: 한계 켜기·변경(1~2000 mm, 최대-최소 ≥ 10). `mzl0`: 끄기(센서를 달지 않은 벤치 시험용, 값은 유지). PC는 선별 전에 레일 설정과 함께 `mzl`을 다시 보냅니다.
- 한계 밖 `mzt`는 `ERR:REJECTED`. PID 목표가 범위 밖이면 `mzm1`도 거부합니다.
- 1 kHz 모션 루프의 감시: Z 이동(큐 `mzr`, 조그 `j`, PID)이 한계 쪽으로 가면서 남은 거리 ≤ 정지 여유이면 Z 가속도로 감속 정지합니다. 이미 한계를 넘었으면 즉시 정지합니다. 정지 여유 = 제동 거리(v²/2a) + ToF 지연(0.3 s × 속도) + 1 mm이고, 한계에는 PID 데드밴드 2 mm를 허용합니다(목표 120 mm 도달 가능). 한계에서 멀어지는 이동은 항상 허용합니다. 감속 목표는 한 번 정해 유지합니다(매 틱 `stop()`을 다시 부르면 감속하지 않음, Python 모델로 확인).
- 한계가 켜져 있고 신선한 ToF가 없으면 새 Z 이동(`mzr`, Z 조그)을 거부하고, 진행 중인 Z는 멈춥니다. X/Y는 영향이 없습니다.
- `status`에 `rail_limit_on`, `rail_min`, `rail_max`, `rail_limit_count`(정지·거부 횟수), `rail_limit`(마지막 사유)를 추가했습니다.
- 감시 계산 모델 시험: `python -m unittest discover -s test/model`(컴파일 없음).

## 디스크 속도 하향과 기울기 ±60° 한계 (2026-10-27)
- 원점 오차(오버슛·탈조) 대책입니다. X/Y 기본 속도를 500에서 300 steps/s로, 가속도를 500에서 300 steps/s²로 낮췄습니다. `mxs`/`mys`도 300을 넘지 못합니다(menuconfig `ARD_DISK_MAX_SPEED`, `ARD_DISK_ACCEL`).
- 디스크 기울기 = (X − Y)/2 스텝 × 1.8°/4(4:1 감속)로 계산합니다. ±60°(X − Y ≤ 266스텝 = 59.85°)를 넘지 않습니다. 디스크 회전(X + Y)은 제한하지 않습니다(`ARD_DISK_TILT_LIMIT_DEG`, `ARD_DISK_GEAR_RATIO`).
- 이동(`b`, `df`/`db`/`dr`/`dl`, `mxr`, `myr`): 끝 위치가 한계 밖이면 `ERR:REJECTED`입니다. 이때 실행 중인 이동과 큐에 쌓인 이동까지 모두 더해서 판단합니다. 큐에서 꺼낼 때 다시 검사하고, 거부되면 큐를 비웁니다. 한계 밖에서 수평 쪽으로 돌아오는 이동은 허용합니다.
- 조그(`j`): 한계 2스텝 안쪽에 소프트 월이 있습니다. 상대 축의 정지 위치를 기준으로 목표를 제한하고, 스틱을 반대로 꺾으면 제한된 위치로 먼저 감속합니다. Python 모델로 무작위 스틱 30회를 돌렸을 때 최대 266스텝이었습니다.
- 백스톱: 기울기가 한계를 넘으면서 더 벌어지면 X/Y를 즉시 멈추고 프로그램을 비웁니다. 감속 정지를 쓰면 300 steps/s에서 약 17°를 더 넘어가기 때문입니다.
- `status`에 `tilt_deg`, `tilt_limit_deg`, `tilt_limit_count`, `tilt_limit`(마지막 사유)를 추가했습니다.

## 발견해서 고친 기존 버그
- **`stop` 명령이 정지하지 않음**: 기존 `processCommand`는 `s<값>`(X 속도) 검사를 `stop`보다 먼저 해서 `stop`이 X 최대속도를 0으로 바꿨습니다. 이제 `x`/`stop`이 먼저 처리됩니다.
- **HMI가 Z 보정값을 덮어씀**: HMI의 오픈루프 `STP +50/-50` 버튼이 보내는 `mzs+50`을 기존 펌웨어가 Steps/mm = 50으로 받아들였습니다. 부호가 붙은 `mzs`는 이제 거부하고 로그만 남깁니다. 보정은 `mzs43.48`처럼 부호 없는 값으로만 바뀝니다.
- ToF 값이 200 ms 넘게 갱신되지 않으면 레일 PID가 오래된 값으로 움직이지 않고 Z를 정지합니다.

## ToF 오류값·timeout 복구 (2026-10-26)
- VL53L0X 레지스터는 repeated START를 사용하는 단일 I2C 트랜잭션으로 읽습니다. 수신 오류는 래치하고 초기화 도중 추가 버스 접근을 중단합니다.
- 데이터 준비 상태를 태스크 주기마다 확인합니다. 기존 거리 읽기의 대기 루프는 런타임에서 사용하지 않습니다. 개별 I2C 전송에는 별도 timeout이 있습니다.
- 측정 시간 예산은 50 ms입니다. 12바이트 결과의 원시 장치 상태가 11인 거리만 사용합니다. ST API의 최종 RangeStatus=0과 구분합니다. Pololu 포트에는 ST의 소프트웨어 sigma 추정기가 없으므로 같은 품질 검증을 보장하지 않습니다.
- 기존 5개 이동 평균을 3개 중앙값으로 변경했습니다. 오류 후 필터를 비우고 연속 정상 측정 3개를 기다립니다(`WARMUP`). 고립된 이상치 영향을 줄이지만 절대 거리 정확도를 보장하지 않습니다.
- `I2C_ERROR` 또는 200 ms 데이터 미수신(`TIMEOUT`) 시 값을 무효화하고 2초 재시도 주기로 소프트 리셋·재초기화합니다. 측정 결과 오류는 `RANGE_ERROR`, 범위 오류는 `OUT OF RANGE`입니다.
- 오류·준비 중·200 ms 초과 값은 `tof_ok=false`, 숫자 거리는 기존 숫자 형식과 호환되도록 0으로 송출합니다(실제 0 mm 측정 아님). OLED는 `N/A`를 표시합니다. TCP status와 HTTP 하트비트에 `tof_age_ms`를 추가했습니다.
- 정상 측정이 없으면 PID 진입을 거부합니다. PID 중 측정 오류가 나면 Z를 감속 정지하고 적분을 초기화합니다. 복구 후 정상 측정 3개가 쌓이면 켜져 있던 PID는 다시 제어합니다.
- 이번 수정은 빌드·플래시·실물 검증을 하지 않았습니다. 사용자가 빌드 후 실제 거리와 보고 거리, timeout 로그, 모터 구동 중 I2C 상태를 확인해야 합니다.

## 그대로인 부분과 남은 확인 사항
- 명령 목록과 검증(`CommandValidation.h`), TCP 응답(`ACK:OK`, `ERR:REJECTED`, `ping`, `status` JSON), Wi-Fi 설정 포털(`ard-drive-xxxxxx` AP, `/network`, NVS `ard-network`)은 기존과 같습니다. 기존 `drive-config` 네임스페이스의 Wi-Fi 정보도 한 번 가져옵니다.
- ToF(VL53L0X)와 레일 PID는 2026-10-26부터 기본으로 켜져 있습니다(아래 "Z축 레일 PID 복귀" 참고). 끄려면 `menuconfig`에서 `Enable VL53L0X ToF sensor` 또는 `Allow ToF rail PID`를 끕니다.
- ESP32(클래식)는 ESP-IDF 5에서 칩 온도 센서 드라이버가 없어 하트비트의 `temp`는 보내지 않습니다(PC는 0으로 표시).
- GPS 위치는 마지막 유효 위치가 5초 넘게 갱신되지 않으면 무효로 봅니다(기존 TinyGPS는 한 번 잡히면 계속 유효).
- 설정 AP의 DNS 리다이렉트(캡티브 포털)는 옮기지 않았습니다. `http://192.168.4.1/network`로 접속하세요.
- 실제 보드에서 모터 구동·센서·Wi-Fi 동작은 아직 확인하지 않았습니다. 빌드와 스텝 생성기 시뮬레이션만 확인했습니다.

## 파일
- `main/app_main.cpp` 부팅 순서와 태스크 배치표
- `main/stepper_engine.*` 타이머 인터럽트 스텝 생성 + 가감속 플래너
- `main/motion.*` 이동 큐, 명령 간 지연, 귀환, 레일 PID, 연속 조그
- `main/commands.cpp`, `main/command_servers.cpp` 명령 해석, 시리얼 콘솔, TCP 5002
- `main/telemetry.*` PC 하트비트/로그/A-GPS (HTTP 5000)
- `main/sensors.*`, `main/display.*`, `ssd1306.*`, `bmp280.*`, `aht20.h`, `ard_wire.*` I2C 장치
- `main/gps.*`, `main/buzzer.*`, `main/net/` Wi-Fi·mDNS·설정 포털
- `main/third_party/` VL53L0X(Pololu, MIT 계열), 5x7 글꼴(Adafruit GFX, BSD) 원본 라이선스 포함
