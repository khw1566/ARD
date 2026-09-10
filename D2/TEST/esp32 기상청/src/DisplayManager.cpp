#include "DisplayManager.h"
#include "HancomSans16.h"
#include "HancomSans24.h"
#include "HancomSans40.h"

// --- 커스텀 폰트 출력 구현 ---
void DisplayManager::drawCustomText(const char* text, int x, int y, uint16_t color, const uint8_t* bitmap, const void* glyphs, int numGlyphs) {
    const CustomGFXGlyph* glyphArray = (const CustomGFXGlyph*)glyphs;
    int cursor_x = x;
    
    // UTF-8 디코딩 및 출력
    int i = 0;
    while (text[i]) {
        uint32_t code = 0;
        unsigned char c = text[i];
        if (c < 0x80) { code = c; i++; }
        else if ((c & 0xE0) == 0xC0) { code = ((text[i]&0x1F)<<6) | (text[i+1]&0x3F); i += 2; }
        else if ((c & 0xF0) == 0xE0) { code = ((text[i]&0x0F)<<12) | ((text[i+1]&0x3F)<<6) | (text[i+2]&0x3F); i += 3; }
        else { i++; continue; }

        for (int g = 0; g < numGlyphs; g++) {
            if (glyphArray[g].code == code) {
                int gw = glyphArray[g].width;
                int gh = glyphArray[g].height;
                int ofs = glyphArray[g].bitmapOffset;
                
                int bitIndex = 0;
                for (int yy = 0; yy < gh; yy++) {
                    for (int xx = 0; xx < gw; xx++) {
                        uint8_t byteVal = bitmap[ofs + (bitIndex / 8)];
                        if (byteVal & (0x80 >> (bitIndex % 8))) {
                            _tft->drawPixel(cursor_x + glyphArray[g].xOffset + xx, y + glyphArray[g].yOffset + yy, color);
                        }
                        bitIndex++;
                    }
                }
                cursor_x += glyphArray[g].xAdvance + 1;
                break;
            }
        }
    }
}

DisplayManager::DisplayManager(TFT_eSPI* tft, CYD28_TouchR* touch) 
    : _tft(tft), _touch(touch), _month(-1), _day(-1), _hour(-1), _minute(-1), _second(-1), _animLastTime(0), _animFrame(0), _currentIcon(ICON_SUN), _currentRegionIdx(1), _currentRegionName("울산"), _selectedDay(0) {
    _currentWeather.isValid = false;
    _currentWeather.temperature = "--";
    _currentWeather.skyState = "1";
    _currentWeather.rainState = "0";
}

void DisplayManager::begin() {
    _tft->begin();
    _tft->invertDisplay(true); // 하드웨어 초기화 후 호출하여 확실히 적용
    _tft->setRotation(3);
    _tft->fillScreen(TFT_BLACK);
    _tft->setTextColor(TFT_WHITE, TFT_BLACK);
}

void DisplayManager::setWeather(const WeatherData& data) {
    _currentWeather = data;
    _fullForecast.now = data; // 현재 데이터 업데이트
    
    if (_selectedDay == 0) {
        updateInternalState(data);
    }
}

void DisplayManager::setForecast(const ForecastData& forecast) {
    _fullForecast = forecast;
    if (_selectedDay != 0) {
        WeatherData target = (_selectedDay == 1) ? forecast.tomorrow : forecast.dayAfter;
        updateInternalState(target);
    }
}

