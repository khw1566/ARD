# ARD 발표 영상 수정 인수인계 (2026-10-26)

## PC 후속 완료 기록 — 2026-10-26

아래 클라우드 작업 기록을 읽고 PC에서 수정을 완료했다. `/mnt/project-files/ard-video-fix/src/`의 수정 소스는 PC에 없었으므로, 이 문서의 다섯 가지 수정 사양을 현재 코드에 재구성했다. **최신 결과는 `final/ARD_FullPresentation_1080p_v2.mp4`**다. 원본 v1과 촬영본은 해시를 대조해 보존했다.

- 재생 페이지: `http://127.0.0.1:5187/review_v2.html`.
- 단독 실물: `final/ARD_LiveDemo_Stabilized_1080p_v2.mp4`.
- 이중 자막 제거, PC 모니터 가림, 크레딧 1회, 레일→선회→기울임→중립→레일 복귀, 종이색 정보판과 한국어 설명을 반영했다.
- 미리보기와 GPU 출력은 `editorial_overlay.js`의 동일한 캔버스 그림을 사용한다. 프레임 4020장을 JPEG로 저장하는 대신 PC WebGL/WebCodecs에서 직접 H.264로 출력했다. 4020프레임 출력에 약 21.86초가 걸렸다. 클라우드 프레임·Playwright·PC VM·30MB 전송 제한은 사용하지 않았다.
- 현재 조립 명령: `python output/ard-cg/make_presentation_v2.py`. 렌더 수신기가 남긴 `render_jobs/latest.txt`는 **v2 4020프레임 job**이어야 한다. 기존 믹스·개인 음성 파일도 필요하다.
- 서체: 로컬 `fonts/`, IBM Plex Sans KR Regular/Medium 및 Plex Mono Regular. OFL과 출처·해시는 같은 폴더에 보존했다.
- 모니터 가림은 sigma 48 블러와 플레이트 전경 윤곽을 제외하는 프레임별 마스크다. **alphamerge+overlay**를 사용하며 마스크의 255를 완전 불투명 알파로 유지한다. YUV 마스크 범위 변환으로 원본 글자가 옅게 남았던 첫 시도는 최종본에서 교체했다. 시간 기준도 1/30으로 통일했다.
- 두 최종 파일에는 자막 스트림 및 촬영 위치 태그가 없다. 외부 SRT/VTT만 별도로 제공하며 재생 HTML에도 자동 자막 트랙을 넣지 않았다.
- 검증: `node output/ard-cg/verify_v2_content.cjs`와 `python output/ard-cg/verify_presentation_v2.py`. 180초/5400프레임, 실물 1268프레임, 전체 디코딩·음량·자막 트랙 0·위치 메타데이터 없음·기존 원본 해시 일치를 확인했다. 실물 화면 가림은 12시점을 눈으로 확인했으며 전체 모든 프레임을 사람이 수동 검수한 것은 아니다.
- 상세 결과: `results/261026_발표영상_v2_피드백반영.md`, 수치: `final/presentation_v2_quality_report.json`.
- `_review_tmp/`는 삭제하지 않았다. 펌웨어/앱 컴파일, Git 커밋·푸시, 외부 업로드는 수행하지 않았다.

이하 1–6절은 인계받은 클라우드 작업 당시의 기록이며, 현재 실행 방법은 위 PC 완료 기록이 우선한다.

## 1. 사용자 요청
- 대상: `C:\Users\work\Desktop\ARD_v1\output\ard-cg\final\ARD_FullPresentation_1080p.mp4` (180초, 1080p30)
- 요청 원문: "문제점 수정해줘. 이거 코드로 만든거니, 상세히 수정가능할거야. 디자인도 ai느낌 안나게 수정해줘"
- 검토 보고서(문제 목록 원본): Claude Doc "ARD 발표 영상 검토 결과" https://claude.ai/artifact/9tM5ykg8T9fPVLRWVKnz1v
- 결과물은 원본을 덮어쓰지 말고 `final/ARD_FullPresentation_1080p_v2.mp4`로 만든다.

