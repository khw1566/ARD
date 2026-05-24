#include "esp_camera.h"
#include "soc/rtc_cntl_reg.h"
#include "soc/soc.h"
#include <Arduino.h>
#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <Preferences.h>
#include <WiFi.h>
#include <WiFiUdp.h>

// --- 사용자 구성 & 저장소 ---
Preferences prefs;
const char *WIFI_SSID_1 = "YOUR_WIFI_SSID";
const char *WIFI_PASS_1 = "YOUR_WIFI_PASSWORD";
const char *WIFI_SERVER_1 = "192.168.0.13";

const char *WIFI_SSID_2 = "1-309";
const char *WIFI_PASS_2 = "0522300751";
const char *WIFI_SERVER_2 = "192.168.0.35";

const char *WIFI_SSID_3 = "A35_m";
const char *WIFI_PASS_3 = "00000000";
const char *WIFI_SERVER_3 = "10.37.227.12";

uint32_t WIFI_TIMEOUT_MS = 5000; // 5 seconds per attempt

String ssid_active = "";
String pass_active = "";
String server_ip_pref = "";
String server_ip = "10.37.227.12"; // Active server IP
int udp_port = 5001;

// --- 스마트 고정 IP 설정 (사용자 요청 사항: 뒷자리만 고정) ---
bool useStaticIP = true;  // 고정 IP 사용 여부
int targetLastByte = 100; // 송신기에 부여하고 싶은 IP 뒷자리 (예: 192.168.0.36)
                          // 주의: server_ip와 중복되지 않게 설정하세요!

String serverUrl, configUrl, logUrl, heartbeatUrl;

void updateUrls() {
  serverUrl = "http://" + server_ip + ":5000/upload";
  configUrl = "http://" + server_ip + ":5000/api/imagesendermcu/config";
  logUrl = "http://" + server_ip + ":5000/api/log";
  heartbeatUrl = "http://" + server_ip + ":5000/api/heartbeat";
}

WiFiUDP udp;

// --- 현재 상태 트래킹 ---
String currentResolution = "SVGA";
int currentQuality = 10;
String currentSensor = "OV5640";
bool configChanged = false;
String currentMode = "UDP"; // 기본값

// [버그 수정 1] 소켓 충돌 방지 플래그 (HTTP 소켓과 카메라 캡처 동시 실행 방지)
volatile bool pendingCapture = false;

// --- 카메라 핀 설정 (사용자 검증 완료된 N16R8 표준 핀맵) ---
#define PWDN_GPIO_NUM -1
#define RESET_GPIO_NUM -1
#define XCLK_GPIO_NUM 15
#define SIOD_GPIO_NUM 4
#define SIOC_GPIO_NUM 5

#define Y9_GPIO_NUM 16
#define Y8_GPIO_NUM 17
#define Y7_GPIO_NUM 18
#define Y6_GPIO_NUM 12
#define Y5_GPIO_NUM 10
#define Y4_GPIO_NUM 8
#define Y3_GPIO_NUM 9
#define Y2_GPIO_NUM 11
#define VSYNC_GPIO_NUM 6
#define HREF_GPIO_NUM 7
#define PCLK_GPIO_NUM 13

void captureAndUpload();
void checkRemoteConfig();
void initCamera();
void sendLog(String source, String msg);
void sendHeartbeat();
void handleWifiConfigMode();

framesize_t getFrameSize(String res) {
  if (res == "QSXGA")
    return FRAMESIZE_QSXGA;
  if (res == "UXGA")
    return FRAMESIZE_UXGA;
  if (res == "SXGA")
    return FRAMESIZE_SXGA;
  if (res == "XGA")
    return FRAMESIZE_XGA;
  if (res == "SVGA")
    return FRAMESIZE_SVGA;
  if (res == "VGA")
    return FRAMESIZE_VGA;
  if (res == "CIF")
    return FRAMESIZE_CIF;
  if (res == "QVGA")
    return FRAMESIZE_QVGA;
  return FRAMESIZE_UXGA;
}