void DisplayManager::updateInternalState(const WeatherData& data) {
    // 비/눈 상태 (0: 없음, 1: 비, 2: 비/눈, 3: 눈, 5: 빗방울, 6: 빗방울눈날림, 7: 눈날림)
    int pty = data.rainState.toInt();
    if (pty == 1 || pty == 2 || pty == 5 || pty == 6) {
        _currentIcon = ICON_RAIN;
    } else if (pty == 3 || pty == 7) {
        _currentIcon = ICON_SNOW;
    } else {
        // 하늘 상태 (1: 맑음, 3: 구름많음, 4: 흐림)
        int sky = data.skyState.toInt();
        if (sky >= 3) {
            _currentIcon = ICON_CLOUD;
        } else {
            _currentIcon = ICON_SUN;
        }
    }
    
    _tft->fillScreen(TFT_BLACK); 
    
    // 배경색 결정
    uint16_t bgColor = _tft->color565(135, 206, 235); // Sky Blue
    if (_currentIcon == ICON_CLOUD) bgColor = _tft->color565(100, 150, 200);
    else if (_currentIcon == ICON_RAIN) bgColor = _tft->color565(80, 80, 80);
    else if (_currentIcon == ICON_SNOW) bgColor = _tft->color565(200, 200, 200);

    _tft->fillScreen(TFT_BLACK); 
    
    // 전체 배경 (상단 0부터 하단 탭 시작 전 200까지 모두 칠하기)
    _tft->fillRect(0, 0, 320, 200, bgColor); 

    renderTime();
    renderWeatherText();
    drawTabs();
    drawSubTabs();
    renderWeatherIcon(); // 초기 아이콘 렌더링
}

void DisplayManager::updateTime(struct tm* timeinfo) {
    if (_minute != timeinfo->tm_min || _hour != timeinfo->tm_hour || _day != timeinfo->tm_mday) {
        _month = timeinfo->tm_mon + 1;
        _day = timeinfo->tm_mday;
        _hour = timeinfo->tm_hour;
        _minute = timeinfo->tm_min;
        _second = timeinfo->tm_sec;
        renderTime();
        renderWeatherText(); // 시간이 지날때 화면 덮어쓰기 방지
    }
}

void DisplayManager::renderTime() {
    if (_hour < 0) return;
    
    // 현재 날씨에 맞는 배경색 계산 (시계 지우기용)
    uint16_t bgColor = _tft->color565(135, 206, 235); // Sky Blue
    if (_currentIcon == ICON_CLOUD) bgColor = _tft->color565(100, 150, 200);
    else if (_currentIcon == ICON_RAIN) bgColor = _tft->color565(80, 80, 80);
    else if (_currentIcon == ICON_SNOW) bgColor = _tft->color565(200, 200, 200);

    // 시계 영역만 현재 배경색으로 지우기 (블랙 대신 블루 계열)
    _tft->fillRect(0, 0, 165, 60, bgColor); 

    char buf[32];
    // 1. 날짜 표시 (M월 D일)
    sprintf(buf, "%d월 %d일", _month, _day);
    drawCustomText(buf, 20, 25, TFT_WHITE, HancomSans_SemiBold_0_16ptBitmaps, HancomSans_SemiBold_0_16ptGlyphs, HancomSans_SemiBold_0_16pt_NumGlyphs);

    // 2. 시간 표시 (HH:MM)
    sprintf(buf, "%02d:%02d", _hour, _minute);
    drawCustomText(buf, 20, 55, TFT_WHITE, HancomSans_SemiBold_0_40ptBitmaps, HancomSans_SemiBold_0_40ptGlyphs, HancomSans_SemiBold_0_40pt_NumGlyphs);
}

