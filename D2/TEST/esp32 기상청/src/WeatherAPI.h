#ifndef WEATHERAPI_H
#define WEATHERAPI_H

#include <Arduino.h>

struct WeatherData {
    String temperature;
    String skyState;  // 1: 맑음, 3: 구름많음, 4: 흐림
    String rainState; // 0: 없음, 1: 비, 2: 비/눈, 3: 눈, 5: 빗방울, 6: 빗방웅눈날림, 7: 눈날림
    String humidity;  // REH: 습도 %
    String windSpeed; // WSD: 풍속 m/s
    String windDir;   // VEC: 풍향
    String rainAmount;// RN1: 1시간 강수량 mm
    String fcstDate;  // 예보 날짜 (YYYYMMDD)
    String fcstTime;  // 예보 시간 (HHMM)
    bool isValid;
};

// 3일치 데이터를 담을 구조체
struct ForecastData {
    WeatherData now;      // 초단기 (현재)
    WeatherData tomorrow; // 내일 (특정 시간대)
    WeatherData dayAfter; // 모레 (특정 시간대)
};

class WeatherAPI {
public:
    WeatherAPI(const char* apiKey, int nx, int ny);
    bool fetchWeather(WeatherData& data);  // 초단기예보 (현재)
    bool fetchForecast(ForecastData& forecast); // 단기예보 (내일/모레)
    void setLocation(int nx, int ny);

private:
    const char* _apiKey;
    int _nx;
    int _ny;
};

#endif
