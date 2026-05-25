#include "Audio.h"
#include "lwip/dns.h"
#include <Adafruit_NeoPixel.h>
#include <Arduino.h>
#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <Preferences.h>
#include <WiFi.h>
// #include <BluetoothSerial.h> // ESP32-S3 does not support Classic BT
// (BluetoothSerial)

// --- Pin Definitions (ESP32-S3 N16R8 IPEX) ---
#define JOY_UP 47
#define JOY_DOWN 21
#define JOY_LEFT 13
#define JOY_RIGHT 14
#define TOGGLE_A 35
#define TOGGLE_B 36
#define PUSH_BTN 11       // GPIO11 = 버튼 입력
#define BUTTON_LED_PIN 4 // 4번 핀의 LED (음성입력 상태 표시)
#define LED_PIN 48        // 보드 내장 WS2812B RGB LED
#define NUM_LEDS 1

// Adafruit NeoPixel 객체 (WS2812B, GPIO48, GRB 순서)
Adafruit_NeoPixel strip(NUM_LEDS, LED_PIN, NEO_GRB + NEO_KHZ800);

// I2S Pins (MAX98357A Amplifier)
#define I2S_DOUT 40 // ESP32 → 앰프 DIN
#define I2S_WS 41   // ESP32 → 앰프 LRC
#define I2S_BCLK 42 // ESP32 → 앰프 BCLK
#define AMP_SD 39   // ESP32 → 앰프 SD (HIGH=켜짐, LOW=셧다운)

// --- Unified WiFi Configuration ---
Preferences prefs;
const char *WIFI_SSID_1 = "1-309";
const char *WIFI_PASS_1 = "0522300751";
const char *WIFI_SERVER_1 = "192.168.0.35";

const char *WIFI_SSID_2 = "A35_m";
const char *WIFI_PASS_2 = "00000000";
const char *WIFI_SERVER_2 = "192.168.0.35";

const char *WIFI_SSID_3 = "YOUR_WIFI_SSID";
const char *WIFI_PASS_3 = "YOUR_WIFI_PASSWORD";
const char *WIFI_SERVER_3 = "192.168.0.35";

String ssid_active = "";
String pass_active = "";
String server_ip_pref = "";
String server_ip = "192.168.0.35";
String serverUrl = "http://192.168.0.35:5000";

// --- Global States ---
volatile char currentMode = 'N'; // 'A', 'N'(Neutral), 'B'
volatile int currentSubMode = 1;
bool joyState[4] = {false, false, false, false};
volatile bool forceStateUpdate = false; // 즉시 상태 전송 플래그

// --- Task Handles ---
TaskHandle_t hControlTask = NULL;
TaskHandle_t hNetworkTask = NULL;

// --- Audio Player & Station List ---
struct Station {
  const char *name;
  const char *url;
};

Station stations[] = {
    // === 안정적인 엀엄리카 (128kbps 작동 확인됨) ===
    {"FFH Germany", "http://mp3.ffh.de/radioffh/livestream.mp3"},
    // === SomaFM 64kbps AAC ===
    {"SomaFM Groove Salad", "http://icecast.somafm.com/groovesalad-64-aac"},
    {"SomaFM Lofi", "http://icecast.somafm.com/lofi-64-aac"},
    {"SomaFM Indie Pop", "http://icecast.somafm.com/indiepop-64-aac"},
    // === 일본 채널 ===
    {"J1 Hits (J-Pop)", "http://hits.j1fm.tokyo/"},
    {"J1 Gold (Oldies)", "http://gold.j1fm.tokyo/"},
    {"Hotmix Japan",
     "http://hotmixradio-japan.ice.infomaniak.ch/hotmixradio-japan-128.mp3"},
    {"Shonan Beach FM", "http://47.89.252.38:8000/by_the_sea"},
    {"Ottava Classical", "http://rakuten.streamguys1.com/ottava1_b"},
    {"Asia DREAM Japan", "http://cabhs30.sonixcast.com:9764/;"}};

int currentStationIdx = 0;
int numStations = sizeof(stations) / sizeof(Station);

Audio audio;
volatile bool isAiSpeaking = false;
volatile uint8_t globalVolume = 12; // 전역 볼륨 (0 - 21)

