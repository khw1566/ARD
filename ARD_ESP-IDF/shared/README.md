# ESP-IDF 공통 SoftAP 설정

ControlMCU, DriveMCU, HMI_MCU, ImageSenderMCU, JC1060P470C가 사용합니다. 각 프로젝트를 복사할 때 `ARD_ESP-IDF/shared`와의 상대 경로를 유지하세요.

- 부팅 시 시리얼 모니터에 SoftAP 접속 QR과 `http://192.168.4.1/network` 주소 QR을 출력합니다. ANSI 색상과 UTF-8을 지원하는 모니터에서 확인하세요.
- 웹 로그인은 기존과 같이 `admin`과 SoftAP 비밀번호를 사용합니다.
- 웹 화면 진입 시 보드가 주변 SSID를 검색합니다. 최대 64개 AP 레코드에서 중복 SSID를 제거한 목록을 표시합니다. `다시 검색` 버튼으로 갱신할 수 있습니다.
- 저장된 SSID를 선택하면 비밀번호를 채웁니다. 비밀번호는 화면에 표시되며 수정할 수 있습니다. 공개 Wi-Fi는 비워 두세요.
- 설정 적용 시 기존처럼 재시작합니다. 새 자격 증명은 먼저 검색 결과와 일치할 때 연결하며, IP 주소를 받은 경우에만 성공 기록에 추가합니다.
- 성공 기록은 NVS `ard-network/profiles16`에 최대 16개 저장합니다. 같은 SSID의 비밀번호는 성공한 값으로 갱신합니다. 최근 성공 순으로 유지하며, 17번째 SSID가 성공하면 가장 오래된 성공 항목을 교체합니다.
- 재부팅 시 먼저 주변 SSID를 검색합니다. 저장된 항목 중 검색에 나타난 SSID만 최근 성공 순서로 시도합니다. 새로 적용한 설정이 있으면 검색된 경우 한 번 먼저 시도합니다.
- 실패한 자격 증명은 성공 기록을 지우지 않습니다. 같은 SSID의 새 비밀번호가 틀리면 이전 성공 비밀번호도 후보가 됩니다.
- 연결 시도는 약 12초 대기합니다. 후보가 모두 실패하면 최소 15초 간격으로 새 검색을 요청합니다. 연결이 끊긴 경우에도 새 검색 후 연결합니다.
- 숨겨진 SSID는 검색에 나타나지 않으므로 자동 연결 대상에서 제외됩니다.

## HMI 화면

- HMI_MCU: Wi-Fi 미연결 상태로 시작하면 QR 화면이 열립니다. `Next QR`로 두 QR을 전환하고 `Done`으로 닫습니다. 상단 헤더를 1.2초 누르면 다시 열립니다.
- JC1060P470C: `System` 탭의 `Setup QR` 버튼을 누릅니다. `Next QR`로 전환합니다. 상단의 E-STOP은 계속 접근할 수 있습니다.

## 코드와 검증

- `ard_wifi.c`: 스캔, 연결 후보 선택, 연결 결과 이벤트, NVS 저장.
- `wifi_profiles.h`: 16개 성공 기록의 검증과 교체 규칙.
- `ard_wifi_http.h`, `portal_page.h`: 인증된 검색 API와 한국어 설정 화면.
- `ard_setup_qr.c`, `third_party/qrcodegen.*`: 공통 QR 생성과 시리얼 출력. QR 생성기는 기존 LVGL 배포본의 Project Nayuki MIT 코드를 독립 사용하도록 조정하고 심볼에 접두사를 붙였습니다.
- `test/test_portal.js`: `node ARD_ESP-IDF/shared/test/test_portal.js`로 웹 화면 동작을 확인합니다.
- `test/test_wifi.c`: 실제 연결 관리 코드를 가짜 Wi-Fi/NVS 이벤트로 구동하는 PC 단위 테스트입니다. `python ARD_ESP-IDF/shared/test/run_tests.py`로 실행합니다. 정상적인 MSVC C 도구와 Windows SDK 설치가 필요합니다.

펌웨어 빌드와 실물 검증은 별도입니다. HMI_MCU의 `build.ps1`은 SDK에 지정된 도구 버전과 실제 설치 경로를 사용하도록 수정했습니다. 나머지 보드의 빌드 스크립트에는 이전 PC의 사용자 경로가 남아 있으므로 실행 전에 실제 도구 경로를 확인해야 합니다.

HMI_MCU의 `test/sim`은 실제 화면 코드와 연결 관리 코드를 QEMU에서 실행합니다. MSVC/Windows SDK 없이도 연결 관리의 7개 단위 테스트와 화면 QR 판독을 검증할 수 있습니다. 자세한 실행 방법은 `../HMI_MCU/README.md`를 참고하세요.