// 시리얼에서 한 줄(엔터 전까지)을 확실하게 읽어오는 함수
String readSerialLine() {
  String input = "";
  while (true) {
    if (Serial.available()) {
      char c = Serial.read();
      if (c == '\n' || c == '\r') {
        // \r\n 같이 연속된 개행문자 처리
        delay(50);
        while (Serial.available() &&
               (Serial.peek() == '\n' || Serial.peek() == '\r')) {
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

void setup() {
  WRITE_PERI_REG(RTC_CNTL_BROWN_OUT_REG, 0);

  Serial.begin(115200);
  delay(2000);
  Serial.println("\n\n=== TRANSMITTER SYSTEM ONLINE ===");

  // 1. 설정 및 WiFi 정보 로드
  prefs.begin("wifi-config", false);
  if (prefs.isKey("ssid"))
    ssid_active = prefs.getString("ssid");
  if (prefs.isKey("password"))
    pass_active = prefs.getString("password");
  if (prefs.isKey("server_ip"))
    server_ip_pref = prefs.getString("server_ip");

  updateUrls();

  // Fallback WiFi Connection Logic
  const char *ssids[] = {ssid_active.c_str(), WIFI_SSID_1, WIFI_SSID_2,
                         WIFI_SSID_3};
  const char *passes[] = {pass_active.c_str(), WIFI_PASS_1, WIFI_PASS_2,
                          WIFI_PASS_3};
  const char *servers[] = {server_ip_pref.c_str(), WIFI_SERVER_1, WIFI_SERVER_2,
                           WIFI_SERVER_3};
  bool connected = false;

  for (int i = 0; i < 4; i++) {
    if (strlen(ssids[i]) == 0)
      continue;

    Serial.printf("\n[WiFi] Attempting connection to %s...\n", ssids[i]);

    // Set Target Server IP before attempting Static Config (it depends on it)
    if (strlen(servers[i]) > 0)
      server_ip = servers[i];

    if (useStaticIP) {
      IPAddress serverAddr;
      if (serverAddr.fromString(server_ip)) {
        IPAddress local_IP = serverAddr;
        local_IP[3] = targetLastByte;
        IPAddress gateway = serverAddr;
        gateway[3] = 1;
        IPAddress subnet(255, 255, 255, 0);
        WiFi.config(local_IP, gateway, subnet);
      }
    }

    WiFi.begin(ssids[i], passes[i]);
    WiFi.setSleep(false);

    unsigned long start = millis();
    while (WiFi.status() != WL_CONNECTED &&
           (millis() - start < WIFI_TIMEOUT_MS)) {
      delay(500);
      Serial.print(".");
    }

    if (WiFi.status() == WL_CONNECTED) {
      updateUrls(); // Synced URL with the current server_ip
      connected = true;
      break;
    }
  }

  if (connected) {
    Serial.println("\nWiFi Connected Success!");
    Serial.print("Local IP: ");
    Serial.println(WiFi.localIP());
    sendLog("Transmitter", "System Online. IP: " + WiFi.localIP().toString());
  } else {
    Serial.println("\nWiFi All Connection Failed!");
    handleWifiConfigMode();
  }

  Serial.println("=== SYSTEM READY. Type 'c' to capture ===");

  // 최초 카메라 초기화
  initCamera();
}

void initCamera() {
  camera_config_t config;
  config.ledc_channel = LEDC_CHANNEL_0;
  config.ledc_timer = LEDC_TIMER_0;
  config.pin_d0 = Y2_GPIO_NUM;
  config.pin_d1 = Y3_GPIO_NUM;
  config.pin_d2 = Y4_GPIO_NUM;
  config.pin_d3 = Y5_GPIO_NUM;
  config.pin_d4 = Y6_GPIO_NUM;
  config.pin_d5 = Y7_GPIO_NUM;
  config.pin_d6 = Y8_GPIO_NUM;
  config.pin_d7 = Y9_GPIO_NUM;
  config.pin_xclk = XCLK_GPIO_NUM;
  config.pin_pclk = PCLK_GPIO_NUM;
  config.pin_vsync = VSYNC_GPIO_NUM;
  config.pin_href = HREF_GPIO_NUM;
  config.pin_sccb_sda = SIOD_GPIO_NUM;
  config.pin_sccb_scl = SIOC_GPIO_NUM;
  config.pin_pwdn = PWDN_GPIO_NUM;
  config.pin_reset = RESET_GPIO_NUM;
  config.xclk_freq_hz = 18000000;
  config.pixel_format = PIXFORMAT_JPEG;
  config.grab_mode = CAMERA_GRAB_LATEST;

  config.frame_size = getFrameSize(currentResolution);
  // PC 0-50 -> ESP 직접 전달 (0이 최고화질, 50이 고압축)
  config.jpeg_quality = currentQuality;
  config.fb_count = psramFound() ? 2 : 1;
  config.fb_location = psramFound() ? CAMERA_FB_IN_PSRAM : CAMERA_FB_IN_DRAM;

  esp_err_t err = esp_camera_init(&config);
  if (err != ESP_OK) {
    Serial.printf("Camera init failed: 0x%x\n", err);
    return;
  }

  sensor_t *s = esp_camera_sensor_get();
  if (s) {
    s->set_exposure_ctrl(s, 1);
    s->set_gain_ctrl(s, 1);
    s->set_whitebal(s, 1);
    s->set_sharpness(s, 2);
    s->set_denoise(s, 1);
    s->set_quality(s, currentQuality); // 현재 품질 즉시 적용

    String logMsg = "Camera Init: " + currentResolution +
                    " (Q:" + String(currentQuality) + ")";
    Serial.println(logMsg);
    sendLog("ImageSenderMCU", logMsg);
  }
}

void handleWifiConfigMode() {
  WiFi.disconnect(true);
  delay(500);
  Serial.println("\n\n--- ImageSender Config Mode ---");
  Serial.println("1. Serial Config (WIFI,SSID,PASS,IP)");
  Serial.println("2. Bluetooth Config (ARD_Cam_Config)");
  Serial.println("3. Reset & Restart");
  Serial.print("Select (1-3): ");

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
                prefs.begin("wifi-config", false);
                prefs.putString("ssid", line.substring(5, p1));
                prefs.putString("password", line.substring(p1+1, p2));
                prefs.putString("server_ip", line.substring(p2+1));
                prefs.end();
                ESP.restart();
            }
        }
      } else if (choice == "3") {
        prefs.begin("wifi-config", false);
        prefs.clear();
        prefs.end();
        ESP.restart();
      }
    }
    delay(100);
  }
}