void audio_info(const char *info) {
  Serial.print("[Audio Info] ");
  Serial.println(info);
}

void audioTask(void *pvParameters) {
  while (true) {
    if (!isAiSpeaking) {
      audio.loop();
    } else {
      if (audio.isRunning()) {
        audio.stopSong();
        Serial.println("[Audio] Stopped Radio stream for AI Voice playback");
      }
    }
    vTaskDelay(1 / portTICK_PERIOD_MS);
  }
}

// --- Function Prototypes ---

void setupWifi();
void sendHeartbeat();
void sendStateToPC();
void networkTask(void *pvParameters);
void controlTask(void *pvParameters);
void handleWifiConfigMode();
String readSerialLine();

String readSerialLine() {
  String input = "";
  while (true) {
    if (Serial.available()) {
      char c = Serial.read();
      if (c == '\n' || c == '\r') {
        delay(50);
        while (Serial.available() &&
               (Serial.peek() == '\n' || Serial.peek() == '\r'))
          Serial.read();
        input.trim();
        return input;
      }
      input += c;
    }
    vTaskDelay(10 / portTICK_PERIOD_MS);
  }
}

#include <WiFiUdp.h>
#include <driver/i2s.h>

// --- AI Audio & Discovery Configuration ---
WiFiUDP udp_audio;
WiFiUDP discoveryUdp;
const int AUDIO_UDP_PORT = 5005;
const int DISCOVERY_PORT = 5010;
const int DMA_BUF_LEN = 1024;
const int SAMPLE_RATE = 16000;

// --- Notification Tones ---
void playNotificationTone(int freq, int duration_ms) {
  int samples = (SAMPLE_RATE * duration_ms) / 1000;
  int16_t tone_val = (int16_t)(6000 * ((float)globalVolume / 21.0f)); // 볼륨 연동 적용
  int half_period = SAMPLE_RATE / (freq * 2);
  int16_t *temp_buf =
      (int16_t *)malloc(DMA_BUF_LEN * 2); // 스테레오 대응 (4바이트 per sample)

  int samples_sent = 0;
  while (samples_sent < samples) {
    int chunk = (samples - samples_sent > DMA_BUF_LEN / 4)
                    ? DMA_BUF_LEN / 4
                    : (samples - samples_sent);
    for (int i = 0; i < chunk; i++) {
      int16_t val = ((samples_sent + i) % (half_period * 2) < half_period)
                        ? tone_val
                        : -tone_val;
      temp_buf[i * 2] = val;     // Left
      temp_buf[i * 2 + 1] = val; // Right
    }
    size_t written;
    i2s_write(I2S_NUM_0, temp_buf, chunk * 4, &written, portMAX_DELAY);
    samples_sent += chunk;
  }
  free(temp_buf);
  i2s_zero_dma_buffer(I2S_NUM_0);
}

void playBootMelody() {
  Serial.println("[BOOT] Playing Premium Startup Melody...");
  // C Major 9 Arpeggio: C5 - E5 - G5 - B5 - D6 - G6 (Aesthetic, futuristic sound)
  int melody[] = {523, 659, 784, 988, 1175, 1568};
  int durations[] = {100, 100, 100, 100, 120, 300};
  
  for (int i = 0; i < 6; i++) {
    playNotificationTone(melody[i], durations[i]);
    delay(10);
  }
}

