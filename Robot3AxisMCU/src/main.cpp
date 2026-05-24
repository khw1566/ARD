#include <Arduino.h>
#include <WiFi.h>
#include <WiFiClient.h>
#include <AccelStepper.h>
#include <ArduinoJson.h>
#include <HTTPClient.h>
#include "soc/soc.h"
#include "soc/rtc_cntl_reg.h"

// --- Pin Definitions (ESP32-S3) ---
// Stepper Motors (X, Y, Z) - Order: EN, STEP, DIR
#define X_EN_PIN   11
#define X_STEP_PIN 9
#define X_DIR_PIN  10

#define Y_EN_PIN   14
#define Y_STEP_PIN 12
#define Y_DIR_PIN  13

#define Z_EN_PIN   46
#define Z_STEP_PIN 8
#define Z_DIR_PIN  3

// Relay (4 Channels) - Pins 4 & 5 are for Pneumatic Gripper
const int RELAY_PINS[4] = {4, 5, 6, 7};

// L298N (4 Inputs)
const int L298N_PINS[4] = {15, 16, 17, 18};

// --- Unified WiFi Configuration ---
const char* WIFI_SSID_1 = "1-309";
const char* WIFI_PASS_1 = "0522300751";
const char* WIFI_SERVER_1 = "192.168.0.35";

const char* WIFI_SSID_2 = "A35_m";
const char* WIFI_PASS_2 = "00000000";
const char* WIFI_SERVER_2 = "192.168.0.35";

const char* WIFI_SSID_3 = "YOUR_WIFI_SSID";
const char* WIFI_PASS_3 = "YOUR_WIFI_PASSWORD";
const char* WIFI_SERVER_3 = "192.168.0.13";

String ssid_active = "";
String server_ip = "192.168.0.35";
String serverUrl = "http://192.168.0.35:5000";

// --- Global Objects ---
AccelStepper stepperX(AccelStepper::DRIVER, X_STEP_PIN, X_DIR_PIN);
AccelStepper stepperY(AccelStepper::DRIVER, Y_STEP_PIN, Y_DIR_PIN);
AccelStepper stepperZ(AccelStepper::DRIVER, Z_STEP_PIN, Z_DIR_PIN);

WiFiServer commandServer(5002);
WiFiClient client;

// --- Task Handles ---
TaskHandle_t hMotorTask = NULL;
TaskHandle_t hNetworkTask = NULL;

// --- Functions ---
void setupWifi();
void processCommand(String cmd);
void motorTask(void *pvParameters);
void networkTask(void *pvParameters);
void sendHeartbeat();

void setup() {
    WRITE_PERI_REG(RTC_CNTL_BROWN_OUT_REG, 0); // Disable brownout detector

    Serial.begin(115200);
    delay(500);
    Serial.println("\n[BOOT] Robot3AxisMCU (ESP32-S3) Pneumatic-Control Starting...");

    // Pin Modes
    pinMode(X_EN_PIN, OUTPUT);
    pinMode(Y_EN_PIN, OUTPUT);
    pinMode(Z_EN_PIN, OUTPUT);
    digitalWrite(X_EN_PIN, LOW);
    digitalWrite(Y_EN_PIN, LOW);
    digitalWrite(Z_EN_PIN, LOW);

    for (int i = 0; i < 4; i++) {
        pinMode(RELAY_PINS[i], OUTPUT);
        digitalWrite(RELAY_PINS[i], HIGH); // Active-LOW: HIGH = 릴레이 OFF (초기 안전 상태)
        pinMode(L298N_PINS[i], OUTPUT);
        digitalWrite(L298N_PINS[i], LOW);
    }

    // Stepper Config
    stepperX.setMaxSpeed(2000);
    stepperX.setAcceleration(1000);
    stepperY.setMaxSpeed(2000);
    stepperY.setAcceleration(1000);
    stepperZ.setMaxSpeed(2000);
    stepperZ.setAcceleration(1000);

    // networkTask를 Core 0에 먼저 생성 (Core 1의 setup()에 영향 없음)
    xTaskCreatePinnedToCore(networkTask, "NetworkTask", 8192, NULL, 1, &hNetworkTask, 0);
    // motorTask는 마지막에 생성 (priority 3으로 setup()을 선점하므로 반드시 마지막)
    xTaskCreatePinnedToCore(motorTask,   "MotorTask",   4096, NULL, 3, &hMotorTask,   1);


    Serial.println("[BOOT] Setup Complete.");
}

void loop() {
    vTaskDelete(NULL);
}

void setupWifi() {
    const char* ssids[] = {WIFI_SSID_1, WIFI_SSID_2, WIFI_SSID_3};
    const char* passes[] = {WIFI_PASS_1, WIFI_PASS_2, WIFI_PASS_3};
    const char* servers[] = {WIFI_SERVER_1, WIFI_SERVER_2, WIFI_SERVER_3};

    bool connected = false;
    for (int i = 0; i < 3; i++) {
        Serial.printf("[WiFi] Trying: %s\n", ssids[i]);
        WiFi.begin(ssids[i], passes[i]);
        unsigned long start = millis();
        while (WiFi.status() != WL_CONNECTED && millis() - start < 5000) {
            delay(500); Serial.print(".");
        }
        Serial.println();
        if (WiFi.status() == WL_CONNECTED) {
            ssid_active = ssids[i];
            server_ip = servers[i];
            serverUrl = "http://" + server_ip + ":5000";
            connected = true;
            break;
        }
    }
    if (connected) {
        Serial.printf("[WiFi] Connected! IP: %s\n", WiFi.localIP().toString().c_str());
        commandServer.begin();
    } else {
        Serial.println("[WiFi] All failed. Retrying in 10s...");
        delay(10000); ESP.restart();
    }
}