void DisplayManager::renderWeatherText() {
    WeatherData displayData = _currentWeather;
    if (_selectedDay == 1) displayData = _fullForecast.tomorrow;
    else if (_selectedDay == 2) displayData = _fullForecast.dayAfter;

    // 배경색은 이미 updateInternalState에서 칠해짐
    if (!displayData.isValid) {
        drawCustomText("데이터 수신중...", 20, 100, TFT_WHITE, HancomSans_SemiBold_0_16ptBitmaps, HancomSans_SemiBold_0_16ptGlyphs, HancomSans_SemiBold_0_16pt_NumGlyphs);
        return;
    }
    
    char buf[64];
    // --- 0. 지역명 및 예보 시점 ---
    String title = _currentRegionName;
    if (_selectedDay == 1) title += " (내일)";
    else if (_selectedDay == 2) title += " (모레)";
    drawCustomText(title.c_str(), 20, 80, TFT_WHITE, HancomSans_SemiBold_0_24ptBitmaps, HancomSans_SemiBold_0_24ptGlyphs, HancomSans_SemiBold_0_24pt_NumGlyphs);
    
    // --- 1. 온도 ---
    sprintf(buf, "%s ℃", displayData.temperature.c_str());
    drawCustomText(buf, 20, 110, TFT_YELLOW, HancomSans_SemiBold_0_24ptBitmaps, HancomSans_SemiBold_0_24ptGlyphs, HancomSans_SemiBold_0_24pt_NumGlyphs);

    // --- 2. 습도 및 강수 ---
    int y_pos = 145;
    sprintf(buf, "습도: %s %%", displayData.humidity.c_str());
    drawCustomText(buf, 20, y_pos, TFT_WHITE, HancomSans_SemiBold_0_16ptBitmaps, HancomSans_SemiBold_0_16ptGlyphs, HancomSans_SemiBold_0_16pt_NumGlyphs);
    y_pos += 25;

    if (displayData.rainAmount != "강수없음" && displayData.rainAmount != "0") {
        sprintf(buf, "강수: %s", displayData.rainAmount.c_str());
        drawCustomText(buf, 20, y_pos, TFT_CYAN, HancomSans_SemiBold_0_16ptBitmaps, HancomSans_SemiBold_0_16ptGlyphs, HancomSans_SemiBold_0_16pt_NumGlyphs);
    } else {
        // 비/눈 안올 때는 하늘 상태 명시
        String skyMsg = "맑음";
        if (displayData.skyState == "3") skyMsg = "구름많음";
        else if (displayData.skyState == "4") skyMsg = "흐림";
        
        drawCustomText(skyMsg.c_str(), 20, y_pos, _tft->color565(220, 220, 220), HancomSans_SemiBold_0_16ptBitmaps, HancomSans_SemiBold_0_16ptGlyphs, HancomSans_SemiBold_0_16pt_NumGlyphs);
    }
    y_pos += 25;

    // --- 3. 풍속 ---
    if (_selectedDay == 0) {
        sprintf(buf, "풍속: %s m/s", displayData.windSpeed.c_str());
        drawCustomText(buf, 20, y_pos, TFT_WHITE, HancomSans_SemiBold_0_16ptBitmaps, HancomSans_SemiBold_0_16ptGlyphs, HancomSans_SemiBold_0_16pt_NumGlyphs);
    }
}

void DisplayManager::updateAnimation() {
    uint32_t now = millis();
    if (now - _animLastTime > 200) { // 200ms 단위 프레임 이동
        _animLastTime = now;
        _animFrame = (_animFrame + 1) % 8;
        renderWeatherIcon();
    }
}

void DisplayManager::showUpdating() {
    // 버튼 아래쪽 빈 공간에 업데이트 중 표시 (기존 상단 덮어쓰기 방지)
    _tft->fillRect(165, 200, 150, 40, _tft->color565(50, 50, 50)); 
    _tft->setTextDatum(TC_DATUM);
    _tft->setTextSize(2);
    _tft->setTextColor(TFT_GREEN);
    _tft->drawString("Updating...", 240, 210);
}

void DisplayManager::renderWeatherIcon() {
    WeatherData displayData = _currentWeather;
    if (_selectedDay == 1) displayData = _fullForecast.tomorrow;
    else if (_selectedDay == 2) displayData = _fullForecast.dayAfter;

    if (!displayData.isValid) return;
    
    int cx = 240, cy = 80; // 우측 중앙
    
    // 바탕화면에 맞는 색상 결정
    uint16_t bgColor = _tft->color565(135, 206, 235); // Sky Blue
    if (_currentIcon == ICON_CLOUD) bgColor = _tft->color565(100, 150, 200);
    else if (_currentIcon == ICON_RAIN) bgColor = _tft->color565(80, 80, 80);
    else if (_currentIcon == ICON_SNOW) bgColor = _tft->color565(200, 200, 200);

    // 아이콘 영역만 지우기 (좌측 텍스트 영역 침범하지 않도록 x=180부터 지움)
    _tft->fillRect(180, 0, 140, 145, bgColor); 
    
    drawRefreshButton(); // 새로고침 버튼 렌더링
    
    switch (_currentIcon) {
        case ICON_SUN: drawSun(cx, cy, 30, _animFrame); break;
        case ICON_CLOUD: drawCloud(cx, cy, 60, 30, TFT_WHITE); break;
        case ICON_RAIN: drawCloud(cx, cy-10, 60, 30, TFT_DARKGREY); drawRainDrops(cx, cy+20, _animFrame); break;
        case ICON_SNOW: drawCloud(cx, cy-10, 60, 30, TFT_WHITE); drawSnowFlakes(cx, cy+20, _animFrame); break;
    }
}

