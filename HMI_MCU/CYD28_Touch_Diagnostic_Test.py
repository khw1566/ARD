# -*- coding: utf-8 -*-
# gen_test.py - CYD28 library-based touch diagnostic
code = r"""// CYD Diagnostic v5 - CYD28_TouchscreenR (software SPI, dedicated pins)
// Touch pins: MOSI=32 MISO=39 CLK=25 CS=33 IRQ=36  (NOT shared with display!)
// Display SPI: MOSI=13 MISO=12 CLK=14 CS=15        (HSPI via TFT_eSPI)
#include <Arduino.h>
#include <TFT_eSPI.h>
#include "CYD28_TouchscreenR.h"

#define BL_PIN 21

TFT_eSPI      tft   = TFT_eSPI();
CYD28_TouchR  touch = CYD28_TouchR(320, 240); // portrait: swap to get correct x(0-240) y(0-320)

void drawPattern() {
    tft.fillRect(  0,   0, 120, 140, TFT_RED);
    tft.fillRect(120,   0, 120, 140, TFT_GREEN);
    tft.fillRect(  0, 140, 120, 140, TFT_BLUE);
    tft.fillRect(120, 140, 120, 140, TFT_YELLOW);
    tft.setTextSize(2);
    tft.setTextColor(TFT_WHITE, TFT_RED);    tft.setCursor(4,   4); tft.print("TL");
    tft.setTextColor(TFT_BLACK, TFT_GREEN);  tft.setCursor(172,  4); tft.print("TR");
    tft.setTextColor(TFT_WHITE, TFT_BLUE);   tft.setCursor(4, 256); tft.print("BL");
    tft.setTextColor(TFT_BLACK, TFT_YELLOW); tft.setCursor(172,256); tft.print("BR");
    tft.fillRect(0, 282, 240, 38, TFT_BLACK);
    tft.setTextSize(1); tft.setTextColor(TFT_WHITE, TFT_BLACK);
    tft.setCursor(2, 284); tft.print("Touch corners. Dot = where library thinks");
    tft.setCursor(2, 296); tft.print("r=restart  p=redraw  rot=rotation(0-3)");
}

void setup() {
    Serial.begin(115200); delay(400);
    Serial.println("\n=== CYD Diag v5 (CYD28_TouchscreenR) ===");
    Serial.println("Touch pins: MOSI=32 MISO=39 CLK=25 CS=33 IRQ=36");
    Serial.println("Display:    MOSI=13 MISO=12 CLK=14 CS=15");

    pinMode(BL_PIN, OUTPUT); digitalWrite(BL_PIN, HIGH);

    tft.init(); tft.setRotation(0); tft.invertDisplay(true);
    tft.fillScreen(TFT_BLACK);

    tft.setTextSize(3); tft.setTextColor(TFT_WHITE, TFT_BLACK);
    tft.setCursor(0, 0); tft.print("DISPLAY OK");
    tft.setTextSize(2); tft.setTextColor(TFT_CYAN, TFT_BLACK);
    tft.setCursor(0, 34); tft.print("Portrait 240x320");
    tft.setCursor(0, 58); tft.print("Init touch...");

    // CYD28 uses software SPI on dedicated pins (no HSPI conflict!)
    touch.begin();
    // Portrait mode: try rotation 0 first
    // 0=PORT0  1=LANDSC0  2=PORT1  3=LANDSC1
    touch.setRotation(0);

    tft.setTextColor(TFT_GREEN, TFT_BLACK);
    tft.setCursor(0, 82); tft.print("Touch OK (sw SPI)");
    delay(1000);
    drawPattern();

    Serial.println("Ready. Touch screen corners.");
    Serial.println("Commands: r=restart  p=redraw  0/1/2/3=setRotation");
}

unsigned long lastT = 0;

void loop() {
    if (Serial.available()) {
        char c = Serial.read();
        if (c == 'r') { delay(200); ESP.restart(); }
        else if (c == 'p') { drawPattern(); }
        else if (c >= '0' && c <= '3') {
            int rot = c - '0';
            touch.setRotation(rot);
            Serial.printf("Rotation set to %d\n", rot);
            drawPattern();
        }
    }

    if (!touch.touched()) return;
    if (millis() - lastT < 150) return;
    lastT = millis();

    CYD28_TS_Point p = touch.getPointScaled();
    CYD28_TS_Point r = touch.getPointRaw();

    Serial.printf("SCALED x=%3d y=%3d z=%d  |  RAW x=%4d y=%4d\n",
                  p.x, p.y, p.z, r.x, r.y);

    // Clamp to screen
    int sx = constrain(p.x, 0, 239);
    int sy = constrain(p.y, 0, 319);

    // Touch feedback circle
    tft.fillCircle(sx, sy, 12, TFT_WHITE);
    tft.fillCircle(sx, sy,  9, TFT_MAGENTA);

    // Info bar
    tft.fillRect(0, 282, 240, 38, TFT_BLACK);
    tft.setTextSize(1); tft.setTextColor(TFT_WHITE, TFT_BLACK);
    tft.setCursor(2, 283); tft.printf("SCALED x=%d y=%d z=%d", p.x, p.y, p.z);
    tft.setCursor(2, 293); tft.printf("RAW    x=%d y=%d", r.x, r.y);
    tft.setCursor(2, 303); tft.print("Commands: r=restart  p=redraw  0-3=rotation");
    tft.setCursor(2, 313); tft.printf("Rotation: %d  (0=PORT0 2=PORT1)", touch.isrWake?1:0);
}
"""
with open("src/main.cpp", "w", encoding="utf-8") as f:
    f.write(code)
print("OK lines:", len(code.splitlines()))
