#ifndef DISPLAYMANAGER_H
#define DISPLAYMANAGER_H

#include <TFT_eSPI.h>
#include <CYD28_TouchscreenR.h>
#include "WeatherAPI.h"

// 날씨 아이콘 타입
enum WeatherIconType { ICON_SUN, ICON_CLOUD, ICON_RAIN, ICON_SNOW };

// 지역 정보 구조체
struct WeatherRegion {
    const char* name;
    int nx;
    int ny;
};

class DisplayManager {
public:
    DisplayManager(TFT_eSPI* tft, CYD28_TouchR* touch);
    void begin();
    
    // 데이터 갱신
    void setWeather(const WeatherData& data);
    void setForecast(const ForecastData& forecast);
    void updateTime(struct tm* timeinfo);
    
    // 루프 내 애니메이션 업데이트
    void updateAnimation();

    // 화면 갱신 여부 피드백 텍스트
    void showUpdating();

    // 터치 입력 확인 (터치 시 true 반환하여 메인루프에서 새로고침 수행)
    bool checkTouch(); 

    // 터치된 지역 인덱스 반환 (-1이면 버튼 아님, 0~4 지역 탭, 5 새로고침, 10~12 예보 탭)
    int getTouchAction();

    // --- 새로고침 버튼 영역 정의 (우측 상단) ---
    const int REFRESH_BTN_X = 280;
    const int REFRESH_BTN_Y = 5;
    const int REFRESH_BTN_W = 35;
    const int REFRESH_BTN_H = 35;

    // --- 하단 메인 탭 (지역 선택) ---
    const int TAB_H = 40;
    const int TAB_Y = 240 - 40;
    const int TAB_W = 320 / 5;

    // --- 상단 서브 탭 (지금/내일/모레 선택) ---
    const int SUB_TAB_Y = 240 - 75; // 메인 탭 바로 위
    const int SUB_TAB_H = 30;
    const int SUB_TAB_W = 50; // 서브 탭 너비 소폭 축소
    const int SUB_TAB_X_START = 165; // 우측 영역(160~) 내부에 배치

private:
    TFT_eSPI* _tft;
    CYD28_TouchR* _touch;
    WeatherData _currentWeather;
    ForecastData _fullForecast;
    int _selectedDay; // 0: Now, 1: Tomorrow, 2: Day After
    int _currentRegionIdx;
    String _currentRegionName;
    int _month, _day, _hour, _minute, _second;
    
    void renderTime();
    void renderWeatherText();
    void renderWeatherIcon();
    
    void drawSun(int x, int y, int radius, int frame);
    void drawCloud(int x, int y, int w, int h, int fill);
    void drawRainDrops(int x, int y, int frame);
    void drawSnowFlakes(int x, int y, int frame);
    void drawRefreshButton(); // 새로고침 버튼 그리기
    void drawTabs();          // 하단 메인 탭 그리기
    void drawSubTabs();       // 예보 선택 서브 탭 그리기
    void updateInternalState(const WeatherData& data); // 내부 상태 업데이트 및 렌더링 호출

    WeatherIconType _currentIcon;
    uint32_t _animLastTime;
    int _animFrame;

    // --- 커스텀 폰트 출력기 ---
    void drawCustomText(const char* text, int x, int y, uint16_t color, const uint8_t* bitmap, const void* glyphs, int numGlyphs);
};

#endif