## 2. 고쳐야 할 문제 (검토 결과)
1. mp4에 mov_text 자막 트랙이 들어 있어, 플레이어에서 화면에 새긴 자막과 이중으로 표시됨. 화면 CG 자막과 내레이션 문장도 서로 다름.
2. 실물 구간(2:14~2:56)에서 PC 모니터에 개인 이메일과 AI 채팅 화면이 노출됨.
3. 음악 크레딧이 live.ass(2:49)와 outro.ass(2:56)에 중복됨.
4. 분류 순서 장면: 레일이 선회와 동시에 25만큼만 움직여, 내레이션의 "먼저 레일을 이동"과 맞지 않음.
5. 디자인: 영어 키커, 페이지 카운터, 진행 바, 리더선 같은 "AI 템플릿" 느낌의 요소를 없애고 평면 인쇄물 느낌으로 바꾼다.

참고: 감속비 4:1과 ψ=(a+b)/2, φ=(a−b)/2는 정확하다. 각도 표시는 반올림한 입력값으로 출력을 계산하도록 고쳤다.

## 3. 파이프라인 구조 (원본, PC의 ard-cg 폴더)
- CG는 three.js 페이지 `cinematic.html`, `cinematic.js`, `cinematic_plan.js`(장면 목록과 `frameAt`)로 만든다. 리그 파일은 `differential_rig.js`, `planetary_rig.js`이다.
- `serve.py`가 127.0.0.1:5187에서 서빙한다. `/cad/`는 `ARD_v1/영상제작용 CAD파일`에 매핑된다.
- 원래 CG mp4는 사용자 Windows Chrome에서 `export.html`/`export_video.js`(WebCodecs)로 뽑았다.
- 오디오와 조립은 다음 스크립트가 맡는다.
  - `mix_audio.py`: 내레이션 맞춤, 음악 덕킹, 효과음
  - `finalize_video.py`: CG 135초 영상
  - `make_presentation.py`: 180초 발표본. 구성은 아래와 같다.

| 구간 | 내용 | 길이 |
|---|---|---|
| 소개 | 390프레임 | 13초 |
| CG | 원본 8~129초 | 3630프레임 |
| 실물 | `live/live_stabilized_clean.mp4` | 1268프레임 |
| 마무리 | | 112프레임 |

- 원본 make_presentation은 Windows ffmpeg(`C:\KHW\ffmpeg\bin\ffmpeg.exe`)와 h264_nvenc를 쓴다.
- AGENTS.md 규칙: 펌웨어나 앱 빌드, 컴파일러를 실행하지 말 것. 영상 렌더링, Python, JS는 허용된다.

## 4. 이미 한 작업 (수정된 파일은 `/mnt/project-files/ard-video-fix/src/`)
**PC의 원본 파일은 아직 하나도 바꾸지 않았다.** 아래 파일을 PC의 `ard-cg/` 폴더에 넣으면 된다(`cg_captions.json`은 `narration/`에).

- **`build_captions.py` (신규)**
  - 장면 2~14의 표기형 문장(DISPLAY)으로 자막 큐를 만들어 `narration/cg_captions.json`에 쓴다.
  - 표기 예: PC, DriveMCU, A/B, 0, 4 : 1.
  - 문장 단위로 나누고, 34자가 넘는 문장은 쉼표에서 한 번 더 나눈다.
  - 시간은 timing.json의 narration_start/end 사이에 글자 수 비율로 나눈다.
  - 소개 자막 큐(0.35초부터)도 함께 만든다. 이미 실행해서 결과 json이 같이 들어 있다.
