# JC1060 Cloud TTS 재생성 및 SD 교체 완료

2026-10-27 KST. 사용자가 Windows TTS 대신 유료 Cloud TTS로 다시 만들고,
ARD에서 키를 찾지 못하면 Mini WebServer 프로젝트에서 찾도록 지시했다.

## 서비스와 키 확인

- ARD의 Azure 설정은 공식 Azure 음성 목록 요청에서 HTTP 401이었다.
- 지정된 Mini WebServer M3의 비공개 SD 설정에서 Azure Speech와 Google Cloud TTS 키를 찾았다.
  두 키 모두 공식 음성 목록 API에 인증됐으며, 실제 음성 합성도 성공했다.
- 키 값은 콘솔에 출력하거나 ARD 설정·음성 파일·펌웨어·결과 패키지에 복사하지 않았다.
  생성 폴더와 SD 패키지 원본을 실제 키 바이트와 비교해 포함되지 않았음을 확인했다.
- 기존 ARD `.env`는 수정하지 않았다. 이 작업은 녹음된 안내 파일의 생성·배포이며 실시간 TTS 설정 변경과 별개다.

## 적용한 음성

| 용도 | 서비스 / 음성 | 개수 |
|---|---|---:|
| 보드 기본 한국어 | Google Cloud `ko-KR-Chirp3-HD-Aoede` | 14 |
| 보드 기본 영어 | Google Cloud `en-US-Chirp3-HD-Aoede` | 14 |
| 비교용 한국어 여성 참여 안내 | Azure `ko-KR-SunHiNeural` | 1 |
| 비교용 한국어 남성 참여 안내 | Azure `ko-KR-HyunsuMultilingualNeural` | 1 |
| 비교용 다른 한국어 여성 참여 안내 | Google `ko-KR-Chirp3-HD-Kore` | 1 |

Google 기본 음성은 speakingRate 1.08, Azure 비교 음성은 SSML rate +5%로 요청했다.
모두 16 kHz / PCM16 / mono WAV이다. Google LINEAR16 및 Azure RIFF 응답을 사용했다.
보드 기본 파일 중 최장 4.676초, PCM 합계 3,108,096 bytes, WAV 합계 3,109,328 bytes이다.
7초 화면 보호기 장면과 0.5초 반향 대기 예산을 모두 만족한다.

이전 Windows 기본 음성은 `output/jc1060-cloud-voice/previous_sd_voice.zip`에 보존했다.
비교본은 패키지에만 담았으며 현재 보드에는 Google Aoede 기본 28개를 적용했다.

## 실제 배포·검사

- `192.168.0.30`의 기존 웹 업로드 기능으로 28개 파일을 교체했다.
- 각 파일의 저장 후 SHA-256과 별도 SD 읽기 SHA-256이 원본과 일치했다.
- 검증 결과: `261027_JC1060_GoogleCloud_음성업로드_검증.json`.
- 로컬 WAV 검사: 28개 형식, RIFF 크기, 해시, 무음 여부, 크기·장면 시간 예산 통과.
- 배포 패키지: `output/JC1060_Cloud_TTS_SD_20261027.zip` (기본 28개 + 비교 3개).
- 파일별 서비스·음성명·길이·해시는 `ARD_ESP-IDF/JC1060P470C/sdcard/voice_manifest.json`에 기록했다.
- 펌웨어를 수정하거나 다시 플래시하지 않고 SD 자산과 재생 캐시를 갱신했다.
  실제 스피커 청취 검증은 수행하지 않았다. PC에서 비교 WAV를 직접 들을 수 있다.

## 생성과 API 근거

- 생성 스크립트: `ARD_ESP-IDF/JC1060P470C/tools/generate_cloud_visitor_voice.py`.
  키는 실행 시 지정된 비공개 JSON에서 메모리로만 읽는다. 재실행은 동일 작업 메타데이터가 있으면 생성 파일을 재사용한다.
- [Google Chirp 3 HD 음성과 속도 제어](https://docs.cloud.google.com/text-to-speech/docs/chirp3-hd)
- [Google text.synthesize REST 및 LINEAR16 WAV 응답](https://docs.cloud.google.com/text-to-speech/docs/reference/rest/v1/text/synthesize)
- [Azure Neural TTS REST 및 출력 형식](https://learn.microsoft.com/en-us/azure/ai-services/speech-service/rest-text-to-speech)
