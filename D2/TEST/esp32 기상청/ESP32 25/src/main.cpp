#include <Arduino.h>
#include <SPI.h>
#include <TFT_eSPI.h>
#include <XPT2046_Touchscreen.h>

// 터치 스크린 설정
#define XPT2046_IRQ 36
#define XPT2046_MOSI 32
#define XPT2046_MISO 39
#define XPT2046_CLK 25
#define XPT2046_CS 33

SPIClass touchSPI = SPIClass(VSPI);
XPT2046_Touchscreen touch(XPT2046_CS, XPT2046_IRQ);
TFT_eSPI tft = TFT_eSPI();

// 하드웨어 정보 출력 및 화면 표시를 위한 함수
void displayHardwareInfo(int w, int h) {
    Serial.println("\n--- Hardware Information ---");
    
    // 1. Flash 정보
    uint32_t flashSize = ESP.getFlashChipSize();
    Serial.printf("Flash size: %u bytes (%u MB)\n", flashSize, flashSize / (1024 * 1024));
    
    // 2. PSRAM 정보
    uint32_t psramSize = ESP.getPsramSize();
    Serial.printf("PSRAM size: %u bytes (%u MB)\n", psramSize, psramSize / (1024 * 1024));
    
    // 3. CPU 클럭
    uint32_t cpuFreq = getCpuFrequencyMhz();
    Serial.printf("CPU Frequency: %u MHz\n", cpuFreq);

    // 4. Chip ID
    uint32_t chipId = 0;
    for(int i=0; i<17; i=i+8) {
      chipId |= ((ESP.getEfuseMac() >> (40 - i)) & 0xff) << i;
    }
    Serial.printf("ESP32 Chip ID: %08X\n", chipId);
    Serial.println("---------------------------\n");

    // 화면에 출력
    tft.setTextColor(TFT_GREEN, TFT_BLACK);
    char buf[64];
    
    sprintf(buf, "CPU: %u MHz", cpuFreq);
    tft.drawCentreString(buf, w / 2, 75, 2);
    
    sprintf(buf, "Flash: %u MB", flashSize / (1024 * 1024));
    tft.drawCentreString(buf, w / 2, 95, 2);
    
    if (psramSize > 0) {
        sprintf(buf, "PSRAM: %u MB", psramSize / (1024 * 1024));
    } else {
        sprintf(buf, "PSRAM: None");
    }
    tft.drawCentreString(buf, w / 2, 115, 2);

    sprintf(buf, "ChipID: %08X", chipId);
    tft.drawCentreString(buf, w / 2, 135, 2);
}

void setup() {
    Serial.begin(115200);
    delay(1000);

    // 디스플레이 초기화
    tft.init();
    tft.setRotation(3); // 반시계 방향 90도 회전
    tft.fillScreen(TFT_BLACK);
    
    int w = 320; 
    int h = 240; 

    tft.setTextColor(TFT_YELLOW, TFT_BLACK);
    tft.drawCentreString("ESP32 Hardware Info", w / 2, 20, 4); 
    tft.drawCentreString("(CYD 2432S028)", w / 2, 50, 2);
    
    // 하드웨어 정보 표시
    displayHardwareInfo(w, h);
    
    tft.setTextColor(TFT_WHITE, TFT_BLACK);
    tft.drawRect(20, 160, w - 40, 65, TFT_WHITE);
    tft.drawCentreString("Touch screen to test", w / 2, 190, 2);

    // 터치 초기화
    touchSPI.begin(XPT2046_CLK, XPT2046_MISO, XPT2046_MOSI, XPT2046_CS);
    if (!touch.begin(touchSPI)) {
        Serial.println("Touch screen initialization FAILED!");
    } else {
        Serial.println("Touch screen initialized.");
    }
}

void loop() {
    if (touch.touched()) {
        TS_Point p = touch.getPoint();
        int w = 320;
        int h = 240;
        
        Serial.printf("Touch: x=%d, y=%d, z=%d\n", p.x, p.y, p.z);
        
        tft.fillRect(w/2 - 50, 215, 100, 15, TFT_BLACK); 
        tft.setTextColor(TFT_CYAN, TFT_BLACK);
        char buf[32];
        sprintf(buf, "X:%d Y:%d", p.x, p.y);
        tft.drawCentreString(buf, w/2, 215, 2);
        
        // 터치 좌표 맵핑 (상하좌우 반전 적용된 최신 공식 - 실수 수정)
        int screen_x = map(p.x, 3700, 185, 0, w);
        int screen_y = map(p.y, 3850, 280, 0, h);
        
        screen_x = constrain(screen_x, 0, w - 1);
        screen_y = constrain(screen_y, 0, h - 1);
        
        tft.fillCircle(screen_x, screen_y, 2, TFT_MAGENTA);
    }
    delay(20);
}
