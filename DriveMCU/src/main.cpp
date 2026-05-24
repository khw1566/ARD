#include <Arduino.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <Preferences.h>
#include <AccelStepper.h>
#include <TinyGPSPlus.h>
#include <VL53L0X.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <Adafruit_Sensor.h>
#include <Adafruit_AHTX0.h>
#include <Adafruit_BMP280.h>
#include <time.h>
#include "esp_sntp.h"
#include <BluetoothSerial.h>

// --- Forward Declarations ---
void sendLogAsync(String msg);
void updateUrls();
void processCommand(String command);
void syncGPSTimePos();
void moveMotorQueued(long xSteps, long ySteps, char type);
void sendHeartbeat();
void handleWifiConfigMode();
void printHelp();
void updateOLED();
String readSerialLine();

// --- Configuration & Storage ---
Preferences prefs;
// --- WiFi Configuration (Fallback Support) ---
const char* WIFI_SSID_1 = "1-309";
const char* WIFI_PASS_1 = "0522300751";
const char* WIFI_SERVER_1 = "192.168.0.35";

const char* WIFI_SSID_2 = "A35_m";
const char* WIFI_PASS_2 = "00000000";
const char* WIFI_SERVER_2 = "10.234.216.12";

const char* WIFI_SSID_3 = "YOUR_WIFI_SSID";
const char* WIFI_PASS_3 = "YOUR_WIFI_PASSWORD";
const char* WIFI_SERVER_3 = "192.168.0.13";

uint32_t WIFI_TIMEOUT_MS = 5000; // 5 seconds per attempt

String ssid_active = "";
String pass_active = "";
String server_ip_pref = ""; 
String server_ip = "192.168.0.35"; // Active server IP
int targetLastByte = 150; 

String serverUrl;

// --- Pins for A4988 (ESP32-WROOM32D) ---
// X-axis
#define STEP_PIN_X 18
#define DIR_PIN_X  19
#define EN_PIN_X   21

// Y-axis
#define STEP_PIN_Y 16
#define DIR_PIN_Y  17
#define EN_PIN_Y   5

// Z-axis (Rail)
#define STEP_PIN_Z 25
#define DIR_PIN_Z  26
#define EN_PIN_Z   27

#define I2C_SDA 22
#define I2C_SCL 23

// --- Pins for 0.96" OLED (SSD1306) ---
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_RESET    -1 
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

// --- Environment Sensors (AHT20 + BMP280) ---
Adafruit_AHTX0 aht;
Adafruit_BMP280 bmp;
int oledPage = 0;
unsigned long lastPageSwitch = 0;

// --- Passive Buzzer Configuration (GPIO 4) ---
#define BUZZER_PIN 4
int buzzerVolume = 64; // Default volume for UI/Commands (0-255)
int heartbeatVolume = 20; // Dedicated lower volume for background heartbeat (0-255)
void playMelody(int type);
void checkTcpCommand();
void networkTask(void *pvParameters);
void heartbeatTask(void *pvParameters);
void logTask(void *pvParameters);
void buzzerTask(void *pvParameters);
void inputTask(void *pvParameters);

// --- Pins for NEO-6M GPS ---
// [주의] GPIO 0은 부팅 핀입니다. GPS TX가 연결되어 있으면 부팅이 안 될 수 있습니다. 
// 가급적 GPIO 32(RX), 33(TX) 또는 GPIO 13(RX), 14(TX) 사용을 권장합니다.
#define GPS_RX_PIN 32  // (현재 코드상의 핀, 하드웨어 연결과 일치해야 함!)
#define GPS_TX_PIN 33
#define GPS_BAUD   9600
#define GPS_DEBUG_MODE false // 통신 확인 완료되어 RAW 로그 끔
TinyGPSPlus gps;
HardwareSerial gpsSerial(2); // UART2

// --- AccelStepper Instances ---
AccelStepper stepperX(AccelStepper::DRIVER, STEP_PIN_X, DIR_PIN_X);
AccelStepper stepperY(AccelStepper::DRIVER, STEP_PIN_Y, DIR_PIN_Y);
AccelStepper stepperZ(AccelStepper::DRIVER, STEP_PIN_Z, DIR_PIN_Z);

VL53L0X sensor;
float lastDistance = 0; // Changed to float for filtered data
float filteredDistance = 0; // EMA filtered value
uint32_t heartbeatInterval = 1000; // 1 second default for fast UI updates
uint32_t i2cClockSpeed = 100000; // default 100kHz Standard Mode

SemaphoreHandle_t motorMutex;
QueueHandle_t logQueue;

struct MotorAction {
    long stepsX;
    long stepsY;
    long stepsZ;
    char axis; // 'x', 'y', 'z', 'b'(both x/y)
};
struct BuzzerAction {
    int type;
};
QueueHandle_t commandQueue;
QueueHandle_t buzzerQueue;
uint32_t interCommandDelay = 0; // Default 0ms
volatile bool stopActive = false; // Emergency Stop Flag

// --- Manual Control Configuration ---
#define SW1_PIN 13 // Left / Prev / Cancel
#define SW2_PIN 14 // Right / Next / Select
#define POT_PIN 34 // Analog Input

enum ManualState { MANUAL_OFF, MANUAL_MENU, MANUAL_CTRL_X, MANUAL_CTRL_Y, MANUAL_CTRL_Z, MANUAL_CTRL_DIFF_FW, MANUAL_CTRL_DIFF_ROT, MANUAL_HOME };
ManualState manualMode = MANUAL_OFF;
int manualMenuIndex = 0; // 0: X, 1: Y, 2: Z, 3: Diff-FW, 4: Diff-Rot, 5: Home, 6: Exit

// Manual Control Gear Ratios (Applied only in Manual Mode)
float manualGearRatioX = 4.0;
float manualGearRatioY = 4.0;
float manualGearRatioZ = 1.0; // Direct drive
long manualBaseStepsX = 0;
long manualBaseStepsY = 0;

struct ButtonStatus {
    int pin;
    bool lastState;
    unsigned long pressTime;
    unsigned long releaseTime;
    int clickCount;
    bool isLongPressing;
};
ButtonStatus btn1 = {SW1_PIN, HIGH, 0, 0, 0, false};
ButtonStatus btn2 = {SW2_PIN, HIGH, 0, 0, 0, false};

void handleButton(ButtonStatus &btn, int &clickEvent, bool &longPressEvent) {
    clickEvent = 0;
    longPressEvent = false;
    
    bool current = digitalRead(btn.pin);
    unsigned long now = millis();
    
    if (current == LOW && btn.lastState == HIGH && (now - btn.releaseTime > 50)) { // Pressed (Debounce 50ms)
        btn.pressTime = now;
        btn.isLongPressing = false;
    } 
    else if (current == HIGH && btn.lastState == LOW && (now - btn.pressTime > 50)) { // Released
        btn.releaseTime = now;
        if (!btn.isLongPressing) {
            btn.clickCount++;
        }
        btn.isLongPressing = false;
    }
    else if (current == LOW && btn.lastState == LOW) { // Holding
        if (!btn.isLongPressing && (now - btn.pressTime > 600)) { // 600ms for Long Press
            btn.isLongPressing = true;
            longPressEvent = true;
            btn.clickCount = 0;
        }
    }
    
    // Click event generated if no new press within 250ms
    if (current == HIGH && btn.clickCount > 0 && (now - btn.releaseTime > 250)) {
        clickEvent = btn.clickCount;
        btn.clickCount = 0;
    }
    
    btn.lastState = current;
}

// --- Global States ---
int motorSpeedX = 1000;
int motorAccelX = 500;
int motorSpeedY = 1000;
int motorAccelY = 500;
int motorSpeedZ = 1000;
int motorAccelZ = 1000;

// PID Rail Control Variables
bool usePIDRail = false;
int targetRailDist = 140; 
float stepsPerMmZ = 43.48; // Physical conversion factor (Steps/mm)
float pid_p = 0.3; // P-Gain (Dimensionless when used with stepsPerMmZ)
float pid_i = 0.01; 
float pid_d = 5.0;  
float integralRail = 0;
float lastRailError = 0;
unsigned long lastPIDTime = 0;
bool enableGPS = false;
String tofStatus = "Ready";

