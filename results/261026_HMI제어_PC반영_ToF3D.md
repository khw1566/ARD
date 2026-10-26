작성일: 2026-10-27
작성자: Claude

# HMI 제어가 PC 프로그램에 반영되지 않는 문제, ToF 기반 3D 레일

## 원인 (경로별)

PC 화면의 모터 위치(`motor_state`)는 보드 실제값이 아니었다. PC가 `/api/drivemcu/command`로 보낸 명령을 ACK 때 더하는 추정값이었다. 그래서 이 경로를 거치지 않은 움직임은 화면에 나타나지 않았다.

| 경로 | 보드까지 가는 길 | 이전 반영 여부 |
|---|---|---|
| JC1060 버튼(레일 ±, PID, home 등) | PC `/api/drivemcu/command` | 반영(명령 추정) |
| JC1060 조이스틱 조그 `j…` | PC `/api/jc1060/drive` | 조그 중 0.5 s마다, 종료 1.5 s 뒤에만 차이를 더함. 늦고 누락 가능 |
| HMI_MCU(CYD) | PC `/api/drivemcu/command` | 반영(명령 추정) |
| ControlMCU 조이스틱 `/api/control/update` | PC가 TCP 5002로 직접 전송 | **반영 안 됨** |
| AI 도구·드라이브 시퀀스(`send_tcp_command_async`) | PC가 TCP 5002로 직접 전송 | **반영 안 됨** |
| DriveMCU 시리얼 콘솔, 레일 PID 자체 이동 | 보드 내부 | **반영 안 됨** |
| 3D 레일 위치 | Z 명령 스텝 추정 | ToF 실측 미사용 |

JC1060과 HMI_MCU는 DriveMCU TCP 5002에 직접 연결하지 않는다. 모든 명령이 PC를 거친다.

## 수정

1. **보드 위치가 기준값** (`backend/core/drive_live.py`, `backend/app.py`)
   - drive_live(약 300 ms마다 DriveMCU `status`)가 `x`, `y`, `z`, `boot_id`, `stopped`도 읽는다.
   - 위치가 바뀌면 `apply_board_position()`이 `motor_state`(스텝·각도·roll·yaw, PC 감속비 사용)를 보드값으로 바꾸고 SSE `drive_update`(source=board)를 보낸다. 어느 경로로 움직였든 PC 화면·3D·HUD·JC1060의 위치가 보드를 따른다.
   - 보드 상태가 최신(2 s 이내)이면 `update_motor_state()`는 위치를 더하지 않는다(이중 계산 방지). `mzt`/`mzm` 설정 반영은 그대로다. 보드 상태가 끊기면 이전처럼 명령 추정으로 돌아간다.
   - JC1060 조그 중에는 조그 경로가 0.5 s마다 읽는 status를 그대로 반영한다. 조그를 놓으면 다음 폴링(약 0.3 s)이 최종 위치를 읽어 반영한다.
2. **3D 레일 = ToF** (`frontend/static/js/simulation-drive.js`, `main.js`, `templates/index.html`)
   - drive_live의 ToF가 정상이면, 티칭값으로 설계 mm로 변환해 3D 레일 캐리지를 움직인다. 중앙 → 0, +측 → +15, -측 → -15이고, 범위 밖은 연장하며 ±25 mm로 제한한다.
   - 위치 미티칭: `(ToF - 중앙) × rail_dir`. ToF 이상: 보드 `z / steps_per_mm`("스텝 추정").
   - HUD "Rail ToF"에 ToF 원값과 "레일 ○ mm · ToF / ToF (위치 미티칭) / 스텝 추정"을 표시한다.
   - 선별 화면 3D도 같은 모델이며, 최신 ToF가 있으면 ToF가 우선한다.

## 시험 (빌드 없음)

- `python -m unittest discover` (ARD_MAIN/tests): 225개 OK.
  - 추가: 보드 위치 발행, 조그 중 status 발행과 해제 후 최종 읽기, 보드값으로 `motor_state` 교체, 이중 계산 없음, 보드 상태 없을 때 기존 추정.
- `node tests/*.cjs`: 5개 OK(`test_drive_model_geometry.cjs`는 three.js 경로 인자가 필요해 제외).
  - 추가: ToF → 레일 변환(티칭, 미티칭, ToF 이상), drive_live만으로 3D 레일 이동(명령 전송 없음).
- 컴파일·실기 확인은 하지 않았다.

## 사용자 작업

- PC 서버(ARD_MAIN) 재시작 후 브라우저 새로고침. 이것만으로 적용된다.
- 펌웨어 재빌드 불필요: DriveMCU, JC1060, HMI_MCU 모두 이번 변경에서 바뀐 것이 없다. JC1060 화면의 "PC estimate" 줄도 이제 보드 실제 위치를 보여준다(문구만 예전 그대로). 이전 작업(선별 단계, 색상 등)의 JC1060 변경은 여전히 빌드가 필요하다.