void sendLog(String level, String msg) {
  if (WiFi.status() != WL_CONNECTED)
    return;
  HTTPClient http;
  http.begin(logUrl);
  http.addHeader("Content-Type", "application/json");

  StaticJsonDocument<256> doc;
  doc["source"] = "ImageSenderMCU";
  doc["message"] = msg;
  String payload;
  serializeJson(doc, payload);

  int httpCode = http.POST(payload);
  if (httpCode > 0) {
    // Serial.printf("[Log] Success: %d\n", httpCode);
  } else {
    Serial.printf("[Log] Failed: %s\n", http.errorToString(httpCode).c_str());
  }
  http.end();
}

void sendHeartbeat() {
  if (WiFi.status() != WL_CONNECTED)
    return;
  HTTPClient http;
  http.begin(heartbeatUrl);
  http.setTimeout(5000); // 5초 타임아웃
  http.addHeader("Content-Type", "application/json");

  StaticJsonDocument<256> doc;
  doc["source"] = "Transmitter";
  doc["rssi"] = WiFi.RSSI();
  doc["ip"] = WiFi.localIP().toString();
  doc["temp"] = temperatureRead();
  String payload;
  serializeJson(doc, payload);

  int httpCode = http.POST(payload);
  if (httpCode > 0) {
    // Serial.printf("[Heartbeat] Success: %d\n", httpCode);
  } else {
    Serial.printf("[Heartbeat] Failed: %s\n",
                  http.errorToString(httpCode).c_str());
  }
  http.end();
}

void checkRemoteConfig() {
  if (WiFi.status() != WL_CONNECTED)
    return;

  HTTPClient http;
  http.begin(configUrl);
  int httpCode = http.GET();

  if (httpCode == 200) {
    String payload = http.getString();
    http.end(); // [버그 수정 2] HTTP 요청 완료 직후 소켓을 닫아 카메라 소켓 사용 시 충돌을 방지합니다.

    DynamicJsonDocument doc(256);
    deserializeJson(doc, payload);

    String newRes = doc["resolution"] | currentResolution;
    String newSensor = doc["camera_model"] | currentSensor;
    int newQuality = doc["jpeg_quality"] | currentQuality;

    String newMode = doc["transmission_mode"] | currentMode;

    if (newRes != currentResolution || newSensor != currentSensor) {
      // 해상도나 센서가 바뀌면 전체 초기화 필요
      currentResolution = newRes;
      currentSensor = newSensor;
      currentQuality = newQuality;
      currentMode = newMode;
      configChanged = true;
    } else if (newQuality != currentQuality || newMode != currentMode) {
      // 품질이나 모드만 바뀌면 즉시 반영 (카메라 재시작 X)
      if (newQuality != currentQuality) {
        currentQuality = newQuality;
        sensor_t *s = esp_camera_sensor_get();
        if (s)
          s->set_quality(s, currentQuality);
        Serial.printf("Quality changed to: %d\n", currentQuality);
      }
      currentMode = newMode;
      Serial.println("Mode/Quality updated. No restart required.");
    }

    // 원격 명령 처리
    String cmd = doc["command"] | "";
    if (cmd == "capture" || cmd == "CAPTURE") { // [버그 수정 3] 대소문자 명령어 모두 지원
      Serial.println("Remote command received: CAPTURE");
      sendLog("Transmitter", "Remote capture command received");
      pendingCapture = true; // 플래그를 세팅하여 loop에서 촬영 진행
    }
  } else {
    http.end();
  }
}

