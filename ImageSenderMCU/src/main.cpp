#include "esp_camera.h"
#include <Arduino.h>
#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <Preferences.h>
#include <WiFi.h>
#include <ArdNetwork.h>
ArdNetwork ardNetwork;
#include <WiFiUdp.h>
#include "esp_system.h"


// --- 사용자 구성 & 저장소 ---




String ssid_active = "";
String server_ip = "10.115.5.12"; // Active server IP
int udp_port = 5001;

// Shared SoftAP/STA manager uses DHCP.
String serverUrl, configUrl, logUrl, heartbeatUrl;

void updateUrls() {
  serverUrl = "http://" + server_ip + ":5000/upload";
  configUrl = "http://" + server_ip + ":5000/api/imagesendermcu/config";
  logUrl = "http://" + server_ip + ":5000/api/log";
  heartbeatUrl = "http://" + server_ip + ":5000/api/heartbeat";
}

WiFiUDP udp;
unsigned long lastCaptureDurationMs = 0;
unsigned long lastTxDurationMs = 0;
// WiFiServer tcpCmdServer(5003); // 지연시간 측정용 (일시 비활성화)

// --- 현재 상태 트래킹 ---
String currentResolution = "SVGA"; // 안정 해상도 SVGA (800x600)
int currentQuality = 12;
String currentSensor = "OV3660";   // 기본 센서 OV3660
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
  Serial.begin(115200);
  Serial.println("\n\n=== TRANSMITTER SYSTEM ONLINE ===");
  Serial.println("[Boot] Stage 1: setup entered; SDK brownout protection retained");
  Serial.printf("[Boot] Reset reason: %d, heap: %u, PSRAM: %u bytes\n",
                static_cast<int>(esp_reset_reason()), ESP.getFreeHeap(),
                ESP.getPsramSize());
  Serial.printf("[Boot] SDK: %s, CPU: %u MHz\n", ESP.getSdkVersion(),
                ESP.getCpuFreqMHz());
  Serial.flush();

  if (!ardNetwork.begin("image", "wifi-config", server_ip.c_str()))
    Serial.println("[NET] Network setup failed");
  server_ip = ardNetwork.serverAddress();
  updateUrls();

  // 최초 카메라 초기화
  Serial.println("[Boot] Stage 4: camera initialization");
  Serial.flush();
  initCamera();
  Serial.println("[Boot] Setup complete. Type 'c' to capture");
}

void initCamera() {
  camera_config_t config = {};
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

  Serial.printf("[Camera] Calling esp_camera_init: PSRAM=%s, fb_count=%u\n",
                psramFound() ? "yes" : "no", config.fb_count);
  Serial.flush();
  esp_err_t err = esp_camera_init(&config);
  Serial.printf("[Camera] esp_camera_init returned: %s (0x%x)\n",
                esp_err_to_name(err), err);
  if (err != ESP_OK) {
    Serial.printf("Camera init failed: 0x%04x\n", err);
    return;
  }

  sensor_t *s = esp_camera_sensor_get();
  if (s != nullptr) {
    s->set_exposure_ctrl(s, 1);
    s->set_gain_ctrl(s, 1);
    s->set_whitebal(s, 1);
    s->set_sharpness(s, 2);
    s->set_denoise(s, 1);
    s->set_quality(s, currentQuality); // 현재 품질 즉시 적용

    String logMsg = "Camera Init: " + currentResolution +
                    " (Q:" + String(currentQuality) + ")";
    Serial.println(logMsg);
    if (WiFi.status() == WL_CONNECTED) {
      sendLog("ImageSenderMCU", logMsg);
    }
  }
}

void handleWifiConfigMode() { ardNetwork.printSetup(); }

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
  // doc["capture_ms"] = lastCaptureDurationMs;
  // doc["tx_ms"] = lastTxDurationMs;
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