// --- Motor Control Task (Core 1, High Priority) ---
void motorTask(void *pvParameters) {
    MotorAction nextAction;
    bool waitingForDelay = false;
    unsigned long delayStartTime = 0;

    while (true) {
        if (xSemaphoreTake(motorMutex, portMAX_DELAY)) {
            // Check if motors are currently moving
            bool isXMoving = stepperX.distanceToGo() != 0;
            bool isYMoving = stepperY.distanceToGo() != 0;

            if (!isXMoving && !isYMoving) {
                // Both motors are stopped, check for next action
                if (stopActive) {
                    waitingForDelay = false; // Reset if stop was triggered
                }

                if (!waitingForDelay) {
                    if (xQueueReceive(commandQueue, &nextAction, 0)) {
                        if (interCommandDelay > 0) {
                            waitingForDelay = true;
                            delayStartTime = millis();
                        } else {
                            // Execute immediately (if stop not active)
                            if (!stopActive) {
                                if (nextAction.axis == 'x' || nextAction.axis == 'b') {
                                    digitalWrite(EN_PIN_X, LOW);
                                    stepperX.move(nextAction.stepsX);
                                }
                                if (nextAction.axis == 'y' || nextAction.axis == 'b') {
                                    digitalWrite(EN_PIN_Y, LOW);
                                    stepperY.move(nextAction.stepsY);
                                }
                                if (nextAction.axis == 'z') {
                                    digitalWrite(EN_PIN_Z, LOW);
                                    stepperZ.move(nextAction.stepsZ);
                                }
                            }
                        }
                    }
                } else {
                    // Waiting for configured inter-command delay
                    if (millis() - delayStartTime >= interCommandDelay || stopActive) {
                        bool wasStopped = stopActive;
                        waitingForDelay = false;
                        
                        // Execute now ONLY if not stopped during wait
                        if (!wasStopped) {
                            if (nextAction.axis == 'x' || nextAction.axis == 'b') {
                                digitalWrite(EN_PIN_X, LOW);
                                stepperX.move(nextAction.stepsX);
                            }
                            if (nextAction.axis == 'y' || nextAction.axis == 'b') {
                                digitalWrite(EN_PIN_Y, LOW);
                                stepperY.move(nextAction.stepsY);
                            }
                            if (nextAction.axis == 'z') {
                                digitalWrite(EN_PIN_Z, LOW);
                                stepperZ.move(nextAction.stepsZ);
                            }
                        }
                    }
                }
            }

            stepperX.run();
            stepperY.run();
            stepperZ.run();
            
            // Enhanced Real-time PID Feedback Logic for Rail
            if (usePIDRail) {
                unsigned long now = millis();
                
                // 50ms 주기로 실시간 제어 (움찔거림 방지 및 매끄러운 이동)
                if (now - lastPIDTime >= 50) {
                    float dt = (now - lastPIDTime) / 1000.0f;
                    lastPIDTime = now;
                    
                    if (lastDistance > 0 && lastDistance < 2000) { 
                        float error = (float)targetRailDist - (float)lastDistance;
                        
                        // 데드존(허용 오차): ±2.0mm 이내면 정지
                        if (abs(error) <= 2.0) {
                            if (stepperZ.distanceToGo() != 0) {
                                if (xSemaphoreTake(motorMutex, 0)) {
                                    stepperZ.stop();
                                    xSemaphoreGive(motorMutex);
                                }
                            }
                            integralRail = 0; 
                            lastRailError = error;
                        } else {
                            // PID 계산 (물리 계수 stepsPerMmZ 반영)
                            float current_p = pid_p;
                            if (abs(error) < 3.0) current_p *= 0.5; // Soft Landing
                            
                            float p_term = error * stepsPerMmZ * current_p;
                            
                            if (abs(error) < 10.0) { 
                                integralRail += error * dt;
                                integralRail = constrain(integralRail, -50, 50);
                            } else {
                                integralRail = 0;
                            }
                            float i_term = integralRail * stepsPerMmZ * pid_i;
                            
                            float d_term = ((error - lastRailError) / dt) * stepsPerMmZ * pid_d * 0.01;
                            lastRailError = error;
                            
                            long moveDelta = (long)(p_term + i_term + d_term);
                            
                            if (moveDelta != 0) {
                                digitalWrite(EN_PIN_Z, LOW);
                                stepperZ.moveTo(stepperZ.currentPosition() + moveDelta);
                            }
                        }
                    }
                }
            } else {
                // PID 미사용 시 에러 상태 및 적분값 초기화
                lastRailError = 0;
                integralRail = 0;
            }
            
            xSemaphoreGive(motorMutex);
        }
        
        vTaskDelay(1); // Precision stepping still possible with tight loop inside mutex period if needed
    }
}

void updateOLED() {
    if (manualMode != MANUAL_OFF) {
        // Manual Control UI Override
        display.clearDisplay();
        display.setTextSize(1);
        display.setTextColor(SSD1306_WHITE);
        display.setCursor(0, 0);
        
        if (manualMode == MANUAL_MENU) {
            display.setTextColor(SSD1306_BLACK, SSD1306_WHITE);
            display.println(" [ MANUAL SELECT ] ");
            display.setTextColor(SSD1306_WHITE);
            
            const char* items[] = {"X-Axis (Servo)", "Y-Axis (Servo)", "Z-Axis (Distance)", "Diff-Drive (FW)", "Diff-Drive (ROT)", "Go Home", "Exit"};
            for(int i=0; i<7; i++) {
                display.setCursor(0, 11 + (i * 9));
                if (manualMenuIndex == i) display.print("> ");
                else display.print("  ");
                display.print(items[i]);
            }
        } else {
            display.setTextColor(SSD1306_BLACK, SSD1306_WHITE);
            if (manualMode == MANUAL_CTRL_X) display.println(" [ CTRL: X-AXIS ] ");
            else if (manualMode == MANUAL_CTRL_Y) display.println(" [ CTRL: Y-AXIS ] ");
            else if (manualMode == MANUAL_CTRL_Z) display.println(" [ CTRL: Z-AXIS ] ");
            else if (manualMode == MANUAL_CTRL_DIFF_FW) display.println(" [ CTRL: DIFF-FW ] ");
            else if (manualMode == MANUAL_CTRL_DIFF_ROT) display.println(" [ CTRL: DIFF-ROT ] ");
            display.setTextColor(SSD1306_WHITE);
            
            // This part will be updated dynamically in loop() for real-time responsiveness.
            // We just draw the static frame here.
            display.setCursor(0, 20);
            display.println("Target: ");
            display.setCursor(0, 35);
            display.println("Real  : ");
            
            display.setCursor(0, 55);
            display.println("LONG PRESS TO EXIT");
        }
        display.display();
        return; // Skip normal page rendering
    }

    if (millis() - lastPageSwitch > 8000) { // Extended to 8 seconds
        oledPage = (oledPage + 1) % 3;
        lastPageSwitch = millis();
    }

    display.clearDisplay();
    display.setTextSize(1);
    display.setTextColor(SSD1306_WHITE);
    
    struct tm timeinfo;
    bool timeValid = getLocalTime(&timeinfo);

    // Header (Show Time HH:MM:SS here)
    display.setCursor(0,0);
    if (timeValid) {
        display.printf("[%d/3] %02d:%02d:%02d", oledPage + 1, timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec);
    } else {
        display.printf("ARD [%d/3] ", oledPage + 1);
    }
    
    // IP last octet
    display.setCursor(95, 0); 
    if (WiFi.status() == WL_CONNECTED) {
        String ip = WiFi.localIP().toString();
        display.print("." + ip.substring(ip.lastIndexOf('.') + 1));
    } else {
        display.print("OFFLINE");
    }
    display.drawLine(0, 9, 128, 9, SSD1306_WHITE);
    
    switch(oledPage) {
        case 0: // Network & GPS
            display.setCursor(0, 12);
            display.printf("SSID: %s", ssid_active.length() > 0 ? ssid_active.c_str() : "N/A");
            display.setCursor(0, 22);
            display.printf("SRV : %s", server_ip.c_str());
            display.setCursor(0, 32);
            display.printf("IP  : %s", WiFi.localIP().toString().c_str());
            display.setCursor(0, 42);
            display.print("GPS : ");
            if (enableGPS) {
                if (gps.location.isValid()) display.printf("FIX (%u SAT)", gps.satellites.value());
                else display.println("SEARCHING...");
            } else display.println("DISABLED");
            break;
            
        case 1: // Motion & ToF
            display.setCursor(0, 12);
            display.printf("X:%.1f Y:%.1f deg", (float)stepperX.currentPosition()*1.8, (float)stepperY.currentPosition()*1.8);
            display.setCursor(0, 22);
            display.printf("Z TGT: %d mm", targetRailDist);
            display.setCursor(0, 32);
            display.printf("ToF : %.1f mm", lastDistance);
            display.setCursor(0, 42);
            display.printf("Sts : %s", tofStatus.c_str());
            break;
            
        case 2: // Environment
            display.setCursor(0, 12);
            if (timeValid) {
                // Show Date YYYY-MM-DD instead of full time
                display.printf("DATE: %04d-%02d-%02d", timeinfo.tm_year + 1900, timeinfo.tm_mon + 1, timeinfo.tm_mday);
            } else display.println("DATE: SYNCING...");
            
            // Read AHT20
            sensors_event_t humidity, temp;
            aht.getEvent(&humidity, &temp);
            
            display.setCursor(0, 22);
            display.printf("TEMP: %.1f C", temp.temperature);
            display.setCursor(0, 32);
            display.printf("HUMI: %.1f %%", humidity.relative_humidity);
            display.setCursor(0, 42);
            display.printf("PRES: %.1f hPa", bmp.readPressure() / 100.0F);
            break;
            
            display.setCursor(0, 42);
            display.printf("PRES: %.1f hPa", bmp.readPressure() / 100.0F);
            break;
    }
    
    // Footer / RSSI
    display.drawLine(0, 52, 128, 52, SSD1306_WHITE);
    display.setCursor(0, 55);
    display.printf("RSSI:%d dBm  V:%d", WiFi.RSSI(), buzzerVolume);
    
    display.display();
}

