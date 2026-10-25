# ARD JC1060P470C 통합 펌웨어 (ESP-IDF 5.5.5)

GUITION JC1060P470C (ESP32-P4 + ESP32-C6-MINI-1U, 7" 1024x600 JD9165 MIPI-DSI, GT911 터치) 한 장으로
기존 **HMI_MCU** 화면과 **ControlMCU**의 조작·음성 역할을 전환해 사용합니다.
**DriveMCU는 기존 독립 보드 그대로 유지**하며 PC와 JC1060 화면·BLE 게임패드 양쪽에서 PC 서버를 거쳐 제어합니다.
JC1060에는 모터·센서·GPS 등 추가 보드를 연결하지 않습니다.
내장 **ES8311 마이크·스피커**로 음성을 입력하고, 기존 PC의 음성 인식·응답·안내 음성을 재생합니다.

> 초기 버전은 실물 없이 작성했습니다. 2026-10-24 V2 패널 수정과 터치 설정 UI에 대해 사용자가 정상 동작을 확인했습니다.
> 이후 원격 Wi-Fi 기록/QR 기능과 병합한 버전은 호스트 회귀 검사와 별도로 실물 재확인이 필요합니다.

## 빌드 / 업로드

```powershell
.\build.ps1                     # 처음이면 set-target esp32p4 후 build
.\build.ps1 flash -Port COM9    # P4 업로드 + 모니터
.\build.ps1 menuconfig          # "ARD JC1060P470C" 메뉴: 패널 V1/V2, 핀, ToF, 게임패드
```

2026-10-17: `idf.py set-target esp32p4` + `idf.py build` 통과, 경고 0.
앱 1,745,488 bytes, 6 MB factory 파티션의 72% 여유.

**C6 보조 펌웨어**: ESP-Hosted 2.12.2 slave가 C6에 있어야 Wi-Fi와 BLE가 동작합니다.
구형 소스는 저장소 밖 `C:/Users/work/Desktop/ETC/ARD_software`로 분리했습니다.
ImageSenderMCU_P4와 같은 버전이므로 해당 폴더의 `ImageSenderMCU_P4/firmware-output/c6` 이미지를
그대로 쓸 수 있습니다(그 빌드의 slave 기본값에 BT 컨트롤러 + VHCI가 켜져 있음). C6가 비어 있으면
Wi-Fi/BLE만 실패하고 화면·시리얼 콘솔은 계속 동작하며 원격 구동 명령은 전송할 수 없습니다.

## 내장 마이크·스피커 사용

1. 새 P4 펌웨어와 수정된 `ARD_MAIN`을 함께 사용한다. PC 프로그램은 다시 시작한다.
2. PC **설정 → 일반/하드웨어 → 제어 단말**에서 JC1060을 선택하고 저장한다. 음성 대상도 JC1060으로 자동 선택되며, 필요하면 별도로 ControlMCU를 선택할 수 있다.
   이미 JC1060을 선택해 둔 경우 **음성 입출력 보드**도 JC1060인지 확인하고 저장한다.
3. 보드 **Voice** 탭에서 **SPEAKER TEST**로 출력을 확인한다. **VOL - / VOL +**, 음소거, 재생 중지를 사용할 수 있다.
4. **MIC ON → PTT**를 선택하고 **HOLD TO TALK** 또는 게임패드 **RT**를 누른 동안 말한 뒤 놓는다. **VAD**에서는 발화를 감지해 자동 녹음하며 약 0.75초 침묵 후 전송한다.
5. 마이크 레벨, 녹음·전송·처리·재생 상태와 오류 수를 Voice 탭에서 확인한다. 마이크 ON/OFF와 보드 볼륨은 NVS에 저장한다.

입력·출력은 보드 내장 마이크·스피커이며, 음성 인식·AI·TTS는 기존 PC 엔진을 사용한다. 녹음은 기본 15초, 설정상 최대 20초로 제한한다. PC 연결이 끊기거나 다른 음성 보드를 선택하면 녹음·업로드를 중단한다. 스피커 재생과 짧은 잔향 구간에는 녹음을 억제한다. 이는 소프트웨어 반향 억제이며 완전한 AEC는 아니다.

부팅 멜로디와 PC의 카메라·로봇·선별·승인·비상정지 안내를 내장 스피커로 재생한다. 화면의 구동 명령은 PC의 기존 DriveMCU API로 전달하며 PC가 안내한다. 게임패드 조그의 짧은 반복 이동에는 음성을 매번 재생하지 않는다.

**VAD 오동작 방지 (2026-10-25):** 소리가 200 ms 이상 이어져야 녹음을 시작하고, 말소리 구간이 합쳐서 300 ms가 안 되는 녹음은 PC로 보내지 않고 버린다(두드림·클릭 등). 조용할 때의 주변 소음 수준을 약 4초 단위로 따라가며 그 3배와 PC 감도 중 큰 값을 넘어야 말소리로 본다. 스피커 앰프가 꺼질 때의 "툭" 소리로 다시 녹음되지 않도록 앰프가 꺼진 뒤 0.6초 동안 녹음을 막는다. PTT는 사용자가 직접 누르는 것이므로 그대로다. 정책 시험: `test/test_voice_capture.c`.

**명령 구분:** `x`/`stop`은 기존 모터 비상정지이고, `audio_stop`은 소리만 중지한다. PC 음성 제어 경로의 `stop`은 JC1060에서 `audio_stop`으로 변환된다. `audio_status`, `audio_test`, `vol=0..21`, `audio_mic_on/off`, `vad`/`ptt`도 지원한다.

오디오 코덱은 `espressif/esp_codec_dev` **1.2.0**에 고정했다. 설치된 ESP-IDF **5.5.5**에서 컴파일·링크했으며, 실물 마이크·스피커 및 모터·화면 동시 운전 검증은 별도로 필요하다. 자세한 결과는 [2026-10-25 음성 이식 기록](../../results/261025_JC1060_내장음성_이식.md)에 있다.

## 구조

| 파일 | 역할 | 출처 |
|---|---|---|
| `bsp_display.*`, `panel_init.c` | DSI 패널, GT911, 백라이트 PWM, LVGL 9 태스크 | 신규 |
| `ui.*` | 1024x600 UI: 상단바(항상 보이는 E-STOP), Main/Drive/Cam/Voice/System/Settings 탭, AI 승인 팝업 | HMI_MCU 화면 재구성 |
| `ui_main.*` | Main 탭: PC 분류 시퀀스 5단계·디스크 선회·트레이 전도·레일 위치 시각화(PC 확인 위치 기준, 이동 중 애니메이션) | 신규 |
| `onboard_audio.*`, `voice_capture.h` | ES8311 전이중 I2S, VAD/PTT 녹음, 발화 업로드, PCM 안내·미디어 재생, 반향 입력 억제 | ControlMCU 출력 경로 및 온보드 입력 추가 |
| `motion.*`, `stepper_engine.*` | 이전 로컬 구동 소스 참고본. 현재 HMI 빌드에서 제외 | 실행하지 않음 |
| `commands.*`, `command_servers.cpp` | 온보드 음성은 로컬 처리, 구동은 PC로 전달. TCP `status`는 HMI 역할만 보고 | 원격 HMI |
| `sensors.*`, `gps.*`, `buzzer.*`, `telemetry.*` | 이전 참고본. 추가 GPIO 초기화 및 DriveMCU 하트비트 전송 제거 | 현재 빌드에서 제외 |
| `pc_link.*` | /api/status 폴링, HMI_MCU 하트비트, 다른 보드 명령 전달, 승인, 카메라 JPEG | HMI_MCU 네트워크 태스크 |
| `gamepad.*`, `hid_gamepad_parser.*` | C6 경유 BLE HID 호스트(NimBLE), 리포트 디스크립터 해석 | 신규 |
| `controls.*` | 게임패드 → 조그/명령 매핑 | ControlMCU 대체 |
| `components/ard_net` | SoftAP + STA, /network 설정 페이지, mDNS | HMI_MCU ESP-IDF 포팅본 복사 |

DriveMCU의 IP·설정·명령 프로토콜은 단말 전환과 독립적입니다. JC1060은 `source=HMI_MCU, board=JC1060P470C`만 전송합니다. Drive 탭 위치는 PC에서 수락한 명령의 추정 스텝이며 실제 엔코더 피드백이 아닙니다.

## 조작 방식: 게임패드

기본값대로 **C6를 통한 BLE HID 게임패드**를 택했습니다. C6가 ESP-Hosted VHCI로 BT 컨트롤러를 제공하고
P4에서 NimBLE + esp_hid가 동작합니다. 별도 하드웨어가 필요 없고 기존 ImageSender C6 펌웨어를 재사용할 수 있어서입니다.
(유선/동글 USB 게임패드는 P4 USB 호스트로 가능하지만 이번 범위에서는 만들지 않았습니다.)

| 입력 | DRIVE 모드 (PC 경유 기존 DriveMCU) | ROBOT 모드 (PC 경유 Robot3Axis) |
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
- 조그는 축당 최대 20스텝의 기존 상대 이동 명령으로 전달합니다. PC 대기 입력은 300ms 뒤 만료하고 한 묶음만 전송 중 상태로 둡니다. DriveMCU의 실제 `status.busy`를 확인해 이동 중에는 새 펄스를 쌓지 않습니다.
- 패드 중립·끊김 시 원격 정지를 요청합니다. 네트워크가 끊기면 이미 보드가 수락한 유한 이동은 완료될 수 있으며, 통신만으로 즉시 정지를 보장하지 않습니다.
- BLE 연결 감시 시간 600 ms를 요청해 범위를 벗어난 패드를 빨리 감지합니다(패드가 거부하면 패드 값 사용).
- 비상정지 후에는 START(또는 화면 RE-ARM)를 누르기 전까지 조그가 무시됩니다.
- PC 수동 명령·선별 시작·보드 설정 변경 시 게임패드 조그를 해제합니다. 다시 START/RE-ARM이 필요합니다. DriveMCU 재부팅이나 통신 실패도 재무장이 필요하며 PID 모드에서는 조그를 거부합니다.
- 저장된 패드가 있으면 그 패드만 재연결합니다. 새 패드는 System 탭의 "Pair new"(60초) 후 연결.

버튼 번호는 Xbox Wireless 컨트롤러(BLE)의 HID 번호 기준입니다. 다른 패드는 System 탭에 눌린 버튼 번호가
보이므로 `controls.cpp`의 `kButton*` 값을 맞추세요. 오른쪽 스틱은 Rx/Ry가 있으면 그것을, 없으면 Z/Rz를 씁니다.

## 기존 대비 개선점

- HMI·조작·음성을 JC1060 내장 기능으로 통합하고 기존 DriveMCU는 보존합니다.
- 아날로그 스틱 크기와 속도 설정에 비례하는 유한 스텝 이동을 PC를 통해 보냅니다.
- 7" 화면에서 마지막 촬영 이미지를 P4 하드웨어 JPEG 디코더로 표시(기존 40 KB 제한 → 512 KB).
- HMI의 레일 "STP +50/-50" 버튼이 `mzs+50`을 보내 Steps/mm 보정값을 덮어쓰던 문제: 이제 `mzr±스텝`(레일 이동)을 보냅니다.
- AI 승인 팝업이 PC에서 처리되면 화면 팝업도 자동으로 닫힙니다.

## 확인 필요 (실물 도착 후)

| 항목 | 현재 값 | 근거 / 증상 |
|---|---|---|
| 패널 리비전 | V2 | 사용자 정상 동작 설정을 기본값에 반영. 구형 V1 패널이면 menuconfig에서 V1 선택. 초기화 표는 ESPHome `mipi_dsi/models/guition.py` |
| LCD 리셋 핀 | GPIO0 | JC1060P470C_I_W_Y 회로도 인용(ESPHome PR). 일부 설정은 GPIO5 |
| 백라이트 | GPIO23 PWM, 비반전 | 커뮤니티 설정 둘이 반전 여부가 다름 → 밝기가 거꾸로면 `ARD_BACKLIGHT_ACTIVE_LOW` |
| 터치 방향 | 변환 없음 | 좌표가 어긋나면 menuconfig의 swap/mirror. 리셋/INT 핀은 건드리지 않고 0x5D/0x14 자동 탐지 |
| DSI PHY LDO | LDO3 2500 mV | Espressif P4 기준 설계 가정 |
| C6 SDIO | CLK18 CMD19 D0~3=14~17, RST54, **10 MHz** | 커뮤니티 설정과 일치. 빠른 클럭은 화면 줄무늬 보고 있음 |
| P4 칩 리비전 | v1.x용 | 부팅 로그의 rev가 3.x면 sdkconfig 리비전 설정 변경 필요 |

## 화면이 보이지만 가운데가 하얗게 뜨는 경우

2026-10-24: ESPHome의 제조사 표 바깥에서 추가되는 `COLMOD=0x55`(RGB565)와
`INVOFF` 명령을 패널 초기화에 보완했습니다. JD9165 컴포넌트 2.0.2는
`bits_per_pixel=16`만으로 이 명령들을 보내지 않습니다. 이 차이가 실물의
중앙 번짐을 해결하는지는 재빌드·업로드 후 확인해야 합니다.

## 화면 출력 방식 (2026-10-25)

부팅 색상 막대·흰색/검은색 진단과 `Use synchronous full-frame UI redraw` 옵션은 제거했습니다.
남아 있던 로컬 `sdkconfig`가 진단 옵션을 다시 켜는 일이 없도록 코드와 메뉴 항목을 모두 삭제했습니다.

LVGL은 DPI 프레임버퍼 2개(각 1,228,800 bytes, PSRAM)에 매 프레임 전체 화면을 직접 그립니다.
표시 중이 아닌 버퍼에 그린 뒤 캐시를 기록하고, 다음 프레임 시작에 버퍼를 교체합니다.
교체가 끝난 것(프레임 완료 인터럽트)을 확인한 뒤에야 LVGL이 이전 버퍼에 다시 그립니다.
표시 중인 버퍼에 CPU로 1.2 MB를 복사하던 이전 방식보다 PSRAM 사용량이 줄고, 찢어짐과
부분 갱신 화면이 생기지 않습니다. 시리얼에 `underrun happens`가 보이면 PSRAM 대역폭 부족,
`LCD frame swap timeout`이 보이면 DSI 프레임 완료 인터럽트 문제를 의심합니다.
프레임버퍼가 정상인데도 사라지지 않는 흐린 잔상은 패널 자체의 화상 잔류일 수 있으므로
화면을 몇 분 꺼 두었다가 다시 확인해 구분합니다.

## 터치 설정 화면

메인 화면의 **Settings** 탭 안에 **Display / Network / Server** 하위 탭을 추가했습니다.
새 체크아웃은 V2 패널을 기본값으로 사용합니다. 기존 `sdkconfig`의 명시적인 선택은 유지합니다.
기존 게임패드·상태 정보는 **System** 탭에 있습니다. 이후 설정 분류는
`main/ui_settings.cpp`의 하위 탭 구성에 추가할 수 있습니다.

- **Display**: 밝기 5~100% 슬라이더와 10/25/50/80/100% 바로 선택.
  밝기는 즉시 반영하고, 손을 떼면 작업 태스크가 `ard-display/brightness`에 저장합니다.
  재부팅 시 복원하며, 저장 실패는 화면에 표시합니다. Wi-Fi 작업 중에는 저장이 늦어질 수 있습니다.
- **Network**: **Search Wi-Fi → 목록 선택 → Password 입력 → 키보드 확인 → Connect**.
  최대 20개 검색 결과에서 같은 SSID/보안 여부 중복을 제거하고 신호 세기·암호 필요 여부를 표시합니다.
  숨겨진 SSID는 직접 입력할 수 있습니다. 현재 키보드는 기존 UI와 같은 영문/숫자/기호 입력입니다.
  비밀번호는 기본 숨김이며 **Show password**로 확인할 수 있습니다.
- 연결은 보드를 재시작하지 않고 진행하며 IP를 받은 뒤 기존 `ard-network/settings` NVS 형식으로
  저장합니다. 실패하면 이전 설정으로 복구를 시도합니다. 설정 AP와 PC 서버 주소는 유지합니다.
- 검색·연결·NVS 저장은 별도 `ui_settings` 태스크에서 처리합니다. 자동 재연결 및 웹 설정 저장과
  충돌하지 않도록 Wi-Fi 작업을 직렬화했습니다. UI에서 읽는 RSSI도 작업 태스크에서 갱신한 값입니다.
- 원격의 8개 성공 Wi-Fi 기록과 Setup QR도 유지합니다. 터치에서 연결한 SSID를 공통 작업 코드의
  활성 연결로 전달하여, 이전 SSID에 새 비밀번호가 잘못 저장되지 않도록 통합했습니다.
- 이 보드의 C6를 통한 2.4 GHz Wi-Fi를 사용합니다. 실제 검색·연결·저장 후 재부팅 검증은 실물에서
  진행해야 하며, 이전 로그의 ESP-Hosted 호스트/C6 버전 불일치는 별도 확인 항목입니다.

실물 확인: 밝기 변경 후 저장 메시지/재부팅 복원, 암호 있는 AP 연결, 암호 없는 AP 연결,
잘못된 암호에서 이전 설정 유지, 연결된 상태에서 재검색, 키보드 닫기/탭 전환을 확인하세요.

### 2026-10-25 Wi-Fi 연결 수정

- 자동 연결에서 공유기 연결·인증 대기(12초)와 DHCP 대기(연결 후 최대 30초)를 분리했습니다.
- 수동 연결 대기 중에도 연결 관리 코드를 실행합니다. 연결 해제 시 선택한 SSID로 최대 두 번 더 시도하며, 25초 대기가 끝나기 전 다른 저장 SSID로 전환하지 않습니다.
- 수동 연결·복구·검색 중 기존 연결을 끊을 때 고정 200ms 대신 실제 해제 이벤트를 최대 2.5초 기다립니다. 이벤트가 없으면 새 설정을 덮어쓰지 않고 오류를 반환합니다.
- 자동·수동 연결에 SAE의 양쪽 방식(H2E 포함)과 선택적 PMF를 명시했습니다. 인증 실패를 포함한 연결 해제의 reason 코드와 DHCP 대기 시작을 시리얼에 기록합니다.
- PC의 Wi-Fi 관리 코드 20개 테스트와 웹 설정 회귀 검사를 통과했습니다. 펌웨어 재빌드·업로드 및 실물 공유기 연결은 별도 확인이 필요합니다.

## 한국어 화면

**설정 → 화면(Display) → 언어**에서 English / 한국어를 고르면 저장 후 보드가 다시 시작해 그 언어로 화면을 만든다.

- 번역표는 `main/ui_text.cpp`이다. `lv_label_set_text(_fmt)`와 입력칸 안내 문구를 링크 단계에서 감싸(`main/CMakeLists.txt`의 `--wrap`) 표의 영어 문장과 정확히 같은 화면 문구만 바꾼다. 같은 영어 단어를 비교하는 로직은 영향을 받지 않는다. `%s` 인자로 들어가는 단어는 호출부에서 `ui_tr()`로 바꾼다.
- 글꼴은 Montserrat(배치·영문) 뒤에 나눔고딕 한글을 대체 글꼴로 연결한 `ard_font_16/20/28`이다. 한글 비트맵은 번역에 쓰인 글자만 담은 `main/ard_font_hangul.c`(현재 274자 × 3크기, 약 232 KB)이다.
- 화면 문구나 번역을 바꾼 뒤에는 `python tools/check_ui_text.py`(표의 영어 키가 소스와 맞는지)와 `python tools/gen_hangul_font.py`(새 글자 반영, Pillow 필요)를 실행한다.
- 나눔고딕은 SIL OFL 1.1이며 원본과 라이선스를 `third_party/nanum-gothic/`에 둔다. 생성 글꼴은 예약 이름을 쓰지 않는다.

## 웹 네트워크 설정

- 호스트/AP 이름 `ard-jc1060-xxxxxx`, 비밀번호 `ARD-xxxxxx!`, 설정 `http://192.168.4.1/network` (System 탭에도 표시).
- PC에는 `DriveMCU`(1초)와 `HMI_MCU`(10초) 하트비트를 모두 보내므로 기존 대시보드가 두 보드 모두 온라인으로 봅니다.
- TCP 5002 명령 서버와 UART0 콘솔은 DriveMCU와 같은 프로토콜입니다.

참고: [ESPHome Guition 패널 정의](https://github.com/esphome/esphome/blob/dev/esphome/components/mipi_dsi/models/guition.py),
[HA 포럼 JC1060P470 스레드](https://community.home-assistant.io/t/guition-esp32-p4-jc1060p470/959144),
[V2 패널 PR](https://github.com/agillis/esphome-modular-lvgl-buttons/pull/127),
[CNX 보드 소개](https://www.cnx-software.com/2025/05/06/7-inch-esp32-p4-wireless-touchscreen-display-supports-guition-designer-for-lvgl-arduino-and-esp-idf-programming/).