void audioReceiverTask(void *pvParameters) {
  uint8_t rx_buffer[DMA_BUF_LEN];
  // 버퍼 오버플로우를 안전하게 방지하기 위해 2배 크기로 확장 할당
  int16_t stereo_buffer[DMA_BUF_LEN * 2];
  static bool wasRadioPlaying = false;

  Serial.println("[Audio] Receiver Task Waiting for WiFi...");

  // WiFi 연결 대기
  while (WiFi.status() != WL_CONNECTED) {
    vTaskDelay(pdMS_TO_TICKS(500));
  }

  // WiFi 연결 후 UDP 시작
  udp_audio.begin(AUDIO_UDP_PORT);
  Serial.printf("[Audio] AI Voice Receiver Active on Port %d\n",
                AUDIO_UDP_PORT);

  unsigned long lastPacketTime = 0;
  int packetCount = 0;
  uint32_t totalBytesReceived = 0;

  while (true) {
    int packetSize = udp_audio.parsePacket();
    if (packetSize > 0) {
      // Check for UDP volume control packets (non-blocking)
      if (packetSize < 20) {
        char vol_buf[20];
        int len = udp_audio.read(vol_buf, 19);
        vol_buf[len] = '\0';
        String msg = String(vol_buf);
        msg.trim();
        if (msg.startsWith("vol=") || msg.startsWith("VOL:")) {
          int eqIdx = msg.indexOf(msg.indexOf('=') != -1 ? '=' : ':');
          int vol = msg.substring(eqIdx + 1).toInt();
          if (vol >= 0 && vol <= 21) {
            globalVolume = vol;
            audio.setVolume(globalVolume);
            Serial.printf("[UDP Volume] Dynamic volume set to %d\n", globalVolume);
          }
        }
        continue; // Do not write control packet to I2S
      }

      if (!isAiSpeaking) {
        isAiSpeaking = true;
        packetCount = 0;
        totalBytesReceived = 0;
        Serial.println("[Audio] Voice Stream Incoming: Muting Radio & Setting I2S to 16kHz...");
        wasRadioPlaying = audio.isRunning(); // 라디오 재생 상태 기억
        audio.stopSong(); // 높은 우선순위 태스크에서 동기적으로 라디오 정지
        // I2S 샘플레이트를 AI 음성에 맞는 16kHz로 강제 변경
        i2s_set_sample_rates(I2S_NUM_0, 16000);
      }
      lastPacketTime = millis();

      int len = udp_audio.read(rx_buffer, DMA_BUF_LEN);
      if (len > 0) {
        packetCount++;
        totalBytesReceived += len;

        // 30패킷(약 1초)마다 수신 현황 시리얼 출력
        if (packetCount % 30 == 1) {
          Serial.printf("[Audio] Playing Voice -> Packet #%d received, Chunk size: %d bytes (Total: %u bytes)\n", 
                        packetCount, len, totalBytesReceived);
        }

        int numSamples = len / 2; // 16-bit samples

        // 안전 장치: 수신된 샘플 수가 버퍼 크기를 초과하지 않도록 제한
        if (numSamples > DMA_BUF_LEN) {
          numSamples = DMA_BUF_LEN;
        }

        int16_t *mono_data = (int16_t *)rx_buffer;
        float vol_factor = (float)globalVolume / 21.0f;
        if (vol_factor > 1.0f) vol_factor = 1.0f;
        if (vol_factor < 0.0f) vol_factor = 0.0f;

        // 모노를 스테레오로 확장 및 볼륨 스케일링 적용
        for (int i = 0; i < numSamples; i++) {
          int16_t scaled_sample = (int16_t)(mono_data[i] * vol_factor);
          stereo_buffer[i * 2] = scaled_sample;     // Left
          stereo_buffer[i * 2 + 1] = scaled_sample; // Right
        }

        size_t bytes_written;
        // 확장된 데이터(4바이트 per sample)를 I2S로 전송
        i2s_write(I2S_NUM_0, stereo_buffer, numSamples * 4, &bytes_written,
                  portMAX_DELAY);
      }
    } else {
      if (isAiSpeaking && (millis() - lastPacketTime > 1500)) {
        isAiSpeaking = false;
        Serial.printf("[Audio] Voice Stream Stopped. Total packets: %d, Total bytes: %u. Restoring Radio...\n", 
                      packetCount, totalBytesReceived);
        if (wasRadioPlaying) {
          // 라디오 채널 원래 스트림 복원 재생
          audio.connecttohost(stations[currentStationIdx].url);
        }
      }
      vTaskDelay(pdMS_TO_TICKS(5));
    }
  }
}