// --- Buzzer Task (Core 0) ---
void buzzerTask(void *pvParameters) {
    BuzzerAction action;
    while (true) {
        if (xQueueReceive(buzzerQueue, &action, portMAX_DELAY)) {
            int type = action.type;
            if (buzzerVolume <= 0 && (type != 1 || heartbeatVolume <= 0)) continue;
            
            switch(type) {
                case 1: // Heartbeat Success
                    ledcWriteTone(0, 1000); ledcWrite(0, heartbeatVolume); vTaskDelay(pdMS_TO_TICKS(10));
                    ledcWrite(0, 0);
                    break;
                case 2: // Boot Start
                    ledcWriteTone(0, 523); ledcWrite(0, buzzerVolume); vTaskDelay(pdMS_TO_TICKS(100));
                    ledcWriteTone(0, 659); vTaskDelay(pdMS_TO_TICKS(100));
                    ledcWriteTone(0, 783); vTaskDelay(pdMS_TO_TICKS(100));
                    ledcWriteTone(0, 1046); vTaskDelay(pdMS_TO_TICKS(200));
                    ledcWrite(0, 0);
                    break;
                case 3: // Emergency Stop
                    for(int i=0; i<4; i++) {
                        ledcWriteTone(0, 1500); ledcWrite(0, buzzerVolume); vTaskDelay(pdMS_TO_TICKS(80));
                        ledcWrite(0, 0); vTaskDelay(pdMS_TO_TICKS(40));
                    }
                    break;
                case 4: // Connection Loss
                    for(int i=0; i<2; i++) {
                        ledcWriteTone(0, 800); ledcWrite(0, buzzerVolume); vTaskDelay(pdMS_TO_TICKS(200));
                        ledcWriteTone(0, 400); vTaskDelay(pdMS_TO_TICKS(200));
                    }
                    ledcWrite(0, 0);
                    break;
                case 5: // Command Received
                    ledcWriteTone(0, 2000); ledcWrite(0, buzzerVolume); vTaskDelay(pdMS_TO_TICKS(50));
                    ledcWrite(0, 0); vTaskDelay(pdMS_TO_TICKS(30));
                    ledcWriteTone(0, 2500); ledcWrite(0, buzzerVolume); vTaskDelay(pdMS_TO_TICKS(50));
                    ledcWrite(0, 0);
                    break;
                case 6: // Menu Scroll
                    ledcWriteTone(0, 3000); ledcWrite(0, buzzerVolume); vTaskDelay(pdMS_TO_TICKS(15));
                    ledcWrite(0, 0);
                    break;
                case 7: // Menu Select/Enter
                    ledcWriteTone(0, 1200); ledcWrite(0, buzzerVolume); vTaskDelay(pdMS_TO_TICKS(30));
                    ledcWriteTone(0, 1800); vTaskDelay(pdMS_TO_TICKS(40));
                    ledcWrite(0, 0);
                    break;
                case 8: // Menu Cancel/Exit
                    ledcWriteTone(0, 800); ledcWrite(0, buzzerVolume); vTaskDelay(pdMS_TO_TICKS(30));
                    ledcWriteTone(0, 500); vTaskDelay(pdMS_TO_TICKS(50));
                    ledcWrite(0, 0);
                    break;
            }
        }
    }
}

void playMelody(int type) {
    BuzzerAction action = {type};
    if (buzzerQueue != NULL) {
        xQueueSend(buzzerQueue, &action, 0);
    }
}

void updateUrls() {
    serverUrl = "http://" + server_ip + ":5000";
}

// --- TCP Configuration ---
WiFiServer server(5002); 

// Serial Line Reader
String readSerialLine() {
    String input = "";
    while (true) {
        if (Serial.available()) {
            char c = Serial.read();
            if (c == '\n' || c == '\r') {
                delay(50);
                while (Serial.available() && (Serial.peek() == '\n' || Serial.peek() == '\r')) {
                    Serial.read();
                }
                input.trim();
                return input;
            }
            input += c;
        }
        delay(10);
    }
}

void handleWifiConfigMode();
void printHelp();
void sendLog(String msg);

void sendUBX(uint8_t* msg, int len) {
    for (int i = 0; i < len; i++) {
        gpsSerial.write(msg[i]);
    }
}

void syncGPSTimePos() {
    if (WiFi.status() != WL_CONNECTED) return;
    
    Serial.println("  [A-GPS] Syncing Time/Pos from Server...");
    HTTPClient http;
    http.begin(serverUrl + "/api/time");
    http.setTimeout(500); // 500ms 타임아웃 제한 (모터 스태터 방지)
    int httpCode = http.GET();
    
    if (httpCode == 200) {
        String payload = http.getString();
        StaticJsonDocument<200> doc;
        deserializeJson(doc, payload);
        
        uint16_t week = doc["week"];
        uint32_t tow = doc["tow_ms"];
        
        Serial.printf("  [A-GPS] Server Time: Week %u, TOW %u\n", week, tow);
        
        // UBX-AID-INI Message (48-byte payload)
        uint8_t ubx_aid_ini[56] = {
            0xB5, 0x62, // Header
            0x0B, 0x01, // Class/ID
            48, 0,      // Length (48)
            // Payload
            0,0,0,0,    // ECEF X (Ignore if cfgPos=0)
            0,0,0,0,    // ECEF Y
            0,0,0,0,    // ECEF Z
            0,0,0,0,    // Pos Accuracy
            0x02, 0x00, // Cfg (bit 1 = time only for now to be safe)
            (uint8_t)(week & 0xFF), (uint8_t)((week >> 8) & 0xFF),
            (uint8_t)(tow & 0xFF), (uint8_t)((tow >> 8) & 0xFF), (uint8_t)((tow >> 16) & 0xFF), (uint8_t)((tow >> 24) & 0xFF),
            0,0,0,0,    // towns
            0xE8, 0x03, 0, 0, // tAcc (1000ms)
            0,0,0,0,    // fCfg
            0,0,0,0,    // fAcc
            0,0         // CK_A, CK_B (placeholder)
        };
        
        // Calculate Checksum over bytes 2..53
        uint8_t ck_a = 0, ck_b = 0;
        for (int i = 2; i < 54; i++) {
            ck_a += ubx_aid_ini[i];
            ck_b += ck_a;
        }
        ubx_aid_ini[54] = ck_a;
        ubx_aid_ini[55] = ck_b;
        
        sendUBX(ubx_aid_ini, 56);
        Serial.println("  [A-GPS] AssistNow (Time) Sent to GPS.");
        sendLogAsync("A-GPS (Time) Synced from PC");
    } else {
        sendLogAsync("A-GPS Sync Failed: HTTP " + String(httpCode));
    }
    http.end();
}

