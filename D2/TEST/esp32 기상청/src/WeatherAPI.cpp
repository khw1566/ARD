#include "WeatherAPI.h"
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <time.h>

WeatherAPI::WeatherAPI(const char* apiKey, int nx, int ny)
    : _apiKey(apiKey), _nx(nx), _ny(ny) {}

void WeatherAPI::setLocation(int nx, int ny) {
    _nx = nx;
    _ny = ny;
}

bool WeatherAPI::fetchWeather(WeatherData& data) {
    if (WiFi.status() != WL_CONNECTED) {
        return false;
    }

    // 초단기예보 (getUltraSrtFcst)는 매시 45분에 발표, 1시간 주기로 데이터 갱신
    // 현재 시간에서 1시간(3600초)을 빼서 해당 시간대의 30분 발표를 기준으로 삼음
    time_t now = time(NULL);
    if (now < 100000) return false; // NTP가 아직 연동되지 않음

    time_t targetTime = now - 3600;
    struct tm *ptm = localtime(&targetTime);

    char baseDate[9];
    sprintf(baseDate, "%04d%02d%02d", ptm->tm_year + 1900, ptm->tm_mon + 1, ptm->tm_mday);
    char baseTime[5];
    sprintf(baseTime, "%02d30", ptm->tm_hour); // 30분 발표 기준

    String url = "http://apis.data.go.kr/1360000/VilageFcstInfoService_2.0/getUltraSrtFcst";
    url += "?serviceKey="; url += _apiKey;
    url += "&pageNo=1&numOfRows=60&dataType=JSON";
    url += "&base_date="; url += baseDate;
    url += "&base_time="; url += baseTime;
    url += "&nx="; url += String(_nx);
    url += "&ny="; url += String(_ny);

    HTTPClient http;
    http.begin(url);
    Serial.print("[Weather] GET Request: ");
    Serial.println(url);

    int httpCode = http.GET();
    Serial.print("[Weather] HTTP Response Code: ");
    Serial.println(httpCode);

    data.isValid = false;

    if (httpCode > 0) { // HTTP_CODE_OK 뿐만 아니라 에러도 확인
        String payload = http.getString();
        
        if (httpCode != HTTP_CODE_OK) {
            Serial.println("[Weather] ERROR Payload: ");
            Serial.println(payload);
            http.end();
            return false;
        }
        
        // JSON 파싱 (60개 item 이상 처리, 용량 넉넉히 16384바이트 할당)
        DynamicJsonDocument doc(16384); 
        DeserializationError error = deserializeJson(doc, payload);

        if (error) {
            Serial.print("[Weather] JSON Parse Error: ");
            Serial.println(error.c_str());
            http.end();
            return false;
        }

        JsonArray items = doc["response"]["body"]["items"]["item"].as<JsonArray>();
        if(items.isNull()) {
            Serial.println("[Weather] JSON Parse Error: No items found. (Check base_time/date API bounds)");
            Serial.println(payload);
            http.end();
            return false;
        }

        bool t1h_found = false, pty_found = false, sky_found = false;
        bool reh_found = false, ws_found = false, wd_found = false, rn1_found = false;
        String firstFcstTime = items[0]["fcstTime"].as<String>(); // 가장 이른 예보시간 1건만 취합

        for (JsonObject item : items) {
            String category = item["category"].as<String>();
            String fcstTime = item["fcstTime"].as<String>();
            String value = item["fcstValue"].as<String>();

            if (fcstTime != firstFcstTime) continue; // 가장 최근 1시간 뒤 예보만 사용

            if (category == "T1H") {
                data.temperature = value;
                t1h_found = true;
            } else if (category == "PTY") {
                data.rainState = value;
                pty_found = true;
            } else if (category == "SKY") {
                data.skyState = value;
                sky_found = true;
            } else if (category == "REH") {
                data.humidity = value;
                reh_found = true;
            } else if (category == "WSD") {
                data.windSpeed = value;
                ws_found = true;
            } else if (category == "VEC") {
                data.windDir = value;
                wd_found = true;
            } else if (category == "RN1") {
                data.rainAmount = value;
                rn1_found = true;
            }

            if (t1h_found && pty_found && sky_found && reh_found && ws_found && wd_found && rn1_found) {
                data.isValid = true;
                break;
            }
        }
        
        http.end();
        return data.isValid;
    }

    http.end();
    return false;
}