void setupWifi() {
  Serial.println("\n[WiFi] Connecting...");
  Serial.flush();

  prefs.begin("wifi-config", false);
  if (prefs.isKey("ssid"))
    ssid_active = prefs.getString("ssid");
  if (prefs.isKey("password"))
    pass_active = prefs.getString("password");
  if (prefs.isKey("server_ip"))
    server_ip_pref = prefs.getString("server_ip");

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

    Serial.printf("[WiFi] Trying: %s\n", ssids[i]);
    Serial.flush();
    if (strlen(servers[i]) > 0)
      server_ip = servers[i];

    WiFi.disconnect(true);
    vTaskDelay(100 / portTICK_PERIOD_MS);
    WiFi.mode(WIFI_STA);

    /* // 제거: 고정 IP 대신 DHCP 사용 (네트워크 호환성 증대)
    IPAddress local_IP(192, 168, 0, 25);
    IPAddress gateway(192, 168, 0, 1);
    IPAddress subnet(255, 255, 255, 0);
    IPAddress primaryDNS(8, 8, 8, 8);
    IPAddress secondaryDNS(8, 8, 4, 4);

    if (!WiFi.config(local_IP, gateway, subnet, primaryDNS, secondaryDNS)) {
        Serial.println("[WiFi] Static IP Configuration Failed");
    }
    */

    WiFi.begin(ssids[i], passes[i]);
    WiFi.setSleep(false); // WiFi 시작 후 설정

    unsigned long start = millis();
    while (WiFi.status() != WL_CONNECTED && (millis() - start < 5000)) {
      Serial.print(".");
      Serial.flush();
      vTaskDelay(500 / portTICK_PERIOD_MS);
    }
    Serial.println();

    if (WiFi.status() == WL_CONNECTED) {
      ssid_active = ssids[i];
      pass_active = passes[i]; // 성공한 패스워드 기억
      connected = true;

      // 성공한 설정을 메모리에 저장 (다음 부팅 시 자동 연결용)
      prefs.putString("ssid", ssid_active);
      prefs.putString("password", pass_active);

      // [NEW] 서버 자동 검색 (UDP Discovery)
      Serial.println("[Discovery] Searching for PC Center...");
      discoveryUdp.begin(DISCOVERY_PORT);
      unsigned long discStart = millis();
      bool found = false;
      while (millis() - discStart < 3000) { // 3초간 대기
        int packetSize = discoveryUdp.parsePacket();
        if (packetSize) {
          char buf[64];
          int len = discoveryUdp.read(buf, 63);
          buf[len] = 0;
          String msg = String(buf);
          if (msg.startsWith("ARD_SERVER_DISCOVERY:")) {
            int firstColon = msg.indexOf(':');
            int secondColon = msg.indexOf(':', firstColon + 1);
            if (firstColon != -1 && secondColon != -1) {
              server_ip = msg.substring(firstColon + 1, secondColon);
              String port = msg.substring(secondColon + 1);
              serverUrl = "http://" + server_ip + ":" + port;
              Serial.printf("[Discovery] Found! IP=%s, URL=%s\n",
                            server_ip.c_str(), serverUrl.c_str());
              prefs.putString("server_ip", server_ip);
              found = true;
              break;
            }
          }
        }
        vTaskDelay(100 / portTICK_PERIOD_MS);
      }
      discoveryUdp.stop();
      if (!found) {
        Serial.printf("[Discovery] Not found. Using fallback: %s\n",
                      server_ip.c_str());
        serverUrl = "http://" + server_ip + ":5000";
        prefs.putString("server_ip", server_ip);
      }
      break;
    }
    Serial.printf("[WiFi] Failed: %s\n", ssids[i]);
    Serial.flush();
  }
  prefs.end();

  if (connected) {
    // serverUrl is already set by UDP Discovery or fallback in setupWifi loop
    Serial.printf("[WiFi] Connected! SSID=%s  IP=%s\n", ssid_active.c_str(),
                  WiFi.localIP().toString().c_str());
    Serial.printf("[WiFi] Server -> %s\n", serverUrl.c_str());

    // lwIP DNS API로 Google DNS 직접 설정
    ip_addr_t dns1, dns2;
    IP4_ADDR(&dns1.u_addr.ip4, 8, 8, 8, 8);
    dns1.type = IPADDR_TYPE_V4;
    IP4_ADDR(&dns2.u_addr.ip4, 8, 8, 4, 4);
    dns2.type = IPADDR_TYPE_V4;
    dns_setserver(0, &dns1);
    dns_setserver(1, &dns2);
    Serial.println("[WiFi] DNS -> 8.8.8.8 / 8.8.4.4");
    Serial.flush();
    vTaskDelay(500 / portTICK_PERIOD_MS);

  } else {

    Serial.println("[WiFi] ALL FAILED. Enter config mode ('w' key)...");
    Serial.flush();
    handleWifiConfigMode();
  }
}