#if 0 // TCP command server is disabled; keep its implementation out of the build.
void checkTcpCommand() {
  WiFiClient client = tcpCmdServer.accept();
  if (!client) return;
  unsigned long startWait = millis();
  while (client.connected() && (millis() - startWait < 500)) {
    if (client.available()) {
      String req = client.readStringUntil('\n');
      req.trim();
      if (req.equalsIgnoreCase("c") || req.equalsIgnoreCase("capture")) {
        Serial.println("[TCP] Direct Capture Command Received via Port 5003");
        pendingCapture = true;
        client.println("ACK:CAPTURE_START");
      } else if (req.equalsIgnoreCase("ping")) {
        client.printf("ACK:PONG,cap_ms=%lu,tx_ms=%lu\n", lastCaptureDurationMs, lastTxDurationMs);
      }
      startWait = millis();
    }
    delay(1);
  }
  client.stop();
}
#endif

void loop() {
  String address = ardNetwork.serverAddress();
  if (address != server_ip) { server_ip = address; updateUrls(); }
  if (WiFi.status() == WL_CONNECTED) ssid_active = WiFi.SSID();
  // TCP 5003 포트 직접 명령 검사 (지연시간 측정용, 일시 비활성화)
  // checkTcpCommand();

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

  // 하트비트 전송 (30초 주기 안정화)
  static unsigned long lastHeartbeat = 0;
  if (millis() - lastHeartbeat > 30000) {
    sendHeartbeat();
    lastHeartbeat = millis();
  }
  delay(1);
}

void captureAndUpload() {
  sensor_t *s = esp_camera_sensor_get();
  if (!s) {
    Serial.println("Capture aborted: Camera sensor not detected.");
    return;
  }

  Serial.println("Capturing...");
  delay(50); // 캡처 전 센서 안정화 최소 대기

  unsigned long t_cap_start = millis();
  camera_fb_t *fb = esp_camera_fb_get();
  if (!fb) {
    Serial.println("Capture Error! Frame buffer is NULL. Retrying...");
    delay(200);
    fb = esp_camera_fb_get(); // 다시 한번 시도
    if (!fb) {
      Serial.println("Critical: Repeated Capture Failure!");
      return;
    }
  }
  lastCaptureDurationMs = millis() - t_cap_start;

  Serial.printf("Captured %u bytes (Capture Latency: %lu ms). Mode: %s. Uploading...\n",
                fb->len, lastCaptureDurationMs, currentMode.c_str());

  unsigned long t_tx_start = millis();

  if (WiFi.status() == WL_CONNECTED) {
    if (currentMode == "TCP") {
      // --- TCP (HTTP POST) 전송 모드 ---
      Serial.printf("TCP Upload Start: %u bytes to %s\n", fb->len,
                    serverUrl.c_str());
      HTTPClient http;
      http.begin(serverUrl);
      http.setTimeout(10000); // 10초 타임아웃
      http.addHeader("Content-Type", "image/jpeg");

      int httpCode = http.POST(fb->buf, fb->len);
      lastTxDurationMs = millis() - t_tx_start;

      if (httpCode > 0) {
        Serial.printf("HTTP Upload Success: %d (%lu ms). Finishing...\n",
                      httpCode, lastTxDurationMs);
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
            delay(5);
        }

        offset += toSend;
        delay(3);
      }
      lastTxDurationMs = millis() - t_tx_start;
      Serial.printf("UDP Upload Result: %s (%u/%u bytes, Tx Latency: %lu ms)\n",
                    (offset >= size ? "Success" : "Failed"), offset, size, lastTxDurationMs);
    }
  } else {
    Serial.println("No WiFi Connection.");
  }

  esp_camera_fb_return(fb);
  Serial.println("Image sensor usage finished.");
  sendLog("Transmitter",
          "Image sensor usage finished. Cap: " + String(lastCaptureDurationMs) + "ms, Tx: " + String(lastTxDurationMs) + "ms");
}
