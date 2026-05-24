# ARD 시스템 개발 진행 보고서 (2026.04.23)

## 1. 개발 주요 마일스톤

### 🚀 Z축 레일 제어 시스템 최적화
- **제어 방식 변경**: 불안정했던 연속 PID 제어를 폐기하고, **"이동-확인-보정"** 방식의 피드백 제어로 전환했습니다.
- **정밀도 향상**: 1mm당 약 43.48스텝(`1000 steps / 23mm`)의 정밀 실측 환경을 구축했습니다.
- **안정화 로직**: 모터 구동 직후 센서 값의 흔들림을 방지하기 위해 300ms의 측정 지연 시간을 도입했으며, 기계적 마찰을 고려한 최소 구동 속도를 확보했습니다.

### 📡 원격 GPS 제어 도입
- **원격 토글**: PC 센터 UI를 통해 DriveMCU의 GPS 모듈 작동 여부를 실시간으로 제어할 수 있습니다.
- **효율성**: 기본 OFF 상태로 동작하며, 필요 시에만 활성화하여 A-GPS 데이터를 동기화합니다.

### 🎨 UI/UX 디자인 복구 및 고도화
- **컴팩트 레이아웃**: 하단 영역에 차트와 로그를 가로로 나란히 배치하여 화면 공간 활용도를 극대화했습니다.
- **HUD 개선**: 3D 시뮬레이션의 각도표를 더욱 전문적이고 빽빽한(Compact) 디자인으로 복구했습니다.

---

## 2. 하드웨어 핀 구성 및 배선도 (Pin Mapping)

### 🧩 DriveMCU (ESP32-WROOM32D)
| 기능 | 핀 번호 (GPIO) | 연결 장치 | 비고 |
| :--- | :--- | :--- | :--- |
| **Step X** | 18 | A4988 STEP | 디스크 모터 X |
| **Dir X** | 19 | A4988 DIR | |
| **Enable X** | 21 | A4988 EN | |
| **Step Y** | 16 | A4988 STEP | 디스크 모터 Y |
| **Dir Y** | 17 | A4988 DIR | |
| **Enable Y** | 5 | A4988 EN | |
| **Step Z** | 25 | A4988 STEP | Z축 레일 모터 |
| **Dir Z** | 26 | A4988 DIR | |
| **Enable Z** | 27 | A4988 EN | |
| **I2C SDA** | 22 | VL53L0X SDA | ToF 거리 센서 |
| **I2C SCL** | 23 | VL53L0X SCL | ToF 거리 센서 |
| **GPS RX** | 32 | NEO-6M TX | UART2 |
| **GPS TX** | 33 | NEO-6M RX | UART2 |

### 📟 HMI_MCU (ESP32 CYD-28)
| 기능 | 핀 번호 (GPIO) | 연결 장치 | 비고 |
| :--- | :--- | :--- | :--- |
| **TFT CS** | 15 | ILI9341 CS | |
| **Touch CS** | 33 | XPT2046 CS | |
| **Touch IRQ** | 36 | XPT2046 IRQ | |
| **I2C SDA** | 22 | Common SDA | |
| **I2C SCL** | 27 | Common SCL | |

---

## 3. 소프트웨어 주요 알고리즘 (Feedback Control)

```cpp
// Z-Axis Step & Check Logic
if (stepperZ.distanceToGo() == 0) { // 정지 확인
    if (millis() - lastUpdate >= 300) { // 안정화 대기
        float error = target - current;
        if (abs(error) > 1.5) { // 데드존 체크
            long moveSteps = (long)(error * (1000.0 / 23.0));
            stepperZ.move(moveSteps); // 비례 이동
        }
    }
}
```

---

## 4. 향후 계획
- **ToF 필터 가변화**: 환경에 따라 이동 평균 필터 샘플 수 조절 기능 검토.
- **감속비 정밀 측정**: 디스크 기어의 실측 감속비를 재확인하여 시뮬레이션 일치도 향상.
