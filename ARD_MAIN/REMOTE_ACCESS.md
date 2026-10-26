# 외부 웹 접속

접속 주소: [ARD Control Center](https://ard.audreaming.com)

기존 `audreaming.com` 홈페이지는 유지합니다. ARD 메인 프로그램은 `ard.audreaming.com`에서 비밀번호로 접속합니다.

- 비밀번호 확인: `.remote-access/connection.txt`.
- 실행: `start_remote.bat`를 두 번 클릭합니다. 이미 실행 중이면 해당 프로세스를 재사용합니다.
- 비밀번호 변경: `change_remote_password.bat`를 두 번 클릭하고 새 비밀번호를 두 번 입력합니다. 기존 로그인 세션도 무효화됩니다.
- 로그아웃: 메인 화면의 메뉴 → 외부 접속 로그아웃.
- 접속 종료: PowerShell에서 `./stop_remote.ps1`을 실행합니다. PC 메인 프로그램은 유지합니다.
- 메인 프로그램까지 종료: `./stop_remote.ps1 -IncludeMain`.

PC가 켜져 있고 인터넷에 연결되어 있어야 접속할 수 있습니다. Windows를 다시 시작한 뒤에는 `start_remote.bat`를 실행합니다. 자동 로그인이나 Windows 시작 작업은 등록하지 않았습니다.

## 연결 구성

`브라우저 HTTPS → Cloudflare Tunnel → 127.0.0.1:5006 비밀번호 게이트웨이 → 127.0.0.1:5000 메인 프로그램`

- 전용 터널: `ard-main-program`.
- 원격 세션: 최대 8시간, HTTPS 전용 쿠키, HttpOnly, SameSite=Strict.
- 로그인 전에는 메인 화면, 정적 파일, API 및 터미널에 접근할 수 없습니다.
- 비밀번호는 게이트웨이 설정에 scrypt 해시로 보관합니다. 사용자가 확인할 초기 비밀번호 사본은 로컬 `connection.txt`에만 저장합니다.
- 로그인 실패 제한과 요청 출처 검사를 적용합니다.
- 기존 LAN·MCU 접속에는 원격 로그인 절차를 추가하지 않습니다.
- 실시간 상태 및 오디오 HTTP 응답은 스트리밍으로 전달합니다.
- 서버는 기존 Python 3.12 환경의 Waitress로 실행합니다. 원격 시작 시 음성 모델은 미리 불러오지 않고 필요한 시점에 불러옵니다.

`.remote-access`에는 비밀번호, 세션 키, 터널 인증 파일이 있습니다. 이 폴더와 백엔드 `.env`는 Git에서 제외합니다. 이 PC의 설정과 인증을 보존하려면 로컬에 안전하게 백업하십시오. 다른 PC에서는 해당 PC의 Cloudflare 인증과 실행 환경에 맞춰 다시 구성해야 합니다.

실행 기록은 `.remote-access/*.stdout.log`, `.remote-access/*.stderr.log`에 저장됩니다. 로그에는 메인 프로그램의 상태 정보가 포함될 수 있습니다.

## 참고

- [Cloudflare Tunnel 구성](https://developers.cloudflare.com/tunnel/features/locally-managed-tunnels/create-local-tunnel/)
- [Flask 보안 설정](https://flask.palletsprojects.com/en/stable/web-security/)
- [Waitress 서버 설정](https://docs.pylonsproject.org/projects/waitress/en/latest/arguments.html)
