# ARD 발표용 시스템 회로 구성 문서

- 최종 문서: `ARD_시스템_회로구성_2장.pdf` (A4 가로, 2쪽)
- 1쪽: 시스템 구성, 네트워크 제어 경로, JC1060 통합 단말과 기존 단말의 관계
- 2쪽: 12V→5V 공급 계통, JC1060 독립 전원, 주요 GPIO 표
- `page_01.svg`, `page_02.svg`: 편집 가능한 벡터 그림. Malgun Gothic 폰트 사용.
- `page_01.png`, `page_02.png`: PDF를 렌더링한 미리보기
- `create_document.py`: 문서 재생성 소스. 펌웨어·앱 빌드를 수행하지 않는다.
- `validation.json`: 페이지 수·텍스트 경계·입력 파일 SHA-256 기록

## 반영한 실물 설명

2026-10-26 사용자가 설명한 전원 구성을 근거로 한다.

1. DriveMCU 측에서 12V를 스텝다운하여 5V를 만들고 MCU와 센서에 공급한다.
2. 같은 5V 계통에서 ImageSenderMCU에도 공급한다.
3. XH60은 사용자가 단자라고 설명했고 도면에서 생략해도 된다고 했다. 따라서 전원 경로만 표시했다.
4. JC1060은 별도 전원 및 제작사 제공 배터리 모듈을 사용한다. 배터리 전압·화학계·충전 회로·전환 방식은 추정하지 않았다.

## 자료 및 축약 범위

기존 자료 위치: `C:/Users/work/Desktop/ETC/ARD_software/results/kicad_legacy_261017/`

- 기존 KiCad 7개 시트, PDF, `pinmap.csv`와 `module_bom.csv`를 참조했다.
- DriveMCU의 현재 `board.h`, `sdkconfig`, `gps.cpp`와 센서 소스를 읽어 핀·기능 상태를 반영했다. 과거 수동 버튼 GPIO13/14 및 POT GPIO34는 현재 미사용이므로 주요 신호 표에서 제외했다.
- ImageSenderMCU는 현재 `ARD_ESP-IDF/ImageSenderMCU/main/board_pins.h`의 ESP32-S3 DVP 핀을 사용했다. 별도 P4 카메라 보드를 혼합하지 않았다.
- Robot3AxisMCU는 기존 회로도의 GPIO를 유지하고 표 머리에 '기존 도면'으로 표시했다.
- JC1060의 역할과 PC 경유 구동은 현재 `README.md`, `board.h`, `pc_link.cpp`를 근거로 한다.
- 기존 ControlMCU와 HMI_MCU는 선택 단말로 남겼다. 상세 GPIO는 기존 7장 회로도에 있으므로 이 2장 요약에서는 기능 수준으로 축약했다.
- JC1060의 보드 내부 화면·터치·오디오 배선은 기능 블록으로 축약했다. 내부 전원·배터리 회로를 새로 추정하지 않았다.

## 확인 범위와 한계

- PDF가 2쪽이고 한글·필수 용어가 추출되는지, 페이지 밖 텍스트가 없는지 확인했다. PDF를 PNG로 렌더링하여 배치를 검토했다.
- KiCad 스킬의 Python 분석기를 기존 프로젝트에 실행했다. 결과는 `tmp/schematic_legacy_analysis.json`에 있다.
- 분석기는 데이터시트·MPN 부재(DS-001), 기존 도면의 접지·풀업 표기 부재 등을 지적했다. 기존 도면은 passive 논리 포트와 미확정 전원 단자로 작성된 발표용 자료이므로 이 결과를 실제 하드웨어 결함으로 확정하지 않았다. 물리적 타당성 검증이 아닌 자료 일관성 확인에 사용했다.
- 예전 ERC 0/0 결과를 새 문서의 전기적 검증 결과로 재사용하지 않았다. 새 KiCad 회로도·PCB·제조 파일은 생성하지 않았다.
- 전원선은 공급·귀환을 묶어 기능적으로 표시했다. 단자 순서, GND 실제 배선, 정격, 모터 VMOT, 센서 모듈 레귤레이터·I2C 풀업 전압, 보호회로는 실물 확인 대상이다. 센서 모듈의 5V 공급을 센서 IC나 GPIO에 5V를 직접 연결하라는 뜻으로 해석하면 안 된다.
- 펌웨어·앱·호스트 실행 파일 빌드, SPICE, PCB/ERC/DRC, 실물 전압·전류·동작 측정은 수행하지 않았다.

## 재생성

이 PC의 번들 Python을 사용한다.

```powershell
& 'C:\Users\work\.cache\codex-runtimes\codex-primary-runtime\dependencies\python\python.exe' -X utf8 'C:\Users\work\Desktop\ARD_v1\output\pdf\ard_system_2p\create_document.py'
```