void handleWifiConfigMode() {
  WiFi.disconnect(true);
  vTaskDelay(500 / portTICK_PERIOD_MS);
  Serial.println("\n\n--- ControlMCU Configuration Mode ---");
  Serial.println("1. Serial Config (WIFI,SSID,PASS,IP)");
  Serial.println("2. Bluetooth Config (Disabled on ESP32-S3)");
  Serial.println("3. Reset & Restart");
  Serial.flush();

  // BluetoothSerial SerialBT; // Disabled on ESP32-S3

  while (true) {
    if (Serial.available()) {
      String choice = readSerialLine();
      if (choice == "1") {
        Serial.println("Enter WIFI,SSID,PASS,IP:");
        String line = readSerialLine();
        if (line.startsWith("WIFI,")) {
          int p1 = line.indexOf(',', 5);
          int p2 = line.indexOf(',', p1 + 1);
          if (p1 > 0 && p2 > 0) {
            prefs.begin("wifi-config", false);
            prefs.putString("ssid", line.substring(5, p1));
            prefs.putString("password", line.substring(p1 + 1, p2));
            prefs.putString("server_ip", line.substring(p2 + 1));
            prefs.end();
            ESP.restart();
          }
        }
      } else if (choice == "2") {
        Serial.println("Bluetooth Config is not supported on ESP32-S3. Please "
                       "use Serial.");
        // SerialBT.begin("ARD_Control_Config");
        // Serial.println("BT Active: ARD_Control_Config");
      } else if (choice == "3") {
        prefs.begin("wifi-config", false);
        prefs.clear();
        prefs.end();
        ESP.restart();
      }
    }

    vTaskDelay(100 / portTICK_PERIOD_MS);
  }
}

void sendStateToPC() {
  if (WiFi.status() != WL_CONNECTED)
    return;
  HTTPClient http;
  http.begin(serverUrl + "/api/control/update");
  http.setTimeout(1000);
  http.addHeader("Content-Type", "application/json");

  StaticJsonDocument<512> doc;
  doc["mode"] = String(currentMode);
  doc["sub_mode"] = currentSubMode;
  doc["rssi"] = WiFi.RSSI();
  doc["temp"] = temperatureRead();

  JsonObject joystick = doc.createNestedObject("joystick");
  joystick["up"] = joyState[0];
  joystick["down"] = joyState[1];
  joystick["left"] = joyState[2];
  joystick["right"] = joyState[3];

  String payload;
  serializeJson(doc, payload);
  int httpCode = http.POST(payload);
  if (httpCode == 200) {
    // Serial.println("[State] Update Success");
  } else {
    Serial.printf("[State] Update FAILED: %d (%s) -> %s\n", httpCode,
                  http.errorToString(httpCode).c_str(), serverUrl.c_str());
  }
  http.end();
}

void sendHeartbeat() {
  if (WiFi.status() != WL_CONNECTED)
    return;
  HTTPClient http;
  http.begin(serverUrl + "/api/heartbeat");
  http.setTimeout(1000);
  http.addHeader("Content-Type", "application/json");

  StaticJsonDocument<256> doc;
  doc["source"] = "ControlMCU";
  doc["rssi"] = WiFi.RSSI();
  doc["ip"] = WiFi.localIP().toString();
  doc["temp"] = temperatureRead();
  doc["mode"] = String(currentMode);
  doc["sub_mode"] = currentSubMode;
  doc["ssid"] = ssid_active;

  String payload;
  serializeJson(doc, payload);
  int httpCode = http.POST(payload);
  if (httpCode == 200) {
    static unsigned long successCount = 0;
    successCount++;
    if (successCount % 10 ==
        1) { // 10번에 한 번씩 상세 로그 출력 (너무 잦은 로그 방지)
      Serial.printf("[Heartbeat] #%lu OK (IP:%s, RSSI:%d)\n", successCount,
                    WiFi.localIP().toString().c_str(), WiFi.RSSI());
    }
  } else {
    Serial.printf("[Heartbeat] ERROR %d: %s\n", httpCode,
                  http.errorToString(httpCode).c_str());
    Serial.printf("            Target: %s/api/heartbeat\n", serverUrl.c_str());
  }
  http.end();
}