void sendLog(String msg) {
    if (WiFi.status() != WL_CONNECTED) return;
    HTTPClient http;
    http.begin(serverUrl + "/api/log");
    http.setTimeout(500);
    http.addHeader("Content-Type", "application/json");
    StaticJsonDocument<200> doc;
    doc["source"] = "DriveMCU";
    doc["message"] = msg;
    String payload;
    serializeJson(doc, payload);
    int httpCode = http.POST(payload);
    if (httpCode <= 0 || httpCode != 200) {
        Serial.println("  [HTTP] Log failed to " + serverUrl + " | Code: " + String(httpCode) + " (" + http.errorToString(httpCode) + ")");
    }
    http.end();
}

void sendHeartbeat() {
    if (WiFi.status() != WL_CONNECTED) return;
    HTTPClient http;
    http.begin(serverUrl + "/api/heartbeat");
    http.setTimeout(1000); // 1s timeout for safety
    http.addHeader("Content-Type", "application/json");

    StaticJsonDocument<1024> doc; // handle GPS + ToF + Env info safely
    doc["source"] = "DriveMCU";
    doc["rssi"] = WiFi.RSSI();
    doc["ip"] = WiFi.localIP().toString();
    doc["temp"] = temperatureRead();
    doc["tof_dist"] = lastDistance;
    doc["tof_status"] = tofStatus;

    // Environment Data (AHT20 + BMP280) - Send every 5s for responsiveness
    static unsigned long lastEnvSend = 0;
    static float cachedTemp = 0, cachedHumid = 0, cachedPress = 0;
    
    if (millis() - lastEnvSend > 5000 || lastEnvSend == 0) {
        sensors_event_t humidity, temp;
        if (aht.getEvent(&humidity, &temp)) {
            cachedTemp = temp.temperature;
            cachedHumid = humidity.relative_humidity;
        }
        cachedPress = bmp.readPressure() / 100.0F;
        lastEnvSend = millis();
        
        doc["env_temp"] = cachedTemp;
        doc["env_humid"] = cachedHumid;
        doc["env_press"] = cachedPress;
    }

    if (enableGPS && gps.location.isValid()) {
        doc["gps_lat"] = gps.location.lat();
        doc["gps_lng"] = gps.location.lng();
        Serial.printf("  [GPS] Fix OK! Lat: %f, Lng: %f\n", gps.location.lat(), gps.location.lng());
    } else {
        doc["gps_lat"] = 0.0;
        doc["gps_lng"] = 0.0;
        if (enableGPS) Serial.printf("  [GPS] No Fix yet. Chars: %u... wait\n", gps.charsProcessed());
    }
    String payload;
    serializeJson(doc, payload);
    int httpCode = http.POST(payload);
    if (httpCode == 200) {
        Serial.println("  [HTTP] Heartbeat sent to " + serverUrl);
        playMelody(1); // Heartbeat blip
    } else {
        Serial.println("  [HTTP] Heartbeat failed to " + serverUrl + " | Code: " + String(httpCode) + " (" + http.errorToString(httpCode) + ")");
        // Connection loss warning removed per user request
    }
    http.end();
}

void moveMotorQueued(long stepsX, long stepsY, long stepsZ, char axis) {
    MotorAction action = {stepsX, stepsY, stepsZ, axis};
    if (commandQueue != NULL) {
        if (xQueueSend(commandQueue, &action, 0) != pdPASS) {
            sendLogAsync("Command Queue FULL! Action dropped.");
        }
    }
}

void clearMotorQueue() {
    if (commandQueue != NULL) {
        xQueueReset(commandQueue);
    }
}

void moveMotor(AccelStepper &s, int enPin, long steps, String axis) {
    // Legacy mapping to Queued system (X/Y relative)
    moveMotorQueued((axis == "X" ? steps : 0), (axis == "Y" ? steps : 0), (axis == "Z" ? steps : 0), 
                    (axis == "X" ? 'x' : (axis == "Y" ? 'y' : 'z')));
}

