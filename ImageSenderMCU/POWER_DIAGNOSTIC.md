# Brownout 재부팅 확인

`E BOD: Brownout detector was triggered`는 MCU가 공급 전압 저하를
감지했다는 의미입니다. 시작 메시지보다 먼저 발생하면 `setup()` 안의
감지 비활성화 코드로 해결할 수 없습니다. Arduino 단독 빌드는
`sdkconfig.defaults`로 사전 빌드된 SDK의 Brownout 설정을 변경하지 않습니다.

## 전원 연결

- USB-UART 모듈의 전원 출력 용량과 현재 사용 중인 3.3V/5V 핀을 확인합니다.
- 보드에 USB 전원 입력이 있으면 충분한 전류를 공급하는 USB 전원과 짧은
  케이블로 먼저 확인합니다. 외부 5V는 보드가 명시한 5V 입력에만 연결합니다.
- 외부 전원 사용 시 USB-UART의 VCC 연결을 분리하고 GND는 보드와 공유합니다.
  TX/RX는 3.3V 논리 레벨을 사용합니다. 3.3V 핀에 5V를 넣지 않습니다.
- 카메라를 분리해 비교하려면 전원을 끈 상태에서 분리합니다.

## 진단 펌웨어

이 환경은 Wi-Fi, 카메라, PSRAM을 초기화하지 않고 CPU를 80MHz로 구동합니다.
Brownout 감지는 유지합니다. 시리얼 출력은 UART에서 115200 baud입니다.
정상이라면 `[POWER TEST] Alive`가 1초마다 출력됩니다.

ImageSenderMCU 폴더에서 실행:

```powershell
pio run -e power-diagnostic -t upload
pio device monitor -b 115200
```

- 진단에서도 Brownout: 전원, 배선, 레귤레이터, 보드 결함을 먼저 확인합니다.
- 진단은 정상이고 본 펌웨어에서 Brownout: PSRAM/카메라/Wi-Fi의 추가 부하를
  견딜 수 있는지 전원 전압을 측정하고 단계별로 확인합니다.
- 진단은 기능을 축소한 비교용이므로 정상 동작만으로 실제 촬영 시 전원이
  충분하다고 판단할 수 없습니다.

전원 확인 후 본 펌웨어 복원:

## 기본 진단이 정상인 경우: 메모리 초기화 비교

`psram-diagnostic`은 본 펌웨어와 동일한 QIO Flash, OPI PSRAM,
160MHz CPU 설정을 사용하되 Wi-Fi와 카메라 코드는 실행하지 않습니다.

```powershell
pio run -e psram-diagnostic -t upload
```

시작 로그의 SDK 버전, CPU 주파수, PSRAM 검색 결과 및 크기를 확인합니다.
이 환경도 시작 전에 재부팅하면 Wi-Fi/카메라 사용 코드가 실행되기 전의
메모리·SDK 초기화 또는 해당 부하가 원인 후보입니다.

그 경우 Flash 모드만 DIO로 바꾸고 OPI PSRAM과 CPU 설정을 유지한
환경을 비교합니다.

```powershell
pio run -e psram-dio-diagnostic -t upload
```

DIO 환경만 정상이면 QIO Flash 설정과 보드/SDK의 호환성을 먼저 확인합니다.
두 환경 모두 정상이라면 본 펌웨어의 추가 초기화 단계로 조사 범위를 좁힙니다.
Brownout 감지는 두 환경 모두 유지합니다.

## 본 펌웨어 복원

```powershell
pio run -e esp32-s3-n16r8 -t upload
```

참고: https://docs.espressif.com/projects/esp-hardware-design-guidelines/en/latest/esp32s3/schematic-checklist.html