void sendStopRequestToPC() {
  if (WiFi.status() != WL_CONNECTED)
    return;
  HTTPClient http;
  http.begin(serverUrl + "/api/soundmcu/stop");
  http.setTimeout(1000);
  http.addHeader("Content-Type", "application/json");
  int httpCode = http.POST("{}");
  if (httpCode == 200) {
    Serial.println("[Audio] Stop request sent to PC Center successfully.");
  } else {
    Serial.printf("[Audio] Stop request FAILED: %d\n", httpCode);
  }
  http.end();
}

void networkTask(void *pvParameters) {
  setupWifi();
  unsigned long lastSend = 0;
  while (true) {
    bool isAnyJoy = false;
    for (int i = 0; i < 4; i++)
      if (joyState[i])
        isAnyJoy = true;

    unsigned long current = millis();
    unsigned long interval = isAnyJoy ? 120 : 2000;

    if (current - lastSend >= interval || forceStateUpdate) {
      forceStateUpdate = false;
      sendStateToPC();
      sendHeartbeat(); // 표준 하트비트 추가 전송
      lastSend = millis();
    }

    if (Serial.available()) {
      char c = Serial.read();
      if (c == 'w') {
        handleWifiConfigMode();
      } else if (c == 'v' || c == 'V') {
        int vol = Serial.parseInt();
        if (vol >= 0 && vol <= 21) {
          globalVolume = vol;
          audio.setVolume(globalVolume);
          Serial.printf("\n[Audio] Volume set to %d via Serial\n", globalVolume);
        } else {
          Serial.printf("\n[Audio] Invalid Volume %d (Range: 0-21)\n", vol);
        }
      } else if (c == 'u' || c == 'U') {
        if (globalVolume < 21) globalVolume++;
        audio.setVolume(globalVolume);
        Serial.printf("\n[Audio] Volume Up: %d\n", globalVolume);
      } else if (c == 'd' || c == 'D') {
        if (globalVolume > 0) globalVolume--;
        audio.setVolume(globalVolume);
        Serial.printf("\n[Audio] Volume Down: %d\n", globalVolume);
      } else if (c >= '0' && c <= '9') {
        int idx = c - '0';
        if (idx < numStations) {
          currentStationIdx = idx;
          Serial.printf("\n[Audio] Switching to Channel %d: %s\n", idx,
                        stations[idx].name);
          audio.stopSong();
          vTaskDelay(200 / portTICK_PERIOD_MS);
          audio.connecttohost(stations[idx].url);
        }
      } else if (c == '+') {
        currentStationIdx = (currentStationIdx + 1) % numStations;
        Serial.printf("\n[Audio] Next: %s\n", stations[currentStationIdx].name);
        audio.stopSong();
        vTaskDelay(200 / portTICK_PERIOD_MS);
        audio.connecttohost(stations[currentStationIdx].url);
      } else if (c == 'r' || c == 'R' || c == 'o' || c == 'O') {
        // 독일 라디오 켜기 (FFH Germany = stations[0])
        Serial.printf("\n[Audio] Starting German Radio: %s\n",
                      stations[0].name);
        audio.stopSong();
        vTaskDelay(200 / portTICK_PERIOD_MS);
        audio.connecttohost(stations[0].url);
      } else if (c == 's' || c == 'S' || c == 'x' || c == 'X' || c == 'q' || c == 'Q') {
        // 모든 소리/라디오/음성 즉각 정지
        Serial.println("\n[Audio] Stopping ALL Audio immediately...");
        audio.stopSong();
        i2s_zero_dma_buffer(I2S_NUM_0);
        isAiSpeaking = false;
        sendStopRequestToPC();
      }
    }

    vTaskDelay(10 / portTICK_PERIOD_MS);
  }
}