void processCommand(String command) {
    if (command == "") return;
    playMelody(5); // Command Received Sound
    Serial.println("\nExecuting: " + command);
    
    String cmd = command;
    cmd.toLowerCase();

    // Check for explicit axis prefix (mx or my)
    if (cmd.startsWith("mx")) {
        stopActive = false; // Reset stop flag on new move
        String sub = cmd.substring(2);
        if (sub.startsWith("r")) moveMotor(stepperX, EN_PIN_X, sub.substring(1).toInt(), "X");
        else if (sub.startsWith("s")) {
            motorSpeedX = sub.substring(1).toInt();
            if (xSemaphoreTake(motorMutex, portMAX_DELAY)) {
                stepperX.setMaxSpeed(motorSpeedX);
                xSemaphoreGive(motorMutex);
            }
            sendLogAsync("Motor X speed: " + String(motorSpeedX));
        } else if (sub.startsWith("a")) {
            motorAccelX = sub.substring(1).toInt();
            if (xSemaphoreTake(motorMutex, portMAX_DELAY)) {
                stepperX.setAcceleration(motorAccelX);
                xSemaphoreGive(motorMutex);
            }
            sendLogAsync("Motor X accel: " + String(motorAccelX));
        } else if (sub == "x") {
            if (xSemaphoreTake(motorMutex, portMAX_DELAY)) {
                stepperX.stop();
                xSemaphoreGive(motorMutex);
            }
            sendLogAsync("Motor X STOP");
        } else if (sub == "e") {
            digitalWrite(EN_PIN_X, LOW);
            sendLogAsync("Motor X Enabled");
        } else if (sub == "d") {
            digitalWrite(EN_PIN_X, HIGH);
            sendLogAsync("Motor X Disabled");
        }
    } else if (cmd.startsWith("my")) {
        stopActive = false; // Reset stop flag on new move
        String sub = cmd.substring(2);
        if (sub.startsWith("r")) moveMotor(stepperY, EN_PIN_Y, sub.substring(1).toInt(), "Y");
        else if (sub.startsWith("s")) {
            motorSpeedY = sub.substring(1).toInt();
            if (xSemaphoreTake(motorMutex, portMAX_DELAY)) {
                stepperY.setMaxSpeed(motorSpeedY);
                xSemaphoreGive(motorMutex);
            }
            sendLogAsync("Motor Y speed: " + String(motorSpeedY));
        } else if (sub.startsWith("a")) {
            motorAccelY = sub.substring(1).toInt();
            if (xSemaphoreTake(motorMutex, portMAX_DELAY)) {
                stepperY.setAcceleration(motorAccelY);
                xSemaphoreGive(motorMutex);
            }
            sendLogAsync("Motor Y accel: " + String(motorAccelY));
        } else if (sub == "x") {
            if (xSemaphoreTake(motorMutex, portMAX_DELAY)) {
                stepperY.stop();
                xSemaphoreGive(motorMutex);
            }
            sendLogAsync("Motor Y STOP");
        } else if (sub == "e") {
            digitalWrite(EN_PIN_Y, LOW);
            sendLogAsync("Motor Y Enabled");
        } else if (sub == "d") {
            digitalWrite(EN_PIN_Y, HIGH);
            sendLogAsync("Motor Y Disabled");
        }
    } else if (cmd.startsWith("df")) { // Disk Forward (Dual Motor)
        stopActive = false;
        long steps = cmd.substring(2).toInt() ?: 200;
        moveMotorQueued(steps, -steps, 0, 'b');
    } else if (cmd.startsWith("db")) { // Disk Backward (Dual Motor)
        stopActive = false;
        long steps = cmd.substring(2).toInt() ?: 200;
        moveMotorQueued(-steps, steps, 0, 'b');
    } else if (cmd.startsWith("dr")) { // Disk Rotate CW (Dual Motor)
        stopActive = false;
        long steps = cmd.substring(2).toInt() ?: 200;
        moveMotorQueued(steps, steps, 0, 'b');
    } else if (cmd.startsWith("dl")) { // Disk Rotate CCW (Dual Motor)
        stopActive = false;
        long steps = cmd.substring(2).toInt() ?: 200;
        moveMotorQueued(-steps, -steps, 0, 'b');
    } else if (cmd.startsWith("mzp")) {
        pid_p = cmd.substring(3).toFloat();
        sendLogAsync("PID P set: " + String(pid_p));
    } else if (cmd.startsWith("mzi")) {
        pid_i = cmd.substring(3).toFloat();
        sendLogAsync("PID I set: " + String(pid_i));
    } else if (cmd.startsWith("gps")) {
        enableGPS = (cmd.substring(3).toInt() == 1);
        if (enableGPS) syncGPSTimePos();
        sendLogAsync("GPS Module: " + String(enableGPS ? "ON" : "OFF"));
    } else if (cmd.startsWith("mzd")) {
        pid_d = cmd.substring(3).toFloat();
        sendLogAsync("PID D set: " + String(pid_d));
    } else if (cmd.startsWith("mzt")) {
        targetRailDist = cmd.substring(3).toInt();
        sendLogAsync("Target ToF: " + String(targetRailDist));
    } else if (cmd.startsWith("mzc")) {
        i2cClockSpeed = cmd.substring(3).toInt();
        Wire.setClock(i2cClockSpeed);
        sendLogAsync("I2C Clock set: " + String(i2cClockSpeed));
    } else if (cmd.startsWith("mzh")) {
        heartbeatInterval = cmd.substring(3).toInt();
        sendLogAsync("ToF Interval set: " + String(heartbeatInterval));
    } else if (cmd.startsWith("mzm")) {
        usePIDRail = (cmd.substring(3).toInt() == 1);
        if (usePIDRail) integralRail = 0;
        sendLogAsync("Rail PID Mode: " + String(usePIDRail ? "ON" : "OFF"));
    } else if (cmd.startsWith("mzs")) { // Manual Step Set (e.g. mzs45.2)
        stepsPerMmZ = cmd.substring(3).toFloat();
        sendLogAsync("Steps/mm Z set to: " + String(stepsPerMmZ));
    } else if (cmd == "mza") { // Auto Calibrate
        sendLogAsync("Starting Auto Calibration... Moving 1000 steps.");
        float d1 = lastDistance;
        moveMotor(stepperZ, EN_PIN_Z, 1000, "Z");
        vTaskDelay(pdMS_TO_TICKS(2500)); // Wait for move and settle (Non-blocking for other tasks)
        float d2 = lastDistance;
        float diff = abs(d1 - d2);
        if (diff > 2.0) {
            stepsPerMmZ = 1000.0 / diff;
            sendLogAsync("Calibration Done. 1000 steps = " + String(diff) + "mm. New Factor: " + String(stepsPerMmZ));
        } else {
            sendLogAsync("Calibration Failed. Distance change too small.");
        }
    } else if (cmd == "home") {
        if (xSemaphoreTake(motorMutex, portMAX_DELAY)) {
            stepperX.moveTo(0);
            stepperY.moveTo(0);
            xSemaphoreGive(motorMutex);
        }
        targetRailDist = 140; // Set Z to physical home distance
        sendLogAsync("System Homing Initiated from PC.");
    } else if (cmd.startsWith("mz")) {
        stopActive = false;
        String sub = cmd.substring(2);
        if (sub.startsWith("r")) moveMotor(stepperZ, EN_PIN_Z, sub.substring(1).toInt(), "Z");
        else if (sub.startsWith("s")) {
            motorSpeedZ = sub.substring(1).toInt();
            if (xSemaphoreTake(motorMutex, portMAX_DELAY)) {
                stepperZ.setMaxSpeed(motorSpeedZ);
                xSemaphoreGive(motorMutex);
            }
            sendLogAsync("Motor Z speed: " + String(motorSpeedZ));
        } else if (sub.startsWith("a")) {
            motorAccelZ = sub.substring(1).toInt();
            if (xSemaphoreTake(motorMutex, portMAX_DELAY)) {
                stepperZ.setAcceleration(motorAccelZ);
                xSemaphoreGive(motorMutex);
            }
            sendLogAsync("Motor Z accel: " + String(motorAccelZ));
        } else if (sub == "x") {
            if (xSemaphoreTake(motorMutex, portMAX_DELAY)) {
                stepperZ.stop();
                xSemaphoreGive(motorMutex);
            }
            sendLogAsync("Motor Z STOP");
        }
    } else if (cmd.startsWith("b") && cmd.indexOf(',') > 0) { // Custom 'Both' move: b<stepsX>,<stepsY>
        stopActive = false;
        int commaIndex = cmd.indexOf(',');
        long stepsX = cmd.substring(1, commaIndex).toInt();
        long stepsY = cmd.substring(commaIndex + 1).toInt();
        moveMotorQueued(stepsX, stepsY, 0, 'b');
    } else {
        // Legacy/Short commands or Global commands
        if (cmd.startsWith("r")) {
            stopActive = false;
            moveMotor(stepperX, EN_PIN_X, cmd.substring(1).toInt(), "X");
        } else if (cmd.startsWith("s")) {
            motorSpeedX = cmd.substring(1).toInt();
            if (xSemaphoreTake(motorMutex, portMAX_DELAY)) {
                stepperX.setMaxSpeed(motorSpeedX);
                xSemaphoreGive(motorMutex);
            }
            sendLogAsync("Motor X speed: " + String(motorSpeedX));
        } else if (cmd.startsWith("a")) {
            motorAccelX = cmd.substring(1).toInt();
            if (xSemaphoreTake(motorMutex, portMAX_DELAY)) {
                stepperX.setAcceleration(motorAccelX);
                xSemaphoreGive(motorMutex);
            }
            sendLogAsync("Motor X accel: " + String(motorAccelX));
        } else if (cmd == "x" || cmd == "stop") {
            stopActive = true; 
            clearMotorQueue(); 
            if (xSemaphoreTake(motorMutex, portMAX_DELAY)) {
                stepperX.setCurrentPosition(stepperX.currentPosition());
                stepperY.setCurrentPosition(stepperY.currentPosition());
                stepperZ.setCurrentPosition(stepperZ.currentPosition());
                xSemaphoreGive(motorMutex);
            }
            digitalWrite(EN_PIN_X, HIGH); 
            digitalWrite(EN_PIN_Y, HIGH);
            digitalWrite(EN_PIN_Z, HIGH);
            sendLogAsync("ALL Motors EMERGENCY STOP & Queue Cleared");
            playMelody(3); // Emergency Alert
        } else if (cmd.startsWith("md")) { 
            interCommandDelay = cmd.substring(2).toInt();
            sendLogAsync("Inter-command Delay: " + String(interCommandDelay) + "ms");
        } else if (cmd.startsWith("bv")) { // Buzzer Volume
            buzzerVolume = cmd.substring(2).toInt();
            ledcWrite(0, buzzerVolume);
            sendLogAsync("Buzzer Volume: " + String(buzzerVolume));
        } else if (cmd == "e" || cmd == "enable") {
            digitalWrite(EN_PIN_X, LOW);
            digitalWrite(EN_PIN_Y, LOW);
            digitalWrite(EN_PIN_Z, LOW);
            sendLogAsync("ALL Motors Enabled");
        } else if (cmd == "d" || cmd == "disable") {
            digitalWrite(EN_PIN_X, HIGH);
            digitalWrite(EN_PIN_Y, HIGH);
            digitalWrite(EN_PIN_Z, HIGH);
            sendLogAsync("ALL Motors Disabled");
        } else if (cmd == "w") {
            handleWifiConfigMode();
        } else if (cmd == "h" || cmd == "?") {
            printHelp();
        }
    }
}

