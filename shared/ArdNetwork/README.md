# ARD 공통 네트워크
운영용 ControlMCU, DriveMCU, HMI_MCU, ImageSenderMCU, Robot3AxisMCU의 Arduino 네트워크 모듈입니다.
P4는 같은 설정 구조·검증 코드·HTML을 사용하는 ESP-IDF 구현을 제공합니다.

## 접속
- 전원을 켜면 `ard-역할-xxxxxx` SoftAP가 열립니다. `xxxxxx`는 MAC 끝 3바이트의 소문자 16진수입니다.
- AP 비밀번호: `ARD-xxxxxx!`. 웹 로그인: 사용자 `admin`, 비밀번호는 AP와 같습니다.
- `http://192.168.4.1/network` 또는 `http://ard-역할-xxxxxx.local/network`를 엽니다.
- Wi-Fi SSID·비밀번호·PC 서버 주소를 저장하면 1.5초 후 재부팅합니다. 기계 동작을 정지한 뒤 설정하세요.
- 같은 공유기에 연결한 후에도 `.local/network`로 설정 페이지에 접근할 수 있습니다.
- `.local` 접속은 클라이언트의 mDNS 지원과 멀티캐스트 허용이 필요합니다. AP IP 주소 접속도 항상 제공합니다.

| 프로젝트 | 역할 / 호스트 접두사 |
| --- | --- |
| ControlMCU | ard-control |
| DriveMCU | ard-drive |
| HMI_MCU | ard-hmi |
| ImageSenderMCU | ard-image |
| Robot3AxisMCU | ard-robot |
| ImageSenderMCU_P4 | ard-image-p4 |

## 동작
- AP+STA 모드와 DHCP를 사용합니다. AP는 STA 연결 후에도 유지합니다.
- 저장된 Wi-Fi로 자동 접속하며 끊기면 15초 간격으로 재연결합니다.
- PC 주소는 IPv4·DNS 이름·`PC이름.local`을 지원합니다. `.local`은 mDNS로 찾고 30초마다 갱신합니다.
- PC 자체가 해당 이름을 광고해야 합니다. mDNS가 차단된 네트워크에서는 PC의 LAN IPv4를 입력하세요.
- TCP/HTTP 5000, 이미지 UDP 5001과 기존 명령 TCP 5002는 유지합니다.
- Arduino는 AP DNS 리다이렉트를 제공합니다. P4는 `/network`에 직접 접속합니다.
- `_http._tcp`, `_ard._tcp`를 광고합니다. 명령 서버가 있는 Arduino 보드는 `_ard-command._tcp`도 광고합니다.
- `ard-network/settings` NVS blob에 한 번에 저장합니다. 기존 `wifi-config`, `drive-config`, `hmi-cfg`에 저장된 유효한 정보는 읽어 사용합니다. 새 설정은 이전 네임스페이스보다 우선합니다.
- 소스에 하드코딩되었던 Wi-Fi 후보는 제거했습니다. 처음 설정하는 보드는 AP에서 Wi-Fi를 입력합니다.
- 설정 저장은 인증과 부팅마다 새로 생성하는 폼 토큰을 확인합니다. 페이지·로그에 공유기 비밀번호를 출력하지 않습니다.

## 코드 인덱스
- `src/ArdNetwork.h`, `src/ArdNetwork.cpp`: Arduino AP/STA·웹·DNS·mDNS·NVS와 서버 주소 스냅샷.
- `src/network_settings.h`: Arduino/P4 공통 NVS 구조 및 바이트 길이·PSK·호스트 검증.
- `src/form_decode.h`: P4 HTTP 폼 URL 디코더.
- `src/portal_page.h`: 공통 한국어 설정 페이지.
- `../../ImageSenderMCU_P4/main/ard_network.c`: ESP-Hosted Wi-Fi·mDNS·Ethernet 우선 선택.
- `../../ImageSenderMCU_P4/main/ard_network_http.c`: ESP-IDF 설정 페이지 및 저장.
- `test/run_tests.py`: 실제 공통 검증 헤더를 MSVC로 컴파일하고 경계값 테스트.

## 빌드
Arduino 프로젝트는 `platformio.ini`의 `[env] lib_extra_dirs = ../shared`로 이 라이브러리를 사용합니다.
각 MCU 폴더와 `shared` 폴더의 상대 위치를 유지하세요.
P4는 ESP-IDF 5.5.5로 `build_p4.ps1`을 사용하며 C6 준비는 `build_c6.ps1`을 사용합니다.
OldVersions, D2/TEST, ImageSenderMCU의 Wi-Fi 비활성 전원 진단 환경과 외부 라이브러리 예제는 수정 대상에서 제외했습니다.