void controlTask(void *pvParameters) {
  bool lastBtn = HIGH;
  unsigned long lastBtnTime = 0;

  int brightness = 0;
  int fade = 5;
  unsigned long lastFade = 0;

  while (true) {
    char lastMode = currentMode;
    int lastSub = currentSubMode;
    bool lastJoy[4];
    for (int i = 0; i < 4; i++)
      lastJoy[i] = joyState[i];

    // --- 3-Position Toggle Switch Logic ---

    bool swA = (digitalRead(TOGGLE_A) == LOW);
    bool swB = (digitalRead(TOGGLE_B) == LOW);

    if (swA) {
      if (currentMode != 'A') {
        currentMode = 'A';
        currentSubMode = 1;
      }
    } else if (swB) {
      if (currentMode != 'B') {
        currentMode = 'B';
        currentSubMode = 1;
      }
    } else {
      // Both HIGH = Center Position
      if (currentMode != 'N') {
        currentMode = 'N';
        currentSubMode = 1;
      }
    }

    // Sub-mode Push (Latching Switch)
    bool btn = (digitalRead(PUSH_BTN) == LOW);
    
    if (btn != (lastBtn == LOW)) {
      currentSubMode = btn ? 2 : 1;
      forceStateUpdate = true; // PC로 즉시 상태 변경 전송 요청
      
      if (btn) {
        Serial.println("[Input] Voice Button Pressed");
      } else {
        Serial.println("[Input] Voice Button Released");
      }
    }
    lastBtn = btn ? LOW : HIGH;

    // Joystick
    joyState[0] = (digitalRead(JOY_UP) == LOW);
    joyState[1] = (digitalRead(JOY_DOWN) == LOW);
    joyState[2] = (digitalRead(JOY_LEFT) == LOW);
    joyState[3] = (digitalRead(JOY_RIGHT) == LOW);

    // --- Debug Output on Change ---
    bool joyChanged = false;
    for (int i = 0; i < 4; i++)
      if (joyState[i] != lastJoy[i])
        joyChanged = true;

    if (currentMode != lastMode || currentSubMode != lastSub || joyChanged) {
      Serial.printf("[Input] Mode:%c Sub:%d Joy:%d%d%d%d\n", currentMode,
                    currentSubMode, joyState[0], joyState[1], joyState[2],
                    joyState[3]);
    }

    // LED Effect (WS2812B NeoPixel via Adafruit_NeoPixel)

    if (millis() - lastFade > 30) {
      brightness += fade;
      if (brightness <= 0 || brightness >= 255)
        fade = -fade;

      // 모드에 따라 색상 변경
      int b = (currentMode == 'N') ? (brightness / 5) : brightness;
      if (currentMode == 'N') {
        strip.setPixelColor(0,
                            strip.Color(b, b / 3, 0)); // 주황색 (NEUTRAL 경고)
      } else if (currentMode == 'A') {
        strip.setPixelColor(0, strip.Color(0, 0, b)); // 파란색 (Robot)
      } else {                                        // B
        strip.setPixelColor(0, strip.Color(0, b, 0)); // 초록색 (Drive)
      }
      strip.show();
      lastFade = millis();
    }

    vTaskDelay(5 / portTICK_PERIOD_MS);
  }
}

#include <WiFiClient.h>
#include <WiFiServer.h>

WiFiServer tcpServer(5002);

