# ARD HMI MCU (ESP-IDF 5.5)

`ARD_software/HMI_MCU`(PlatformIO/Arduino, TFT_eSPI)를 ESP-IDF 5.5로 옮긴 펌웨어입니다.
보드: ESP32-2432S028 (CYD 2.8", ILI9341 240x320 + XPT2046 터치). 기존 코드는 수정하지 않았습니다.

## 빌드 / 플래시
```powershell
.\build.ps1              # idf.py build (ESP-IDF 5.5.5, C:\esp\v5.5.5)
.\build.ps1 flash COM5   # 플래시 후 모니터
```
`idf.py menuconfig` → **ARD HMI** 메뉴: 기본 PC 서버, 상태 폴링 주기, 패널 색 반전, Z STP 버튼 사용 여부.

## 네트워크 설정
기존 ArdNetwork와 같습니다. `ard-hmi-xxxxxx` AP(비밀번호 `ARD-xxxxxx!`)에 접속해
`http://192.168.4.1/network`(로그인 `admin` / AP 비밀번호)에서 Wi-Fi와 PC 서버를 저장합니다.
NVS `ard-network/settings` 블롭을 그대로 쓰므로 Arduino 펌웨어에서 저장한 설정이 유지되고,
더 오래된 `hmi-cfg` 네임스페이스도 한 번 읽어옵니다. Arduino판의 DNS 캡티브 리다이렉트는 넣지 않았습니다.

## 구조
| 파일 | 역할 |
| --- | --- |
| `main/main.c` | 초기화, 터치 폴링 + UI 태스크(코어 1) |
| `main/lcd.c` | ILI9341 (esp_lcd SPI IO, 40 MHz, 이중 DMA 밴드) |
| `main/gfx.c` | 밴드 렌더러, 도형/8x8 글꼴/16x16 한글 글꼴 |
| `main/touch.c` | XPT2046 (VSPI 하드웨어 SPI, PENIRQ로 미접촉 시 SPI 생략) |
| `main/hmi_ui.c` | 화면 레이아웃·버튼·터치 처리 (ESP 의존성 없음, PC 시뮬레이터와 동일 로직) |
| `main/hmi_net.c` | 서버 통신 전담 태스크(코어 0): 상태 폴링, 하트비트, 명령 큐, 카메라 미리보기 |
| `main/jpeg.c` | ROM TJpgDec로 JPEG → RGB565 (로고, 미리보기) |
| `components/ard_net` | Wi-Fi AP+STA, mDNS, `/network` 설정 페이지 (공용 ArdNetwork 이식) |
| `tools/gen_sim_fonts.py` | 펌웨어 글꼴을 PC 시뮬레이터용 JS로 내보내기 |

## 기존 펌웨어 대비 개선점
**버그 수정**
- 네트워크 태스크가 팝업 표시 때 `redraw()`를 직접 불러 UI 태스크와 동시에 SPI로 그리던 문제 → 화면은 UI 태스크만 그림.
- `pdata` 문자열 배열을 잠금 없이 두 태스크가 읽고 쓰던 경쟁 상태 → 타입이 있는 모델 구조체 + 뮤텍스 스냅샷.
- Drive 2쪽 ToF가 `pdata[0][8]`(존재하지 않는 칸)을 읽어 항상 `---` → `tof_dist`를 직접 표시.
- Drive Z Rail의 `Est`는 서버가 보내지 않는 `z_est`라 항상 0 → 제거하고 PID 목표 표시를 추가.
- PID 목표 버튼 라벨은 ±10인데 실제로는 ±5 → 라벨을 ±5로.
- 하트비트 주기 200 ms는 DriveMCU가 거부(250 ms 미만) → 250/500/1000/2000.
- Robot·Cam 탭의 HB 버튼은 해당 보드가 처리하지 않는 `mzh`를 보냄 → 제거(Robot은 REFRESH만).
- ControlMCU가 꺼져 있어도 마지막 모드가 표시됨 → Online일 때만 표시.
- Robot 4쪽 같은 줄 중복 출력, Z Rail 제목과 장치명이 겹치던 레이아웃 정리.
- HB/I2C 버튼 상태가 보드 로컬 값이라 PC와 어긋남 → 서버 `drive_settings`와 동기화.

**동작 개선**
- 모든 HTTP를 전용 태스크로 이동: 버튼을 눌러도 화면이 멈추지 않음(기존엔 명령 POST·이미지 다운로드가 UI 스레드를 최대 1.5 s 막음, 미리보기 중에는 3초마다).
- 밴드 단위 이중 버퍼 렌더링으로 깜빡임 제거, 바뀐 영역만 갱신(시계는 헤더만).
- 터치는 누를 때 한 번만 실행(기존은 누르고 있으면 250 ms마다 명령 반복), 하드웨어 SPI + 3회 샘플 이상치 제거.
- 낙관적 UI 변경은 4초 동안 서버 값보다 우선해 버튼이 되돌아갔다 오는 현상 제거.
- 승인 팝업: 한글 설명을 16x16 글꼴로 최대 3줄 표시(기존 20자 잘림·한글 깨짐), 응답 후 서버가 지울 때까지 재표시 안 함.
- AI 결과(OBJ/CAT)·사운드 상태 등 한글 값 표시, Ctrl 탭(구 Sound)에 음성 모드/상태 추가, 하위 바에 페이지 제목.
- 어두운 버튼의 라벨을 흰색으로(기존 짙은 파랑 위 검정 글씨), 여러 줄 라벨 줄 간격 2 px.
- 탭마다 보드 온라인 점, 헤더 점은 PC 서버 응답 여부.

## 서버/다른 보드 쪽 확인이 필요한 점
- **ToF 레일 PID 잠금(기본)**: Z Rail 쪽의 PID 켜기/끄기와 목표값 버튼을 잠갔습니다. ToF와 현재 모드는 읽기 전용으로 표시하고 HOME만 동작합니다. menuconfig `HMI_RAIL_PID`로 다시 켤 수 있습니다.
- **Z STP 버튼 잠금(기본)**: PC 서버가 `mzs+N`을 DriveMCU로 그대로 전달하고, DriveMCU는 `mzs<값>`을 Z steps/mm 보정값으로 저장합니다. 기존 HMI는 `mzs+60`처럼 누적값을 보내 보정값을 60으로 바꿨습니다. 서버가 전달을 멈추면 `HMI_Z_STEP_BUTTONS`를 켜세요(켜면 `mzs+10`/`mzs-10` 증분만 보냄).
- 구 SoundMCU(ESP32-C3) 탭은 Ctrl 탭(ESP32-S3 ControlMCU)으로 바뀌었습니다. 버튼은 `/api/controlmcu/command`로 가며, 서버는 `vad`/`ptt`를 음성 모드 전환으로 처리하고 나머지(`mute`, `play`)는 ControlMCU TCP 5002로 전달합니다. 상태는 `/api/status`의 `controlmcu.voice_mode`/`status`를 읽습니다.
- 클래식 ESP32는 ESP-IDF에서 칩 온도 센서를 지원하지 않아 하트비트에 `temp`를 넣지 않습니다(서버 기본값 0).