// Separate Network Task (Core 0) to prevent command skipping
void networkTask(void *pvParameters) {
    static String serialBuf = "";
    while (true) {
        checkTcpCommand();
        
        // Handle GPS Parsing (moved from loop)
        if (enableGPS) {
            while (gpsSerial.available() > 0) {
                char c = gpsSerial.read();
                gps.encode(c);
            }
        } else {
            while (gpsSerial.available() > 0) gpsSerial.read();
        }

        // Handle Serial commands
        while (Serial.available()) {
            char c = Serial.read();
            if (c == '\n' || c == '\r') {
                serialBuf.trim();
                if (serialBuf.length() > 0) {
                    processCommand(serialBuf);
                    serialBuf = "";
                    Serial.print("\nDriveMCU> ");
                }
            } else if (c == 8 || c == 127) { // Backspace
                if (serialBuf.length() > 0) {
                    serialBuf.remove(serialBuf.length() - 1);
                    Serial.print("\b \b");
                }
            } else {
                serialBuf += c;
                Serial.print(c);
            }
        }
        vTaskDelay(pdMS_TO_TICKS(10)); // Yield to other Core 0 tasks
    }
}

// Separate asynchronous Logger Task (Core 0)
void logTask(void *pvParameters) {
    char* message;
    while (true) {
        if (xQueueReceive(logQueue, &message, portMAX_DELAY)) {
            sendLog(String(message));
            free(message); // Free the allocated memory
        }
    }
}

void sendLogAsync(String msg) {
    if (logQueue == NULL) return;
    char* msgCopy = strdup(msg.c_str());
    if (msgCopy == NULL) return;
    
    // Non-blocking send, if queue is full, skip (protects motor priority)
    if (xQueueSend(logQueue, &msgCopy, 0) != pdPASS) {
        free(msgCopy);
    }
}

// Separate Heartbeat Task (Core 0) to prevent blocking loop()
void heartbeatTask(void *pvParameters) {
    unsigned long lastHb = 0;
    while (true) {
        if (millis() - lastHb > heartbeatInterval) {
            sendHeartbeat();
            lastHb = millis();
        }
        vTaskDelay(pdMS_TO_TICKS(100)); // Check every 100ms
    }
}

void checkTcpCommand() {
    WiFiClient client = server.available();
    if (!client) return;
    
    unsigned long startWait = millis();
    while (client.connected() && (millis() - startWait < 500)) { // 500ms session limit
        if (client.available()) {
            String request = client.readStringUntil('\n');
            request.trim();
            if (request.length() > 0) {
                processCommand(request);
            }
            startWait = millis(); // Reset timeout on data
        }
        delay(1);
    }
    client.stop();
}

void handleWifiConfigMode() {
    WiFi.disconnect(true);
    delay(500);
    Serial.println("\n\n--- DriveMCU Configuration Mode ---");
    Serial.println("1. Serial Config (W/S/A)");
    Serial.println("2. Bluetooth Config (ARD_Drive_Config)");
    Serial.println("3. Reset & Restart");
    Serial.print("Select (1-3): ");

    BluetoothSerial SerialBT;

    while (true) {
        if (Serial.available()) {
            String choice = readSerialLine();
            if (choice == "1") {
                Serial.println("Enter WIFI,SSID,PASS,IP:");
                String line = readSerialLine();
                if(line.startsWith("WIFI,")) {
                    int p1 = line.indexOf(',', 5);
                    int p2 = line.indexOf(',', p1+1);
                    if(p1>0 && p2>0) {
                        prefs.begin("drive-config", false);
                        prefs.putString("ssid", line.substring(5, p1));
                        prefs.putString("password", line.substring(p1+1, p2));
                        prefs.putString("server_ip", line.substring(p2+1));
                        prefs.end();
                        ESP.restart();
                    }
                }
            } else if (choice == "2") {
                SerialBT.begin("ARD_Drive_Config");
                Serial.println("BT Active: ARD_Drive_Config");
                while(!SerialBT.available()) { delay(10); if(Serial.available()) break; }
            } else if (choice == "3") {
                prefs.begin("drive-config", false); prefs.clear(); prefs.end();
                ESP.restart();
            }
        }
        
        if (SerialBT.hasClient() && SerialBT.available()) {
            String line = SerialBT.readStringUntil('\n');
            line.trim();
            if(line.startsWith("WIFI,")) {
                int p1 = line.indexOf(',', 5);
                int p2 = line.indexOf(',', p1+1);
                if(p1>0 && p2>0) {
                    prefs.begin("drive-config", false);
                    prefs.putString("ssid", line.substring(5, p1));
                    prefs.putString("password", line.substring(p1+1, p2));
                    prefs.putString("server_ip", line.substring(p2+1));
                    prefs.end();
                    SerialBT.println("Saved! Rebooting...");
                    delay(1000);
                    ESP.restart();
                }
            }
        }
        delay(10);
    }
}

void printHelp() {
    Serial.println("\n--- DriveMCU Commands ---");
    Serial.println("  mxr<steps> - Rotate X (e.g., mxr200)");
    Serial.println("  myr<steps> - Rotate Y");
    Serial.println("  mxs<val>   - Max Speed X (e.g., mxs1000)");
    Serial.println("  mys<val>   - Max Speed Y");
    Serial.println("  mxa<val>   - Accel X");
    Serial.println("  mya<val>   - Accel Y");
    Serial.println("  x         - Global Emergency STOP");
    Serial.println("  w         - WiFi/IP Setup Menu");
    Serial.println("  h or ?    - Show this help");
    Serial.print("\nDriveMCU> "); // Prompt
}

