# ImageSenderMCU P4 스페어 펌웨어

참조 예제: `D:\프로젝트\2026project\ESP32-P4 CAM TEST` (2026-10-10 확인).
Waveshare ESP32-P4-Module-DEV-KIT / ESP32-P4NRW32 / OV5647 / IP101 유선 LAN / ESP-IDF 5.5.5.
기존 S3 ImageSenderMCU를 대신하여 PC backend에 촬영 JPEG를 보냅니다.

보드의 [공식 문서](https://docs.waveshare.com/ESP32-P4-Module-DEV-KIT)는
ESP32-P4NRW32, 32MB PSRAM, 모듈의 16MB Flash와 OV5647용 MIPI-CSI를 명시합니다.
현재 펌웨어는 유선 LAN과 보드 내부 ESP32-C6의 SDIO Wi-Fi를 사용합니다.
카메라는 참조 예제와 같은 OV5647을 전제로 하며 실제 센서 ID 0x5647로 확인해야 합니다.
현재 sdkconfig는 P4 Rev 1.x용이므로 실물 부팅 로그의 칩 revision도 확인하세요.

## 사용

1. `main/ard_config.h`의 ARD_SERVER_HOST는 초기 PC 주소 기본값입니다. Wi-Fi 설정 화면에서 저장한 PC 주소가 우선합니다. 현재 기본값은 10.115.5.12입니다.
2. 카메라는 CAMERA/CSI J4에 연결합니다. 네트워크는 유선 LAN 또는 C6 Wi-Fi를 사용하며 IP는 DHCP로 받습니다.
3. PowerShell에서 이 프로젝트 폴더를 열고 `./build_p4.ps1` 실행.
4. `./flash_p4.ps1 -Port COM9` 실행. 실제 포트로 변경합니다.
5. PC backend 실행 후 기존 촬영 버튼 사용. 기존 ImageSenderMCU는 분리하세요. 설정 GET은 명령을 한 번만 전달하므로 두 송신기를 동시에 사용하면 명령을 나누어 받습니다.

## SoftAP / mDNS Wi-Fi 설정
- 공통 사용법: [ARD 네트워크 설정](../shared/ArdNetwork/README.md).
- AP 및 호스트 이름: `ard-image-p4-xxxxxx`, AP 비밀번호: `ARD-xxxxxx!`.
- 웹 로그인은 `admin` / AP 비밀번호. `xxxxxx`는 P4 MAC 끝 3바이트이며 시리얼에 출력합니다.
- `http://192.168.4.1/network` 또는 `http://ard-image-p4-xxxxxx.local/network`에서 Wi-Fi와 PC 주소를 저장합니다.
- PC 주소는 IPv4, DNS 이름, mDNS `.local` 이름을 사용할 수 있습니다.
- Ethernet 연결을 우선하며 LAN 케이블을 빼면 연결된 Wi-Fi를 사용합니다. 카메라 웹 미리보기도 Wi-Fi에서 사용 가능합니다.
- ESP-Hosted 2.12.2의 C6 보조 펌웨어가 필요합니다. C6에 다른 펌웨어가 있으면 P4 펌웨어만 업로드해도 Wi-Fi가 동작하지 않습니다.
- `./build_p4.ps1` 후 `./build_c6.ps1`로 대응하는 C6 펌웨어를 생성합니다.
- C6 업로드는 C6 UART 헤더에 연결한 3.3V USB-UART 포트에서 `./flash_c6.ps1 -Port 실제포트`를 사용합니다. P4 USB 업로드 포트와 구분하세요. C6 다운로드 모드 진입은 보드 문서의 C6 BOOT/EN 절차를 따릅니다.
- C6 SDIO 연결: P4 CLK18/CMD19/D0=14/D1=15/D2=16/D3=17, C6 EN 리셋 GPIO54. 20MHz 버스를 사용합니다.
- 앱 파티션을 1500KB로 확장했습니다. 앱·파티션·부트로더를 함께 적용하는 업로드 스크립트를 사용하세요.

## 지원 범위

- 3초마다 `/api/imagesendermcu/config` 조회, capture 명령 처리.
- UDP:5001 기존 0=시작/1=중간/2=끝, JPEG payload 1436바이트. 단일 패킷 JPEG도 종료 패킷 전송. 전송 실패 시 중단. UDP의 실제 PC 수신을 확인하는 ACK는 없습니다.
- TCP 모드는 `POST http://PC:5000/upload`, Content-Type image/jpeg. HTTP 2xx만 성공 처리.
- 10초마다 `/api/heartbeat`, source=ImageSenderMCU. 현재 RSSI는 0 고정이며 온도 값은 보내지 않습니다.
- `/api/log`에 전송 결과 기록.
- JPEG 품질: PC 0~50(작을수록 고품질)을 하드웨어 100~20으로 선형 변환. S3 압축률과 동일한 결과를 보장하는 매핑은 아닙니다.
- 800x640, OV5647 고정. PC의 다른 resolution/camera_model 설정은 적용하지 않습니다. PC 상태 화면의 설정값은 실제 센서 해상도와 다를 수 있습니다.
- 기존 테스트 웹 미리보기 유지: http://보드IP/ 및 http://보드IP:81/stream. 미리보기 중 촬영은 같은 JPEG 뮤텍스를 사용하여 직렬화됩니다.
- 1초 이상 갱신되지 않은 JPEG는 촬영에 사용하지 않습니다. 카메라 초기화 실패 시 네트워크는 유지하고 재부팅 후 재시도합니다.
- CSI/ISP 설정과 센서 밝기/색 제어는 테스트 소스를 유지했습니다. 자동 노출/화이트밸런스 추가 없음.

## 확인

시리얼에서 센서 ID 0x5647, First CSI frame received, First JPEG encoded, DHCP IP를 확인하세요.
PC에서 ImageSenderMCU Online과 촬영 이미지 수신을 확인하세요.
펌웨어 빌드 성공 여부와 실물 카메라/네트워크 검증은 구분해야 합니다.

출력: firmware-output/esp32p4_cam_eth_test.bin, bootloader.bin, partition-table.bin, ELF.
각 BIN의 주소는 빌드 flash_args를 따라야 하므로 업로드 스크립트를 권장합니다.

2026-10-03: ESP-IDF 5.5.5 컴파일/링크/BIN 생성/파티션 크기 검사 통과. 앱 0xcba70 bytes, 1MB 파티션 20% 여유. 보드 업로드와 실물 촬영은 수행하지 않았습니다.

2026-10-10: 지정한 D: 참조 예제와 보드 핀 설정 일치를 확인하고 ESP-IDF 5.5.5 전체 재빌드 통과.
앱 834,160 bytes, 1MB 앱 파티션 20% 여유. `firmware-output`의 앱/부트로더/파티션 BIN과 ELF를 갱신했습니다.
빌드 로그: `../../tmp/p4-build-261010.log`. 이번에도 업로드·실물 촬영·장시간 안정성은 검증하지 않았습니다.

2026-10-10 SoftAP/mDNS 추가 후: P4 최종 앱 1,173,696 bytes, 1500KB 앱 파티션 24% 여유.
C6 ESP-Hosted 2.12.2 보조 앱 1,222,048 bytes도 빌드했습니다.
P4 배포 파일은 `firmware-output`, C6는 `firmware-output/c6`에 있으며 C6 OTA 초기 데이터도 포함합니다.
전체 적용 결과와 실물 미검증 범위는 [네트워크 적용 결과](../results/261010_네트워크설정.md)에 정리했습니다.