void DisplayManager::drawSun(int x, int y, int radius, int frame) {
    _tft->fillCircle(x, y, radius, TFT_YELLOW);
    for (int i=0; i<8; i++) {
        float angle = (i * 45 + frame * 10) * DEG_TO_RAD; // 프레임에 따라 회전
        int x1 = x + cos(angle) * (radius + 5);
        int y1 = y + sin(angle) * (radius + 5);
        int x2 = x + cos(angle) * (radius + 15);
        int y2 = y + sin(angle) * (radius + 15);
        _tft->drawLine(x1, y1, x2, y2, TFT_ORANGE);
    }
}

void DisplayManager::drawCloud(int x, int y, int w, int h, int color) {
    _tft->fillCircle(x - 20, y + 10, 20, color);
    _tft->fillCircle(x + 20, y + 10, 20, color);
    _tft->fillCircle(x, y - 5, 25, color);
    _tft->fillRect(x - 20, y + 10, 40, 20, color);
}

void DisplayManager::drawRainDrops(int x, int y, int frame) {
    int offsetY = (frame * 4) % 15;
    _tft->drawLine(x - 20, y + offsetY, x - 20, y + offsetY + 5, TFT_BLUE);
    _tft->drawLine(x, y + offsetY + 8, x, y + offsetY + 13, TFT_BLUE);
    _tft->drawLine(x + 20, y + offsetY, x + 20, y + offsetY + 5, TFT_BLUE);
}

void DisplayManager::drawSnowFlakes(int x, int y, int frame) {
    int offsetY = (frame * 3) % 15;
    _tft->fillCircle(x - 20, y + offsetY, 2, TFT_WHITE);
    _tft->fillCircle(x, y + offsetY + 5, 2, TFT_WHITE);
    _tft->fillCircle(x + 20, y + offsetY, 2, TFT_WHITE);
}

void DisplayManager::drawRefreshButton() {
    int cx = REFRESH_BTN_X + REFRESH_BTN_W / 2;
    int cy = REFRESH_BTN_Y + REFRESH_BTN_H / 2;
    int r = REFRESH_BTN_W / 2;

    // 1. 버튼 배경 (고급스러운 원형 베젤 효과)
    _tft->fillCircle(cx, cy, r, _tft->color565(40, 40, 45));       // 기본 배경
    _tft->drawCircle(cx, cy, r, _tft->color565(100, 100, 110));   // 외곽선
    _tft->drawCircle(cx, cy, r - 1, _tft->color565(180, 180, 190)); // 하이라이트 베젤

    // 2. 새로고침 화살표 (세련된 원형 스타일)
    // 원호 그리기 (두께감을 위해 2개 겹침)
    _tft->drawCircle(cx, cy, 9, TFT_WHITE);
    _tft->drawCircle(cx, cy, 8, TFT_WHITE);
    
    // 화살표 틈새 (배경색으로 덮어쓰기)
    _tft->fillRect(cx - 3, cy - 12, 12, 8, _tft->color565(40, 40, 45));
    
    // 화살표 촉 (삼각형)
    _tft->fillTriangle(cx + 6, cy - 13, cx + 6, cy - 2, cx + 13, cy - 7, TFT_WHITE);
}