void setup() {
    Serial.begin(115200);
    
    // GPS 초기화를 모터 설정보다 먼저 수행하여 UART가 모터 핀을 가로채는 것을 방지
    Serial.println("GPS Initializing...");
    gpsSerial.begin(GPS_BAUD, SERIAL_8N1, GPS_RX_PIN, GPS_TX_PIN);

    pinMode(STEP_PIN_X, OUTPUT);
    pinMode(DIR_PIN_X, OUTPUT);
    pinMode(EN_PIN_X, OUTPUT);
    digitalWrite(EN_PIN_X, LOW);

    pinMode(STEP_PIN_Y, OUTPUT);
    pinMode(DIR_PIN_Y, OUTPUT);
    pinMode(EN_PIN_Y, OUTPUT);
    digitalWrite(EN_PIN_Y, LOW);

    pinMode(STEP_PIN_Z, OUTPUT);
    pinMode(DIR_PIN_Z, OUTPUT);
    pinMode(EN_PIN_Z, OUTPUT);
    digitalWrite(EN_PIN_Z, LOW);

    stepperX.setMaxSpeed(motorSpeedX);
    stepperX.setAcceleration(motorAccelX);
    stepperY.setMaxSpeed(motorSpeedY);
    stepperY.setAcceleration(motorAccelY);
    stepperZ.setMaxSpeed(motorSpeedZ);
    stepperZ.setAcceleration(motorAccelZ);
    stepperZ.setPinsInverted(true, false, false);

    // Initial Buzzer Setup
    ledcSetup(0, 2000, 8);
    ledcAttachPin(BUZZER_PIN, 0);
    ledcWrite(0, 0); // Start silent

    // Initial I2C
    Serial.println("--- Starting I2C Scanner ---");
    Wire.begin(I2C_SDA, I2C_SCL);
    Wire.setClock(100000); // 초기 단계에서 안정적인 속도로 설정
    delay(100); // 센서 안정화 대기
    
    byte error, address;
    int nDevices = 0;
    for(address = 1; address < 127; address++ ) {
        Wire.beginTransmission(address);
        error = Wire.endTransmission();
        if (error == 0) {
            Serial.print("I2C device found at address 0x");
            if (address<16) Serial.print("0");
            Serial.print(address,HEX);
            Serial.println("  !");
            nDevices++;
        }
    }
    if (nDevices == 0) Serial.println("No I2C devices found\n");
    else Serial.println("done\n");

    // Initialize AHT20
    Serial.println("Initializing AHT20...");
    if (!aht.begin()) {
        Serial.println("Could not find AHT20!");
    } else {
        Serial.println("AHT20 found!");
    }

    // Initialize BMP280
    Serial.println("Initializing BMP280...");
    if (!bmp.begin(0x76)) {
        if (!bmp.begin(0x77)) {
            Serial.println("Could not find BMP280! Check wiring/address.");
        } else {
            Serial.println("BMP280 found at 0x77");
        }
    } else {
        Serial.println("BMP280 found at 0x76");
    }

    sensor.setTimeout(500);
    if (!sensor.init()) {
        Serial.println("Failed to detect and initialize VL53L0X!");
        sendLogAsync("ToF Sensor Init FAILED");
        tofStatus = "INIT_FAIL";
    } else {
        tofStatus = "OK";
        sensor.startContinuous();
        Serial.println("VL53L0X ToF Initialized Successfully.");
        sendLogAsync("ToF Sensor Initialized");
    }

    // Initialize OLED
    if(!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) { 
        Serial.println(F("SSD1306 allocation failed"));
    } else {
        Wire.setClock(100000); // 명시적 100kHz 설정 (I2C 안정성 확보)
        display.clearDisplay();
        display.setTextSize(1);
        display.setTextColor(SSD1306_WHITE);
        display.setCursor(0,0);
        display.println("ARD DriveMCU");
        display.println("Initializing...");
        display.display();
    }

    // Load Config
    prefs.begin("drive-config", false);
    if (prefs.isKey("ssid")) ssid_active = prefs.getString("ssid");
    if (prefs.isKey("password")) pass_active = prefs.getString("password");
    if (prefs.isKey("server_ip")) server_ip_pref = prefs.getString("server_ip");
    if (prefs.isKey("targetLastByte")) targetLastByte = prefs.getInt("targetLastByte");
    
    updateUrls();

    // Fallback WiFi Connection Logic
    const char* ssids[] = {ssid_active.c_str(), WIFI_SSID_1, WIFI_SSID_2, WIFI_SSID_3};
    const char* passes[] = {pass_active.c_str(), WIFI_PASS_1, WIFI_PASS_2, WIFI_PASS_3};
    const char* servers[] = {server_ip_pref.c_str(), WIFI_SERVER_1, WIFI_SERVER_2, WIFI_SERVER_3};
    bool connected = false;

    for (int i = 0; i < 4; i++) {
        if (strlen(ssids[i]) == 0) continue;
        
        Serial.printf("\n[WiFi] Attempting connection to %s...\n", ssids[i]);
        WiFi.begin(ssids[i], passes[i]);
        
        unsigned long start = millis();
        while (WiFi.status() != WL_CONNECTED && (millis() - start < WIFI_TIMEOUT_MS)) {
            delay(500);
            Serial.print(".");
        }
        
        if (WiFi.status() == WL_CONNECTED) {
            // Apply paired server IP (if available)
            if (strlen(servers[i]) > 0) {
                server_ip = servers[i];
                Serial.printf("\n[WiFi] Matched Server IP: %s\n", server_ip.c_str());
            }
            updateUrls();
            connected = true;
            break;
        }
    }

    if (connected) {
        Serial.println("\nWiFi Connected: " + WiFi.localIP().toString());
        sntp_set_sync_interval(60 * 1000); // 1분에 한 번만 동기화 (60,000ms)
        configTime(32400, 0, "pool.ntp.org", "time.google.com"); // NTP 시간 설정 (한국 UTC+9)
        server.begin();
        sendLogAsync("DriveMCU Online (IP: " + WiFi.localIP().toString() + ")");
        
        digitalWrite(EN_PIN_X, LOW);
        digitalWrite(EN_PIN_Y, LOW);
        digitalWrite(EN_PIN_Z, LOW);
        
        if (enableGPS) syncGPSTimePos(); // Sync A-GPS data on start if ON
    } else {
        Serial.println("\nWiFi All Connection Failed!");
        handleWifiConfigMode();
    }

    // Initialize Manual Pins
    pinMode(SW1_PIN, INPUT_PULLUP);
    pinMode(SW2_PIN, INPUT_PULLUP);
    pinMode(POT_PIN, INPUT); // ADC
    
    // Initialize Mutex and Queues
    motorMutex = xSemaphoreCreateMutex();
    logQueue = xQueueCreate(20, sizeof(char*)); 
    commandQueue = xQueueCreate(32, sizeof(MotorAction));
    buzzerQueue = xQueueCreate(10, sizeof(BuzzerAction));

    // Start Tasks on Core 0 (System Tasks)
    xTaskCreatePinnedToCore(logTask, "LogTask", 4096, NULL, 5, NULL, 0);
    xTaskCreatePinnedToCore(networkTask, "NetworkTask", 8192, NULL, 10, NULL, 0);
    xTaskCreatePinnedToCore(heartbeatTask, "HeartbeatTask", 4096, NULL, 5, NULL, 0);
    xTaskCreatePinnedToCore(buzzerTask, "BuzzerTask", 2048, NULL, 5, NULL, 0);
    
    // Start Tasks on Core 1 (Performance Critical)
    xTaskCreatePinnedToCore(inputTask, "InputTask", 4096, NULL, 20, NULL, 1);
    xTaskCreatePinnedToCore(motorTask, "MotorTask", 4096, NULL, 24, NULL, 1);

    playMelody(2); // Boot Success Melody
    printHelp();
}

// --- High-Performance Input Task (100Hz) ---
volatile int sharedClick1 = 0, sharedClick2 = 0;
volatile bool sharedLong1 = false, sharedLong2 = false;

void inputTask(void *pvParameters) {
    while (true) {
        int c1 = 0, c2 = 0;
        bool l1 = false, l2 = false;
        handleButton(btn1, c1, l1);
        handleButton(btn2, c2, l2);
        
        if (c1 > 0) sharedClick1 = c1;
        if (c2 > 0) sharedClick2 = c2;
        if (l1) sharedLong1 = true;
        if (l2) sharedLong2 = true;
        
        vTaskDelay(pdMS_TO_TICKS(10)); 
    }
}

