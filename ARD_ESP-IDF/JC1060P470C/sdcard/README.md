# JC1060 관람객 음성 SD 패키지

`ard` 폴더를 FAT32 SD카드 루트에 복사한 뒤 보드에 넣고 재부팅합니다.
보드 경로는 `/sdcard/ard/voice/ko/*.wav`, `/sdcard/ard/voice/en/*.wav`입니다.
`sdcard`라는 상위 폴더를 카드에 만들지 않습니다. 이 패키지는 음성 미디어이며 펌웨어와 별개입니다.
웹 업로드 기능이 있는 펌웨어에서는 PC의 `tools/upload_visitor_voice.py --host <보드 IP>`로 전송할 수 있습니다.

- 한국어·영어 각 14종: Google Cloud Text-to-Speech `Chirp3-HD-Aoede`.
- 참여, 제어 진입, 조이스틱, 레일, 카메라, 음성 탭, 설정, 비상정지 요청,
  재가동 확인, PC 연결 끊김/연결, 선별 시작/완료 보고, 안전 안내.
- PCM WAV / 16,000 Hz / 16-bit / mono. 음성 데이터는 펌웨어에 임베드하지 않습니다.
- 언어는 HMI의 English / 한국어 설정을 따릅니다. 설정 → 소리의 안내 ON/OFF와 음량이 적용됩니다.
  참여/조작/안전 안내 듣기로 미리 들어볼 수 있습니다. 안내 OFF 상태에서도 미리 듣기는 가능합니다.
  비상정지 요청 안내는 안내 OFF와 별개지만, 음소거와 음량 0은 따릅니다.
- 파일 누락 시 다른 언어로 대신 재생하거나 네트워크 TTS를 요청하지 않습니다. UI·제어는 계속 사용할 수 있습니다.
- 처음 SD를 찾지 못한 경우 다음 요청에서 30초 간격 제한을 두고 재시도합니다. 사용 중 카드 교체는 재부팅 후 확인합니다.
- `voice_manifest.json`에는 파일별 길이·PCM 크기·SHA-256과 생성 출처가 있습니다.

음성은 유효한 Google Cloud 키로 서버에서 생성한 뒤 SD용 파일로 저장했습니다. 키는 음성 패키지·보드·펌웨어에 넣지 않았습니다.
재생성은 `python tools/generate_cloud_visitor_voice.py`, 미디어 검사는 `python tools/verify_visitor_voice.py`입니다.
Azure SunHi / Hyunsu와 Google Kore의 참여 안내 비교 파일은 별도 Cloud 음성 패키지의 `comparison` 폴더에 있습니다.
