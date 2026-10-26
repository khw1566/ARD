# DriveMCU ToF 오류값·timeout 처리 수정

대상: `ARD_ESP-IDF/DriveMCU`. 기존 작업 파일의 수정 사항은 유지했다.

## 확인한 소스 결함

- VL53L0X 읽기에서 `requestFrom()` 결과를 검사하지 않았다. 수신 실패 시 `read()`의 -1을 unsigned 데이터로 조합했다.
- 거리만 검사하고 센서의 결과 상태는 검사하지 않았다. 숫자 범위에 들어오는 오류 측정도 평균/PID에 전달했다.
- timeout 후 `tofReady`가 유지되어 재초기화 경로에 들어가지 않았다.
- 오류가 나도 기존 유효 플래그와 평균 필터가 유지되었다. 상태와 거리의 유효성이 어긋났다.

이 결함은 소스에서 확인했다. 현재 실물 증상 전체의 원인이 소프트웨어라는 의미는 아니다.

## 수정

- repeated START 방식으로 레지스터를 읽고 실패를 기록한다. 드라이버 오류를 래치해 초기화가 실패한 뒤 추가 버스 접근을 막는다. 복구 시 래치를 해제한다.
- 런타임은 데이터 준비 비트를 확인하고 다음 태스크 주기로 양보한다. 200 ms 새 데이터가 없거나 I2C 오류가 나면 값을 무효화하고 2초 재시도 주기로 소프트 리셋·재초기화한다.
- 50 ms 측정 예산, 12바이트 결과 읽기, 원시 장치 상태 11과 0 < 거리 < 2000 mm 검사, 3개 중앙값 필터를 적용했다. 오류 뒤에는 연속 정상 측정 3개가 필요하다.
- 오류 시 PID의 Z축 목표를 감속 정지하고 적분을 초기화한다. 복구 시 미분항에 이전 오류값을 사용하지 않는다. PID 진입에도 신선한 정상값을 요구한다.
- TCP/하트비트에 `tof_age_ms`를 추가했다. 하트비트에도 `tof_ok`를 추가했다. 무효 거리는 숫자 호환을 위해 0으로 보내고 OLED에는 N/A를 표시한다. `rail_settled`는 유효 ToF가 있을 때만 true다.
- PID가 이미 켜져 있었다면 센서 복구 후 정상 측정 3개가 쌓이면 다시 제어한다.

ST 원본 API의 원시 상태 설명: https://github.com/adafruit/Adafruit_VL53L0X/blob/master/src/core/src/vl53l0x_api_core.cpp (`VL53L0X_get_pal_range_status`). 원시 상태 11은 ST PAL의 RangeStatus=0과 다르다. 이 경량 포트에는 ST 소프트웨어 sigma 추정기가 없으며, 수정으로 절대 정밀도를 보장하지 않는다.

## 실행한 확인

- Python 소스 계약 점검 10개 통과: 런타임 대기 루프 제거, 세 I2C 단계의 오류 검사, 상태 검사 순서, 오류 후 필터 초기화, 유효 기간, PID 진입 조건 등. C++ 실행 검증은 아니다.
- `python ARD_MAIN/tests/test_drive_live.py`: 5개 통과.
- `test_rail_control.py`: 16개 통과. 첫 실행에서는 샌드박스 임시 디렉터리의 파일 교체 권한으로 실패했다. `tempfile.tempdir`를 작업 폴더의 `tmp/tof-python-checks`로 지정한 뒤 모두 통과했다. PC의 가상 보드 테스트이며 펌웨어/실물 검증이 아니다.
- `git diff --check -- ARD_ESP-IDF/DriveMCU`: 통과. CRLF 변환 안내만 출력.
- 빌드, 컴파일, 플래시, 실물 측정은 실행하지 않았다.

## 사용자 빌드 및 확인

프로젝트 루트의 PowerShell에서 실행:

```powershell
& "C:\Users\work\Desktop\ARD_v1\ARD_ESP-IDF\DriveMCU\build.ps1" build
```

보드에 적용한 뒤 PID를 끈 정지 상태에서 알려진 거리의 평평한 표적을 사용해 보고 거리를 비교한다. `status`의 `tof_ok`, `tof_status`, `tof_age_ms`와 `ToF ... raw=... device_status=...` 로그를 함께 확인한다. 오류가 지속되면 이 로그와 실제 표적 거리가 다음 진단에 필요하다. I2C 오류가 모터 구동 때 집중되는지, 정지 중에도 발생하는지 구분한다.