void DisplayManager::drawTabs() {
    const char* names[] = {"서울", "울산", "강원", "광주", "제주"};
    uint16_t activeColor = _tft->color565(0, 120, 215);
    uint16_t inactiveColor = _tft->color565(40, 40, 40);

    for (int i = 0; i < 5; i++) {
        int tx = i * TAB_W;
        uint16_t color = (i == _currentRegionIdx) ? activeColor : inactiveColor;
        
        _tft->fillRect(tx, TAB_Y, TAB_W, TAB_H, color);
        _tft->drawRect(tx, TAB_Y, TAB_W, TAB_H, TFT_SILVER);
        
        // 지역명 텍스트 (가운데 정렬 비슷하게)
        drawCustomText(names[i], tx + 10, TAB_Y + 28, TFT_WHITE, HancomSans_SemiBold_0_16ptBitmaps, HancomSans_SemiBold_0_16ptGlyphs, HancomSans_SemiBold_0_16pt_NumGlyphs);
    }
}

void DisplayManager::drawSubTabs() {
    const char* names[] = {"지금", "내일", "모레"};
    uint16_t activeColor = _tft->color565(0, 150, 136); // Teal
    uint16_t inactiveColor = _tft->color565(60, 60, 60);

    for (int i = 0; i < 3; i++) {
        int tx = SUB_TAB_X_START + (i * SUB_TAB_W);
        uint16_t color = (i == _selectedDay) ? activeColor : inactiveColor;
        
        _tft->fillRoundRect(tx, SUB_TAB_Y, SUB_TAB_W - 2, SUB_TAB_H, 3, color);
        _tft->drawRoundRect(tx, SUB_TAB_Y, SUB_TAB_W - 2, SUB_TAB_H, 3, TFT_SILVER);
        
        drawCustomText(names[i], tx + 8, SUB_TAB_Y + 22, TFT_WHITE, HancomSans_SemiBold_0_16ptBitmaps, HancomSans_SemiBold_0_16ptGlyphs, HancomSans_SemiBold_0_16pt_NumGlyphs);
    }
}

bool DisplayManager::checkTouch() {
    return getTouchAction() != -1;
}

int DisplayManager::getTouchAction() {
    if (!_touch->touched()) return -1;
    
    CYD28_TS_Point p = _touch->getPointScaled();
    
    // 1. 새로고침 버튼 
    if (p.x >= REFRESH_BTN_X - 10 && p.x <= REFRESH_BTN_X + REFRESH_BTN_W + 10 && 
        p.y >= REFRESH_BTN_Y - 10 && p.y <= REFRESH_BTN_Y + REFRESH_BTN_H + 10) {
        Serial.println("[Touch] Refresh Button Pressed!");
        return 5; 
    }
    
    // 2. 하단 메인 탭 (지역)
    if (p.y >= TAB_Y) {
        int idx = p.x / TAB_W;
        if (idx >= 0 && idx < 5) {
            if (idx != _currentRegionIdx) {
                _currentRegionIdx = idx;
                const char* names[] = {"서울", "울산", "강원", "광주", "제주"};
                _currentRegionName = names[idx];
                Serial.printf("[Touch] Region Tab Changed: %s\n", _currentRegionName.c_str());
                return idx;
            }
        }
    }

    // 3. 서브 탭 (지금/내일/모레)
    if (p.y >= SUB_TAB_Y && p.y < TAB_Y && p.x >= SUB_TAB_X_START) {
        int idx = (p.x - SUB_TAB_X_START) / SUB_TAB_W;
        if (idx >= 0 && idx < 3) {
            if (idx != _selectedDay) {
                _selectedDay = idx;
                Serial.printf("[Touch] Forecast Day Changed: %d\n", idx);
                // 즉시 화면 갱신을 위해 데이터 재적용
                WeatherData target = _fullForecast.now;
                if (idx == 1) target = _fullForecast.tomorrow;
                else if (idx == 2) target = _fullForecast.dayAfter;
                updateInternalState(target);
                return 10 + idx; // 10: Now, 11: Tomorrow, 12: Day After
            }
        }
    }
    
    return -1;
}