void loop() {
    // --- Async Input Consumption ---
    int click1 = sharedClick1; sharedClick1 = 0;
    int click2 = sharedClick2; sharedClick2 = 0;
    bool long1 = sharedLong1; sharedLong1 = false;
    bool long2 = sharedLong2; sharedLong2 = false;
    
    // Motor run is now handled by motorTask on Core 1
    
    // --- Manual Control Logic ---
    static float smoothedADC = -1;
    static int lastADC = -1; 
    
    // 1. Multi-sample averaging for raw noise reduction
    long adcSum = 0;
    for(int i=0; i<8; i++) adcSum += analogRead(POT_PIN);
    int adcRaw = adcSum / 8;
    
    if (smoothedADC < 0) smoothedADC = adcRaw;
    
    // 2. Stronger EMA Filter (Alpha 0.05) to eliminate high-frequency jitter
    smoothedADC = (smoothedADC * 0.95) + (adcRaw * 0.05); 
    
    // 3. Larger Hysteresis (Deadzone): Only update if change is > 25 units (~0.6%)
    // This effectively stops the "buzzing" when the pot is not being turned.
    if (lastADC == -1 || abs((int)smoothedADC - lastADC) > 25) {
        lastADC = (int)smoothedADC;
    }

    if (manualMode == MANUAL_OFF) {
        // 길게 누르면 메뉴 진입
        if (long1 || long2) {
            playMelody(7); // Menu Enter Sound
            manualMode = MANUAL_MENU;
            manualMenuIndex = 0;
            updateOLED();
        } else if (click1 == 1) { // Prev Page
            playMelody(6); 
            oledPage = (oledPage - 1 + 3) % 3;
            lastPageSwitch = millis();
            updateOLED();
        } else if (click2 == 1) { // Next Page
            playMelody(6);
            oledPage = (oledPage + 1) % 3;
            lastPageSwitch = millis();
            updateOLED();
        }
    } else if (manualMode == MANUAL_MENU) {
        if (click1 == 1) { // Prev
            playMelody(6); // Scroll Sound
            manualMenuIndex = (manualMenuIndex - 1 + 6) % 6;
            updateOLED();
        } else if (click2 == 1) { // Next
            playMelody(6); // Scroll Sound
            manualMenuIndex = (manualMenuIndex + 1) % 7;
            updateOLED();
        } else if (click1 == 2 || click2 == 2) { // Select (Double Click)
            if (manualMenuIndex == 6) playMelody(8); // Exit Sound
            else playMelody(7); // Select/Enter Sound
            
            if (manualMenuIndex == 0) manualMode = MANUAL_CTRL_X;
            else if (manualMenuIndex == 1) manualMode = MANUAL_CTRL_Y;
            else if (manualMenuIndex == 2) { 
                manualMode = MANUAL_CTRL_Z;
                usePIDRail = true; // Enable PID for Z-Axis distance tracking
            }
            else if (manualMenuIndex == 3) manualMode = MANUAL_CTRL_DIFF_FW;
            else if (manualMenuIndex == 4) {
                manualMode = MANUAL_CTRL_DIFF_ROT;
                if (xSemaphoreTake(motorMutex, portMAX_DELAY)) {
                    manualBaseStepsX = stepperX.currentPosition();
                    manualBaseStepsY = stepperY.currentPosition();
                    xSemaphoreGive(motorMutex);
                }
            }
            else if (manualMenuIndex == 5) {
                manualMode = MANUAL_HOME;
                usePIDRail = false; // Disable PID during homing
                if (xSemaphoreTake(motorMutex, portMAX_DELAY)) {
                    stepperX.moveTo(0);
                    stepperY.moveTo(0);
                    stepperZ.moveTo(0);
                    xSemaphoreGive(motorMutex);
                }
            }
            else if (manualMenuIndex == 6) manualMode = MANUAL_OFF; // Exit
            updateOLED();
        } else if (long1 || long2) { // Cancel (Long Press)
            playMelody(8); // Cancel Sound
            manualMode = MANUAL_OFF;
            usePIDRail = false; // Disable PID when exiting
        }
    } else { // Control Mode (X, Y, Z, Diff, Home)
        if (long1 || long2) { // Exit Control Mode -> Menu
            playMelody(8); // Exit Sound
            manualMode = MANUAL_MENU;
            usePIDRail = false; // Disable PID when exiting control mode
            if (xSemaphoreTake(motorMutex, portMAX_DELAY)) {
                stepperX.stop();
                stepperY.stop();
                stepperZ.stop();
                xSemaphoreGive(motorMutex);
            }
            updateOLED();
        } else {
            // Real-time OLED update and Motor tracking
            static unsigned long lastOledDraw = 0;
            if (millis() - lastOledDraw > 50) { // 20FPS Update
                lastOledDraw = millis();
                
                // Erase previous values (Partial Update for speed)
                display.fillRect(45, 20, 80, 25, SSD1306_BLACK); 
                display.setTextColor(SSD1306_WHITE);
                display.setTextSize(1);
                
                if (manualMode == MANUAL_CTRL_X || manualMode == MANUAL_CTRL_Y) {
                    // Servo Mode (-360 to 360 deg) -> ADC 0~4095
                    float targetDeg = (lastADC / 4095.0) * 720.0 - 360.0;
                    
                    if (manualMode == MANUAL_CTRL_X) {
                        long targetSteps = (targetDeg / 1.8) * manualGearRatioX;
                        float realDeg = (stepperX.currentPosition() * 1.8) / manualGearRatioX;
                        display.setCursor(45, 20); display.printf("%.1f deg", targetDeg);
                        display.setCursor(45, 35); display.printf("%.1f deg", realDeg);
                        
                        if (!stopActive) {
                            digitalWrite(EN_PIN_X, LOW);
                            if (xSemaphoreTake(motorMutex, portMAX_DELAY)) {
                                stepperX.moveTo(targetSteps);
                                xSemaphoreGive(motorMutex);
                            }
                        }
                    } else if (manualMode == MANUAL_CTRL_Y) {
                        long targetSteps = (targetDeg / 1.8) * manualGearRatioY;
                        float realDeg = (stepperY.currentPosition() * 1.8) / manualGearRatioY;
                        display.setCursor(45, 20); display.printf("%.1f deg", targetDeg);
                        display.setCursor(45, 35); display.printf("%.1f deg", realDeg);
                        
                        if (!stopActive) {
                            digitalWrite(EN_PIN_Y, LOW);
                            if (xSemaphoreTake(motorMutex, portMAX_DELAY)) {
                                stepperY.moveTo(targetSteps);
                                xSemaphoreGive(motorMutex);
                            }
                        }
                    }
                } else if (manualMode == MANUAL_CTRL_Z) {
                    // Distance Target Mode (140mm ~ 180mm)
                    int targetDist = map(lastADC, 0, 4095, 140, 180);
                    targetRailDist = targetDist; // Set global PID target
                    
                    display.setCursor(45, 20); display.printf("Tar: %d mm", targetDist);
                    display.setCursor(45, 35); display.printf("Cur: %.1f mm", lastDistance);
                    
                    // Note: stepperZ is automatically driven by motorTask's PID loop
                    // because we set usePIDRail = true when entering this mode.
                } else if (manualMode == MANUAL_CTRL_DIFF_FW) {
                    // Diff Drive Forward/Backward (Absolute Servo Mode)
                    float targetDeg = (lastADC / 4095.0) * 720.0 - 360.0;
                    display.setCursor(45, 20); display.printf("%.1f deg", targetDeg);
                    display.setCursor(45, 35); display.print("Sync: FW/BW");
                    
                    if (!stopActive) {
                        digitalWrite(EN_PIN_X, LOW);
                        digitalWrite(EN_PIN_Y, LOW);
                        if (xSemaphoreTake(motorMutex, portMAX_DELAY)) {
                            stepperX.moveTo((targetDeg / 1.8) * manualGearRatioX);
                            stepperY.moveTo(-(targetDeg / 1.8) * manualGearRatioY);
                            xSemaphoreGive(motorMutex);
                        }
                    }
                } else if (manualMode == MANUAL_CTRL_DIFF_ROT) {
                    // Disk Rotation (Relative Displacement Control)
                    // Maps pot to -360 ~ +360 degree offset from entering point
                    float offsetDeg = (lastADC / 4095.0) * 720.0 - 360.0;
                    
                    display.setCursor(45, 20); display.printf("Offs:%.1f", offsetDeg);
                    display.setCursor(45, 35); display.print("ROT Displacement");
                    
                    if (!stopActive) {
                        digitalWrite(EN_PIN_X, LOW);
                        digitalWrite(EN_PIN_Y, LOW);
                        if (xSemaphoreTake(motorMutex, portMAX_DELAY)) {
                            stepperX.moveTo(manualBaseStepsX + (offsetDeg / 1.8) * manualGearRatioX);
                            stepperY.moveTo(manualBaseStepsY + (offsetDeg / 1.8) * manualGearRatioY);
                            xSemaphoreGive(motorMutex);
                        }
                    }
                } else if (manualMode == MANUAL_HOME) {
                    // Display Homing Status
                    display.setCursor(45, 20); display.print("GO HOME...");
                    
                    bool xDone = stepperX.distanceToGo() == 0;
                    bool yDone = stepperY.distanceToGo() == 0;
                    bool zDone = stepperZ.distanceToGo() == 0;
                    
                    display.setCursor(45, 35);
                    display.printf("%s %s %s", xDone?"X":"-", yDone?"Y":"-", zDone?"Z":"-");
                    
                    if (xDone && yDone && zDone) {
                        playMelody(2); // Success Sound
                        manualMode = MANUAL_MENU;
                        updateOLED();
                    }
                }
                display.display();
            }
        }
    }

    static unsigned long lastToFRead = 0;
    static float distSamples[5] = {0};
    static uint8_t sampleIdx = 0;
    static uint8_t sampleCount = 0;
    
    if (millis() - lastToFRead > 50) { 
        lastToFRead = millis();
        uint16_t dist = sensor.readRangeContinuousMillimeters();
        if (sensor.timeoutOccurred()) {
            tofStatus = "TIMEOUT";
        } else if (dist >= 0 && dist <= 2000) {
            tofStatus = "OK";
            distSamples[sampleIdx] = (float)dist;
            sampleIdx = (sampleIdx + 1) % 5;
            if (sampleCount < 5) sampleCount++;
            
            float sum = 0;
            for (uint8_t i = 0; i < sampleCount; i++) sum += distSamples[i];
            lastDistance = sum / sampleCount; 
        } else {
            tofStatus = "OUT OF RANGE";
            // Do not update lastDistance with invalid readings
        }
    }

    static unsigned long lastOled = 0;
    if (millis() - lastOled > 200) { 
        updateOLED();
        lastOled = millis();
    }

}
