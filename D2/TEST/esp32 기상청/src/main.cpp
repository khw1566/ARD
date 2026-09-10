#include <Arduino.h>
#include <WiFi.h>
#include <time.h>
#include <TFT_eSPI.h>
#include <CYD28_TouchscreenR.h>
#include <SPI.h>

#include "WeatherAPI.h"
#include "DisplayManager.h"

// --- 사용자 설정 ---
const char* ssid = "A35_m";
const char* password = "00000000";

// 기상청 데이터 포털 API 키 및 지역 (울산 무거동 102, 84)
const char* apiKey = "f3e0a628cb33471c8e0e7d78ce1b79943ad6aef826196f918c387471b155f1db";
const int nx = 102;
const int ny = 84;

// --- 전역 객체 ---
TFT_eSPI tft = TFT_eSPI();
CYD28_TouchR touch = CYD28_TouchR(320, 240); // 320x240 해상도

WeatherAPI weatherAPI(apiKey, nx, ny);
DisplayManager display(&tft, &touch);

// 5개 지역 정보 정의
WeatherRegion regions[] = {
    {"서울", 60, 127},
    {"울산", 102, 84},
    {"강원", 73, 134},
    {"광주", 58, 74},
    {"제주", 52, 38}
};
int currentRegionIdx = 1; // 기본값 울산

// 타이머 및 상태 관리
uint32_t lastWeatherUpdate = 0;
uint32_t lastTouchTime = 0; // 중복 터치 방지용
const uint32_t WEATHER_UPDATE_INTERVAL = 10 * 60 * 1000; // 10분

WeatherData weatherData;
ForecastData forecastData;

void updateWeather() {
    Serial.println("[Weather] Updating all weather data...");
    display.showUpdating();
    
    // 1. 초단기예보 (현재)
    if (weatherAPI.fetchWeather(weatherData)) {
        Serial.println("[Weather] Current weather success.");
        display.setWeather(weatherData);
    } else {
        Serial.println("[Weather] Current weather failed.");
    }
    
    // 2. 단기예보 (내일/모레)
    if (weatherAPI.fetchForecast(forecastData)) {
        Serial.println("[Weather] Forecast data success.");
        display.setForecast(forecastData);
    } else {
        Serial.println("[Weather] Forecast data failed.");
    }
}

void setup() {
    Serial.begin(115200);
    delay(1000); // 시리얼 모니터 지연 대기
    Serial.println("\n\n--- [ESP32 Weather Station Starting] ---");

    // 백라이트 켜기 (CYD 보드 흰화면 방지)
    Serial.println("[Init] Turning on Backlight...");
    pinMode(21, OUTPUT);
    digitalWrite(21, HIGH);

    // TFT 화면 우선 초기화 (내부적으로 SPI 버스 초기화)
    Serial.println("[Init] TFT Display & SPI bus initialization...");
    display.begin();
    Serial.println("[Init] TFT Initialized OK!");

    // 터치 모듈 소프트웨어 초기화 (3=Landscape)
    Serial.println("[Init] CYD28 TouchscreenR initialization...");
    touch.begin();
    touch.setRotation(3); 
    
    // WiFi 연결
    Serial.print("[WiFi] Connecting to ");
    Serial.println(ssid);
    
    tft.setTextSize(2);
    tft.setTextColor(TFT_WHITE, TFT_BLACK);
    tft.drawString("Connecting to WiFi...", 10, 10);
    
    WiFi.begin(ssid, password);
    while (WiFi.status() != WL_CONNECTED) {
        delay(500);
        Serial.print(".");
    }
    tft.fillScreen(TFT_BLACK);
    tft.drawString("WiFi Connected!", 10, 10);
    Serial.println("\n[WiFi] Connected successfully!");
    Serial.print("[WiFi] IP Address: ");
    Serial.println(WiFi.localIP());

    // NTP 동기화 (한국 시간 기준 UTC+9)
    Serial.println("[NTP] Syncing with time servers...");
    configTime(9 * 3600, 0, "pool.ntp.org", "time.nist.gov");
    tft.drawString("Syncing Time...", 10, 40);
    
    struct tm timeinfo;
    while (!getLocalTime(&timeinfo)) {
        delay(500);
        Serial.print("*");
    }
    tft.fillScreen(TFT_BLACK);
    Serial.println("\n[NTP] Time Sync Completed!");

    // 초기 날씨 데이터 로드
    updateWeather();
    
    Serial.println("--- [Setup Complete, Entering Loop] ---");
}

void loop() {
    // 1. 애니메이션 루프 (매 프레임 빠르게 처리)
    display.updateAnimation();

    // 2. 터치 감지 후 즉시 동작 (지역 전환 또는 새로고침)
    int action = display.getTouchAction();
    if (action != -1) {
        uint32_t currentMillis = millis();
        // 1초 이내의 연속 터치 무시 (탭 전환 속도를 위해 1초로 단축)
        if (currentMillis - lastTouchTime > 1000) {
            lastTouchTime = currentMillis;
            
            if (action >= 0 && action < 5) {
                // 지역 탭 전환
                currentRegionIdx = action;
                weatherAPI.setLocation(regions[action].nx, regions[action].ny);
                Serial.printf("[Action] Region Switch to %s requested.\n", regions[action].name);
                updateWeather(); // 지역 변경 시 모든 데이터 갱신
            } else if (action == 5) {
                // 새로고침 버튼
                Serial.println("[Action] Manual Refresh requested.");
                updateWeather();
            } else if (action >= 10 && action <= 12) {
                // 예보 시점 전환 (지금/내일/모레)
                // DisplayManager 내부에서 이미 상태 변경 및 화면 갱신을 처리함
                Serial.printf("[Action] Forecast Day Tab %d selected.\n", action - 10);
            }
        }
    }

    // 3. 현재 시간 업데이트
    struct tm timeinfo;
    if (getLocalTime(&timeinfo)) {
        display.updateTime(&timeinfo);
    }

    // 4. 주기적(10분) 날씨 업데이트
    uint32_t now = millis();
    if (now - lastWeatherUpdate > WEATHER_UPDATE_INTERVAL) {
        lastWeatherUpdate = now;
        updateWeather();
    }
    
    delay(10); // 시스템의 Watchdog 양보
}