void motorTask(void *pvParameters) {
    while (true) {
        stepperX.run();
        stepperY.run();
        stepperZ.run();
        // vTaskDelay(0)은 같은 우선순위 태스크에게 양보하지만 즉시 재실행 가능
        // 모터 펄스 정밀도를 위해 yield 없이 폴링 유지
        taskYIELD();
    }
}

void networkTask(void *pvParameters) {
    setupWifi();
    unsigned long lastHB = 0;
    String lineBuffer = "";  // 논-블로킹 라인 버퍼

    while (true) {
        // 새 클라이언트 접속 처리
        if (!client || !client.connected()) {
            client = commandServer.available();
        }

        // 수신 데이터를 문자 단위로 읽어 라인 버퍼에 누적 (논-블로킹)
        if (client && client.connected() && client.available()) {
            while (client.available()) {
                char c = client.read();
                if (c == '\n' || c == '\r') {
                    lineBuffer.trim();
                    if (lineBuffer.length() > 0) {
                        processCommand(lineBuffer);
                    }
                    lineBuffer = "";
                } else {
                    lineBuffer += c;
                }
            }
        }

        // 하트비트 전송 (10초마다)
        if (millis() - lastHB > 10000) {
            sendHeartbeat();
            lastHB = millis();
        }

        // WiFi 재연결
        if (WiFi.status() != WL_CONNECTED) {
            lineBuffer = "";  // 버퍼 초기화
            setupWifi();
        }

        vTaskDelay(1); // 1ms - CPU 독점 방지 (최소값)
    }
}

void processCommand(String cmd) {
    Serial.printf("[CMD] Received: %s\n", cmd.c_str());
    if (cmd.startsWith("a")) {
        int comma1 = cmd.indexOf(',');
        int comma2 = cmd.indexOf(',', comma1 + 1);
        if (comma1 > 0 && comma2 > 0) {
            long dx = cmd.substring(1, comma1).toInt();
            long dy = cmd.substring(comma1 + 1, comma2).toInt();
            long dz = cmd.substring(comma2 + 1).toInt();
            stepperX.move(dx); stepperY.move(dy); stepperZ.move(dz);
        }
    }
    else if (cmd.startsWith("rx")) stepperX.move(cmd.substring(2).toInt());
    else if (cmd.startsWith("ry")) stepperY.move(cmd.substring(2).toInt());
    else if (cmd.startsWith("rz")) stepperZ.move(cmd.substring(2).toInt());
    else if (cmd == "stop") { stepperX.stop(); stepperY.stop(); stepperZ.stop(); }
    else if (cmd == "home") { stepperX.moveTo(0); stepperY.moveTo(0); stepperZ.moveTo(0); }
    else if (cmd.startsWith("sx")) stepperX.setMaxSpeed(cmd.substring(2).toInt());
    else if (cmd.startsWith("sy")) stepperY.setMaxSpeed(cmd.substring(2).toInt());
    else if (cmd.startsWith("sz")) stepperZ.setMaxSpeed(cmd.substring(2).toInt());
    else if (cmd.startsWith("relay")) {
        int num = cmd.substring(5, 6).toInt() - 1;
        if (num >= 0 && num < 4) {
            if (cmd.endsWith("on"))  digitalWrite(RELAY_PINS[num], LOW);  // Active-LOW: LOW = ON
            else if (cmd.endsWith("off")) digitalWrite(RELAY_PINS[num], HIGH); // Active-LOW: HIGH = OFF
        }
    }
    else if (cmd == "l298n fwd") { digitalWrite(L298N_PINS[0], HIGH); digitalWrite(L298N_PINS[1], LOW); }
    else if (cmd == "l298n back") { digitalWrite(L298N_PINS[0], LOW); digitalWrite(L298N_PINS[1], HIGH); }
    else if (cmd == "l298n stop") { digitalWrite(L298N_PINS[0], LOW); digitalWrite(L298N_PINS[1], LOW); }
    
    // Grab Command (Pneumatic Valve Control - Relay 1,2 / GPIO 4,5)
    // Active-LOW 모듈: LOW = 릴레이 ON, HIGH = 릴레이 OFF
    else if (cmd == "grab open") {
        digitalWrite(RELAY_PINS[0], LOW);  // Relay 1 (GPIO 4) ON  → 열기 밸브 통전
        digitalWrite(RELAY_PINS[1], HIGH); // Relay 2 (GPIO 5) OFF → 닫기 밸브 차단
        Serial.println("[GRAB] Open (Pneumatic)");
    }
    else if (cmd == "grab close") {
        digitalWrite(RELAY_PINS[0], HIGH); // Relay 1 (GPIO 4) OFF → 열기 밸브 차단
        digitalWrite(RELAY_PINS[1], LOW);  // Relay 2 (GPIO 5) ON  → 닫기 밸브 통전
        Serial.println("[GRAB] Close (Pneumatic)");
    }
}

void sendHeartbeat() {
    if (WiFi.status() != WL_CONNECTED) return;
    HTTPClient http;
    http.begin(serverUrl + "/api/heartbeat");
    http.setTimeout(1000);
    http.addHeader("Content-Type", "application/json");
    StaticJsonDocument<256> doc;
    doc["source"] = "Robot3AxisMCU";
    doc["rssi"] = WiFi.RSSI();
    doc["ip"] = WiFi.localIP().toString();
    String payload;
    serializeJson(doc, payload);
    http.POST(payload);
    http.end();
}