- **`cinematic.html` / `cinematic.css` (재작성)**
  - 서체: IBM Plex Sans KR / Plex Mono(`fonts/`, OFL)
  - 색과 형태: 종이색 카드(#efede7)와 주황 강조선(#c47f2c)
  - 왼쪽 아래에 제목판(`.editorial`)을 둔다.
  - 하단 104px의 고정 자막 띠(`.caption-band`)에 내레이션과 같은 문장이 나온다.
  - 수식 카드와 A/B 입력축 태그를 둔다.
  - 유성기어 범례는 리더선 없이 세로로 쌓는다.
  - 레일 위치 미터를 두고, 소개 카드(`#intro-card`)를 추가했다.
  - 영어 키커, 페이지 카운터, 진행 바, 사운드 버튼은 삭제했다.
- **`cinematic_plan.js`**
  - 키커를 한국어로 바꾸고 `sub` 필드를 삭제했다.
  - `RAIL=30`으로 바꿨다.
  - 분류 순서 장면은 아래 순서로 움직인다. 카메라는 측면 고정이다.

| 진행 구간 (u) | 동작 |
|---|---|
| 0~0.2 | 레일 이동 |
| 0.22~0.4 | 선회 |
| 0.42~0.6 | 기울임 |
| 0.64~0.8 | 중립 복귀 |
| 0.82~1 | 레일 복귀 |

- **`cinematic.js`**
  - 각도 표기: 반올림한 입력으로 출력을 계산하고, 유니코드 마이너스를 쓰며, −0이 나오지 않게 했다.
  - 단계 표시: 분류 순서 장면의 단계는 `frame.step`을 쓴다.
  - 태그 위치: A/B 태그는 호 옆에 놓고, 투영 전에 `updateMatrixWorld`를 호출한다.
  - 자막: `cg_captions.json`을 불러와 표시한다.
  - 내보내기 API:
    - `window.cgExport.frame(t)`: CG 시간 t의 프레임을 그린다.
    - `window.cgExport.intro(t)`: 소개 0~13초를 그린다. CG 0~8초를 늘려 쓰고 소개 카드와 자막을 얹는다.
- **`overlay.html` + `render_overlays.js` (신규)**
  - 실물 구간과 마무리에 쓸 투명 PNG 오버레이를 같은 CSS로 만든다.
  - 실물 구간: 상단 자막 띠와 왼쪽 아래 "실물 구동" 제목판
  - 마무리: 어두운 화면에 "감사합니다"와 크레딧(한 번만)
- **`make_presentation_v2.py` (신규, 아직 한 번도 끝까지 실행하지 않음)**
  - 오디오 믹스는 원본 코드를 그대로 쓴다.
  - 소개와 CG는 `frames/intro/%05d.jpg`, `frames/cg/%05d.jpg` 시퀀스로 인코딩한다. 위치는 `ARD_FRAMES` 환경변수로 바꿀 수 있다.
  - 실물 구간 처리:
    - 모니터 영역(크롭 1090,0,830x440 안의 다각형, 페더 마스크)에 gblur sigma 36을 건다.
    - 오버레이는 ffconcat으로 붙인다.
  - 마무리: 마지막 프레임에 블러를 걸고 `overlay_outro.png`을 0.35초 페이드인한다.
  - mp4에 자막 트랙을 넣지 않는다. 대신 `final/ARD_FullPresentation_1080p_v2_Korean.srt/.vtt`를 쓴다.
  - 실물 시연 단독본 `ARD_LiveDemo_Stabilized_1080p_v2.mp4`도 만든다.
  - 인코더: Windows ffmpeg가 있으면 nvenc, 없으면 libx264 crf 17을 쓴다.
- **`render_all.js` (신규)**
  - 프레임 렌더러다. 실행 방법: `node render_all.js <worker> <nworkers>`
  - 소개 390프레임과 CG 3630프레임을 `/tmp/work/frames/{intro,cg}/`에 JPEG(q94)로 쓴다.
  - 이미 있는 프레임은 건너뛴다. 경로는 스크립트 안에 하드코딩되어 있으니 필요하면 고친다.
- **`shot2.js`**: 시험 스틸 렌더링용이다. 사용법은 `node shot2.js <outdir> 70 108 i1`이며, `i` 접두사가 붙으면 소개 시간이다.

## 5. 남은 일
1. **CG 프레임 전체 렌더링 (4020장)**
   - 이전 세션의 클라우드 렌더는 컨테이너가 사라지면 함께 없어진다. 처음부터 다시 돌려야 한다.
   - 클라우드(4코어, 소프트웨어 GL) 기준 약 0.8프레임/초로, 약 85분 걸린다.
   - 사용자 PC Chrome의 GPU로 돌리면 훨씬 빠를 수 있다.
   - Playwright 실행 옵션: `--use-gl=angle --use-angle=swiftshader --enable-unsafe-swiftshader --ignore-gpu-blocklist`, 뷰포트 1920x1080, URL `cinematic.html?export&still`, `window.cgReady===true`와 `document.fonts.ready`를 기다린다.
2. **실물 구간 오버레이 결과 확인**
   - 시험 이미지를 만들었지만 아직 눈으로 보지 못했다.
   - 확인할 것: 모니터 블러 영역이 42초 동안 모니터를 계속 덮는지(카메라가 조금 움직임), 자막 띠와 제목판, 마무리 크레딧이 읽히는지.
3. **`make_presentation_v2.py` 실행과 디버깅**
   - 필요한 것: Python(opencv, soundfile, numpy), node와 playwright(오버레이용), serve.py 실행
   - 확인할 지점:
     - ffconcat 이미지 오버레이 길이
     - `eof_action`
     - render_overlays.js의 NODE_PATH
4. **품질 확인**
   - 길이가 정확히 180.0초인지 확인한다.
   - 장면별 스틸을 보며 겹침과 오타가 없는지 확인한다.
   - 자막 트랙이 없는지 ffprobe로 확인한다.
   - 음량이 −16 LUFS인지 확인한다.
   - 소개 13초 → CG, CG → 실물 전환 지점이 매끄러운지 본다.
5. **PC로 전달**
   - `device_commit_files`는 파일당 30MB 제한이 있다. mp4를 30MB 미만으로 나눠(`-c copy` segment) 보내고 PC에서 ffmpeg concat으로 합친다.
   - 수정한 소스 파일(src/ 전체)도 PC의 `ard-cg/`에 복사한다.
   - PC VM은 약하다(2코어, 3GB, chromium 없음, soundfile 없음). 그래서 렌더와 조립은 클라우드에서 했다.
6. **마무리 정리**
   - PC에 검토용 임시 폴더 `ard-cg/_review_tmp/`가 남아 있다. 삭제 권한이 없어서 사용자에게 지워도 된다고 안내만 했다.
   - 프로젝트 메모리 `ard-presentation-video.md`를 결과에 맞게 갱신한다.

## 6. 클라우드 작업 환경 재구성 방법
1. PC에서 `device_stage_files`로 아래 파일을 가져온다.
   - `ard-cg/`의 assets/*.stl(21개)
   - narration/timing.json, manifest.json, sfx_*.wav
   - live/*(json, wav, live_stabilized_clean.mp4)
   - final/ARD_narration_only.wav
   - source_audio/Cylinder_Five.ogg
   - vendor/, *_rig.js, serve.py, mix_audio.py, *_inventory.json
   - `영상제작용 CAD파일/`의 하부이송구조.stl, 비전 캠 장치.stl
2. 원래 폴더 구조(`ARD_v1/output/ard-cg`, `ARD_v1/영상제작용 CAD파일`)를 그대로 만든다.
3. src/ 파일을 덮어쓴다.
4. `pip install opencv-python-headless soundfile`
5. 폰트를 `~/.fonts`에도 복사한다.
6. `python3 serve.py &`를 실행한다.