void tcpServerTask(void *pvParameters) {
  // WiFi 연결 대기
  while (WiFi.status() != WL_CONNECTED) {
    vTaskDelay(pdMS_TO_TICKS(500));
  }

  tcpServer.begin();
  Serial.println("[TCP Server] Listening on Port 5002 for commands");
  
  while (true) {
    WiFiClient client = tcpServer.available();
    if (client) {
      Serial.println("[TCP Server] PC Center connected");
      String command = "";
      while (client.connected() || client.available()) {
        if (client.available()) {
          char c = client.read();
          if (c == '\n' || c == '\r') {
            if (command.length() > 0) {
              command.trim();
              Serial.printf("[TCP Server] Command received: %s\n", command.c_str());
              
              if (command.startsWith("vol=") || command.startsWith("volume=")) {
                int eqIdx = command.indexOf('=');
                int vol = command.substring(eqIdx + 1).toInt();
                if (vol >= 0 && vol <= 21) {
                  globalVolume = vol;
                  audio.setVolume(globalVolume);
                  Serial.printf("[TCP Server] Volume set to %d\n", globalVolume);
                  client.printf("Volume set to %d\n", globalVolume);
                } else {
                  client.println("Error: Volume must be between 0 and 21");
                }
              }
              else if (command == "status") {
                client.printf("vol=%d\n", globalVolume);
              }
              else if (command == "stop") {
                audio.stopSong();
                i2s_zero_dma_buffer(I2S_NUM_0);
                isAiSpeaking = false;
                Serial.println("[TCP Server] Stop command executed. All audio stopped.");
                client.println("Audio stopped");
              }
              else if (command == "mic_ready") {
                digitalWrite(BUTTON_LED_PIN, HIGH);
                audio.stopSong(); // 오디오 스트리밍 중지
                Serial.println("[TCP Server] Mic Ready");
                client.println("OK");
              }
              else if (command == "mic_done") {
                digitalWrite(BUTTON_LED_PIN, LOW);
                audio.stopSong(); // 오디오 스트리밍 중지
                Serial.println("[TCP Server] Mic Done");
                client.println("OK");
              }
              else if (command.startsWith("radio=")) {
                int eqIdx = command.indexOf('=');
                int idx = command.substring(eqIdx + 1).toInt();
                if (idx >= 0 && idx < numStations) {
                  currentStationIdx = idx;
                  audio.stopSong();
                  vTaskDelay(200 / portTICK_PERIOD_MS);
                  audio.connecttohost(stations[idx].url);
                  Serial.printf("[TCP Server] Switching to Radio Station %d: %s\n", idx, stations[idx].name);
                  client.printf("Radio set to %s\n", stations[idx].name);
                } else {
                  client.println("Error: Radio index out of bounds");
                }
              }
              command = "";
            }
          } else {
            command += c;
          }
        }
        vTaskDelay(5 / portTICK_PERIOD_MS);
      }
      client.stop();
      Serial.println("[TCP Server] Client disconnected");
    }
    vTaskDelay(50 / portTICK_PERIOD_MS);
  }
}

void setup() {
  Serial.begin(115200);
  delay(500); // USB CDC 연결 대기 (Native USB 필수)

  // NeoPixel 초기화
  strip.begin();
  strip.setBrightness(60);
  strip.setPixelColor(0, strip.Color(255, 0, 0)); // 부팅확인: 빨간색
  strip.show();

  Serial.println("[BOOT] ControlMCU Starting...");

  Serial.println("[BOOT] I/O Init...");
  pinMode(JOY_UP, INPUT_PULLUP);
  pinMode(JOY_DOWN, INPUT_PULLUP);
  pinMode(JOY_LEFT, INPUT_PULLUP);
  pinMode(JOY_RIGHT, INPUT_PULLUP);
  pinMode(TOGGLE_A, INPUT_PULLUP);
  pinMode(TOGGLE_B, INPUT_PULLUP);
  pinMode(PUSH_BTN, INPUT_PULLUP);
  pinMode(BUTTON_LED_PIN, OUTPUT);
  digitalWrite(BUTTON_LED_PIN, LOW); // 기본 상태는 꺼짐 (LOW)

  pinMode(AMP_SD, OUTPUT);
  digitalWrite(AMP_SD, HIGH);

  Serial.println("[BOOT] Audio Init...");
  audio.setPinout(I2S_BCLK, I2S_WS, I2S_DOUT);
  audio.setVolume(globalVolume);
  audio.setConnectionTimeout(500, 5000); // 연결 타임아웃 설정 (ms)

  Serial.println("[BOOT] Creating Tasks...");
  // audioTask는 우선순위 1로 낮춰 setup을 선점하지 않음
  xTaskCreatePinnedToCore(audioTask, "AudioTask", 16384, NULL, 1, NULL, 1);
  xTaskCreatePinnedToCore(networkTask, "NetworkTask", 8192, NULL, 2,
                          &hNetworkTask, 0); // P1 -> P2
  xTaskCreatePinnedToCore(controlTask, "ControlTask", 4096, NULL, 3,
                          &hControlTask, 1); // P2 -> P3

  // AI 음성 수신 태스크 (가장 높은 우선순위 유지)
  xTaskCreatePinnedToCore(audioReceiverTask, "AudioReceiver", 8192, NULL, 5,
                          NULL, 1);

  // TCP 서버 태스크 시작 (PC Center 볼륨 제어용)
  xTaskCreatePinnedToCore(tcpServerTask, "TcpServerTask", 8192, NULL, 2, NULL, 0);

  // 프리미엄 부팅 멜로디 재생
  delay(100);
  playBootMelody();

  Serial.println("[BOOT] Setup Done!");
}

void loop() { vTaskDelete(NULL); }