void loop() {
  // [버그 수정 4] 캡처 플래그가 세팅된 경우, 가장 먼저 캡처를 실행합니다.
  if (pendingCapture) {
    pendingCapture = false;
    captureAndUpload();
  }

  // 3초마다 원격 설정 및 명령 확인 (반응성 강화)
  static unsigned long lastConfigCheck = 0;
  if (millis() - lastConfigCheck > 3000) {
    checkRemoteConfig();
    lastConfigCheck = millis();

    if (configChanged) {
      Serial.println("Config changed! Re-initializing camera...");
      esp_camera_deinit();
      delay(500);
      initCamera();
      configChanged = false;
    }
  }

  if (Serial.available()) {
    char cmd = Serial.read();
    if (cmd == 'c') {
      captureAndUpload();
    } else if (cmd == 'w') {
      handleWifiConfigMode();
    }
  }

  // 30초마다 하트비트 전송
  static unsigned long lastHeartbeat = 0;
  if (millis() - lastHeartbeat > 5000) {
    sendHeartbeat();
    lastHeartbeat = millis();
  }
  delay(1);
}

void captureAndUpload() {
  Serial.println("Capturing...");
  delay(500); // 캡처 전 센서 안정화 대기

  camera_fb_t *fb = esp_camera_fb_get();
  if (!fb) {
    Serial.println("Capture Error! Frame buffer is NULL. Retrying...");
    delay(500);
    fb = esp_camera_fb_get(); // 다시 한번 시도
    if (!fb) {
      Serial.println("Critical: Repeated Capture Failure!");
      return;
    }
  }

  Serial.printf("Captured %u bytes. Mode: %s. Uploading...\n", fb->len,
                currentMode.c_str());

  if (WiFi.status() == WL_CONNECTED) {
    if (currentMode == "TCP") {
      // --- TCP (HTTP POST) 전송 모드 ---
      Serial.printf("TCP Upload Start: %u bytes to %s\n", fb->len,
                    serverUrl.c_str());
      HTTPClient http;
      http.begin(serverUrl);
      http.setTimeout(10000); // 10초 타임아웃
      http.addHeader("Content-Type", "image/jpeg");

      unsigned long startTime = millis();
      int httpCode = http.POST(fb->buf, fb->len);
      unsigned long duration = millis() - startTime;

      if (httpCode > 0) {
        Serial.printf("HTTP Upload Success: %d (%u ms). Finishing...\n",
                      httpCode, duration);
      } else {
        Serial.printf("HTTP Upload Failed: %s (%d)\n",
                      http.errorToString(httpCode).c_str(), httpCode);
      }
      http.end();
    } else {
      // --- UDP 전송 모드 (기존) ---
      Serial.printf("UDP Upload Start: %u bytes\n", fb->len);
      size_t size = fb->len;
      uint8_t *buffer = fb->buf;
      const size_t chunkSize = 1436; // MTU 최적화 (기존 1024에서 확장)
      size_t offset = 0;

      while (offset < size) {
        size_t remaining = size - offset;
        size_t toSend = (remaining > chunkSize) ? chunkSize : remaining;

        uint8_t header = 1; // 기본 데이터 패킷
        if (offset == 0)
          header = 0; // 시작 패킷
        else if (offset + toSend >= size)
          header = 2; // 종료 패킷

        // 시작 패킷(header 0)의 경우, 수신측 버퍼 초기화를 위해 2번 전송
        // (안정성 강화)
        int repeatCount = (header == 0) ? 2 : 1;

        while (repeatCount > 0) {
          int retryCount = 0;
          bool success = false;

          while (retryCount < 5 && !success) {
            udp.beginPacket(server_ip.c_str(), udp_port);
            udp.write(header);
            udp.write(buffer + offset, toSend);
            if (udp.endPacket() > 0) {
              success = true;
            } else {
              Serial.printf(
                  "[UDP] Packet Send Failed (Error 12/Full). Retry %d...\n",
                  retryCount + 1);
              retryCount++;
              delay(10 * retryCount);
            }
          }

          if (!success) {
            Serial.println("[UDP] Critical: Packet drop!");
            break;
          }

          repeatCount--;
          if (repeatCount > 0)
            delay(5); // 중복 전송 시 짧은 간격
        }

        offset += toSend;
        delay(5); // 지연시간을 5ms로 환원 (버퍼 플러시 시간 확보)
      }
      Serial.printf("UDP Upload Result: %s (%u/%u bytes)\n",
                    (offset >= size ? "Success" : "Failed"), offset, size);
    }
  } else {
    Serial.println("No WiFi Connection.");
  }

  esp_camera_fb_return(fb);
  Serial.println("Image sensor usage finished.");
  sendLog("Transmitter",
          "Image sensor usage finished (Capture/Upload complete)");
}