bool WeatherAPI::fetchForecast(ForecastData& forecast) {
    if (WiFi.status() != WL_CONNECTED) return false;

    time_t now = time(NULL);
    struct tm *ptm = localtime(&now);

    // 기상청 서버 지연을 고려하여 1시간 전 시간을 기준으로 가장 가까운 발표 슬롯(02, 05, 08...) 계산
    time_t queryTime = now - 3600; 
    struct tm *ptmQ = localtime(&queryTime);

    int base_h = ((ptmQ->tm_hour + 22) % 24 / 3) * 3 + 2; 
    if (ptmQ->tm_hour < 2) { 
        base_h = 23;
        queryTime -= 86400; // 전날 데이터
        ptmQ = localtime(&queryTime);
    }

    char baseDate[9], baseTime[5], tomorrowDate[9], dayAfterDate[9];
    sprintf(baseDate, "%04d%02d%02d", ptmQ->tm_year + 1900, ptmQ->tm_mon + 1, ptmQ->tm_mday);
    sprintf(baseTime, "%02d00", base_h);

    time_t t_tomorrow = now + 86400;
    struct tm *pTom = localtime(&t_tomorrow);
    sprintf(tomorrowDate, "%04d%02d%02d", pTom->tm_year + 1900, pTom->tm_mon + 1, pTom->tm_mday);

    time_t t_dayAfter = now + 172800;
    struct tm *pDA = localtime(&t_dayAfter);
    sprintf(dayAfterDate, "%04d%02d%02d", pDA->tm_year + 1900, pDA->tm_mon + 1, pDA->tm_mday);

    String url = "http://apis.data.go.kr/1360000/VilageFcstInfoService_2.0/getVilageFcst";
    url += "?serviceKey="; url += _apiKey;
    url += "&pageNo=1&numOfRows=500&dataType=JSON"; // 500개로 줄여서 수신 안정성 확보
    url += "&base_date="; url += baseDate;
    url += "&base_time="; url += baseTime;
    url += "&nx="; url += String(_nx);
    url += "&ny="; url += String(_ny);

    HTTPClient http;
    http.setTimeout(15000); // 15초 타임아웃 (대용량 데이터 대비)
    http.begin(url);
    Serial.print("[Forecast] GET Request (15s timeout): ");
    Serial.println(url);

    int httpCode = http.GET();
    if (httpCode != HTTP_CODE_OK) {
        http.end();
        return false;
    }

    DynamicJsonDocument doc(49152); // 버퍼 증량 (48KB)
    DeserializationError error = deserializeJson(doc, http.getStream());
    if (error) {
        http.end();
        return false;
    }

    JsonArray items = doc["response"]["body"]["items"]["item"].as<JsonArray>();
    if (items.isNull()) {
        http.end();
        return false;
    }

    auto parseItem = [&](WeatherData& target, const char* targetDate, const char* targetTime) {
        bool foundAny = false;
        for (JsonObject item : items) {
            String fcstDate = item["fcstDate"].as<String>();
            String fcstTime = item["fcstTime"].as<String>();
            if (fcstDate == targetDate && fcstTime == targetTime) {
                String cat = item["category"].as<String>();
                String val = item["fcstValue"].as<String>();
                if (cat == "TMP") target.temperature = val;
                else if (cat == "SKY") target.skyState = val;
                else if (cat == "PTY") target.rainState = val;
                else if (cat == "REH") target.humidity = val;
                else if (cat == "WSD") target.windSpeed = val;
                else if (cat == "RN1") target.rainAmount = val;
                foundAny = true;
            }
        }
        target.isValid = foundAny;
        target.fcstDate = targetDate;
        target.fcstTime = targetTime;
    };

    // 내일/모레 오후 12시 기상 기준
    parseItem(forecast.tomorrow, tomorrowDate, "1200");
    parseItem(forecast.dayAfter, dayAfterDate, "1200");

    http.end();
    return forecast.tomorrow.isValid || forecast.dayAfter.isValid;
}
