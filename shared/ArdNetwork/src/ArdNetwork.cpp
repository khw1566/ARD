#include "ArdNetwork.h"
#include "portal_page.h"
#include <esp_system.h>

bool ArdNetwork::begin(const char *role, const char *legacyNamespace, const char *defaultServer) {
    if (task_) return true;
    settings_.version = 1;
    strlcpy(settings_.server, defaultServer, sizeof(settings_.server));
    Preferences prefs;
    bool loaded = false;
    if (prefs.begin("ard-network", true)) {
        ard_network_settings_t saved = {};
        loaded = prefs.getBytesLength("settings") == sizeof(saved) &&
                 prefs.getBytes("settings", &saved, sizeof(saved)) == sizeof(saved) && ard_network_valid(&saved);
        if (loaded) settings_ = saved;
        prefs.end();
    }
    // Import existing credentials once. No hard-coded SSID/password fallback.
    if (!loaded && legacyNamespace && prefs.begin(legacyNamespace, true)) {
        String ssid = prefs.getString("ssid"), password = prefs.getString("password");
        String server = prefs.getString("server_ip", defaultServer);
        if (ssid.length() <= 32 && password.length() <= 64 && server.length() <= 63) {
            ard_network_settings_t legacy = {1, {}, {}, {}};
            strlcpy(legacy.ssid, ssid.c_str(), sizeof(legacy.ssid));
            strlcpy(legacy.password, password.c_str(), sizeof(legacy.password));
            strlcpy(legacy.server, server.c_str(), sizeof(legacy.server));
            if (ard_network_valid(&legacy)) settings_ = legacy;
        }
        prefs.end();
    }
    uint64_t mac = ESP.getEfuseMac();
    char suffix[7];
    snprintf(suffix, sizeof(suffix), "%02x%02x%02x", (unsigned)((mac >> 24) & 255),
             (unsigned)((mac >> 32) & 255), (unsigned)((mac >> 40) & 255));
    hostname_ = String("ard-") + role + "-" + suffix;
    apName_ = hostname_;
    apPassword_ = String("ARD-") + suffix + "!";
    char token[33];
    snprintf(token, sizeof(token), "%08lx%08lx%08lx%08lx", (unsigned long)esp_random(),
             (unsigned long)esp_random(), (unsigned long)esp_random(), (unsigned long)esp_random());
    token_ = token;
    resolved_ = settings_.server;
    addressMutex_ = xSemaphoreCreateMutex();
    if (!addressMutex_) return false;
    // Hostname must be set before starting either Wi-Fi interface.
    WiFi.persistent(false);
    WiFi.setHostname(hostname_.c_str());
    if (!WiFi.mode(WIFI_AP_STA) || !WiFi.softAP(apName_.c_str(), apPassword_.c_str())) {
        Serial.println("[NET] SoftAP initialization failed");
        return false;
    }
    WiFi.setSleep(false);
    dns_.start(53, "*", WiFi.softAPIP());
    mdnsReady_ = MDNS.begin(hostname_.c_str());
    if (mdnsReady_) {
        MDNS.addService("http", "tcp", 80);
        MDNS.addService("ard", "tcp", 80);
        MDNS.addServiceTxt("ard", "tcp", "role", role);
        if (!strcmp(role, "drive") || !strcmp(role, "robot") || !strcmp(role, "control"))
            MDNS.addService("ard-command", "tcp", 5002);
    } else Serial.println("[NET] mDNS startup failed; use AP IP");
    web_.on("/", HTTP_GET, [this]() { page(); });
    web_.on("/network", HTTP_GET, [this]() { page(); });
    web_.on("/network/save", HTTP_POST, [this]() { save(); });
    web_.onNotFound([this]() {
        web_.sendHeader("Location", String("http://") + WiFi.softAPIP().toString() + "/network");
        web_.send(302, "text/plain", "Open network settings");
    });
    web_.begin();
    printSetup();
    if (settings_.ssid[0]) WiFi.begin(settings_.ssid, settings_.password);
    if (xTaskCreate(taskEntry, "ArdNetwork", 6144, this, 1, &task_) != pdPASS) {
        Serial.println("[NET] Network task allocation failed");
        return false;
    }
    return true;
}

void ArdNetwork::printSetup() const {
    Serial.printf("[NET] AP: %s | Password: %s | Login: admin\n", apName_.c_str(), apPassword_.c_str());
    Serial.printf("[NET] Setup: http://%s/network or http://%s.local/network\n",
                  WiFi.softAPIP().toString().c_str(), hostname_.c_str());
}

String ArdNetwork::serverAddress() {
    if (!addressMutex_) return serverHost();
    xSemaphoreTake(addressMutex_, portMAX_DELAY);
    String value = resolved_;
    xSemaphoreGive(addressMutex_);
    return value;
}

String ArdNetwork::escape(const String &value) {
    String out = value;
    out.replace("&", "&amp;"); out.replace("<", "&lt;"); out.replace(">", "&gt;");
    out.replace("\"", "&quot;"); out.replace("'", "&#39;");
    return out;
}

bool ArdNetwork::authorize() {
    if (web_.authenticate("admin", apPassword_.c_str())) return true;
    web_.requestAuthentication();
    return false;
}

void ArdNetwork::page() {
    if (!authorize()) return;
    String html = ARD_PORTAL_PAGE;
    String status = WiFi.status() == WL_CONNECTED ? String("Wi-Fi 연결됨: ") + WiFi.localIP().toString() : "Wi-Fi 미연결 · 설정 AP 사용 가능";
    html.replace("{{HOST}}", hostname_); html.replace("{{STATUS}}", status);
    html.replace("{{TOKEN}}", token_); html.replace("{{SERVER}}", escape(settings_.server));
    html.replace("{{SSID}}", escape(settings_.ssid));
    web_.sendHeader("Cache-Control", "no-store");
    web_.send(200, "text/html; charset=utf-8", html);
}

void ArdNetwork::save() {
    if (!authorize()) return;
    if (web_.arg("token") != token_) { web_.send(403, "text/plain", "Invalid form token"); return; }
    String ssid = web_.arg("ssid"), password = web_.arg("password"), server = web_.arg("server");
    server.trim();
    ard_network_settings_t next = {1, {}, {}, {}};
    if (ssid.length() > 32 || password.length() > 64 || server.length() > 63 ||
        strlen(ssid.c_str()) != ssid.length() || strlen(password.c_str()) != password.length() ||
        strlen(server.c_str()) != server.length()) {
        web_.send(400, "text/plain", "Setting exceeds maximum byte length"); return;
    }
    strlcpy(next.ssid, ssid.c_str(), sizeof(next.ssid));
    strlcpy(next.password, password.c_str(), sizeof(next.password));
    strlcpy(next.server, server.c_str(), sizeof(next.server));
    if (!ard_network_valid(&next)) { web_.send(400, "text/plain", "Invalid SSID/password/server host"); return; }
    Preferences prefs;
    if (!prefs.begin("ard-network", false)) { web_.send(500, "text/plain", "NVS open failed"); return; }
    bool ok = prefs.putBytes("settings", &next, sizeof(next)) == sizeof(next);
    prefs.end();
    if (!ok) { web_.send(500, "text/plain", "NVS save failed"); return; }
    web_.send(200, "text/html; charset=utf-8", "<meta charset='utf-8'><p>저장했습니다. 보드가 재시작됩니다. 같은 설정 AP 또는 .local 주소로 다시 접속하세요.</p>");
    restartAt_ = millis() + 1500;
}

void ArdNetwork::taskEntry(void *arg) { static_cast<ArdNetwork *>(arg)->run(); }

void ArdNetwork::run() {
    uint32_t lastRetry = millis(), lastResolve = 0;
    bool wasConnected = false;
    for (;;) {
        dns_.processNextRequest();
        web_.handleClient();
        uint32_t now = millis();
        if (restartAt_ && (int32_t)(now - restartAt_) >= 0) ESP.restart();
        bool connected = WiFi.status() == WL_CONNECTED;
        if (connected != wasConnected) {
            Serial.printf("[NET] STA %s | IP %s\n", connected ? "connected" : "disconnected", WiFi.localIP().toString().c_str());
            lastResolve = now - 30000;
            wasConnected = connected;
        }
        if (!connected && settings_.ssid[0] && now - lastRetry >= 15000) {
            lastRetry = now;
            WiFi.reconnect();
        }
        if (connected && now - lastResolve >= 30000) {
            lastResolve = now;
            IPAddress ip;
            String host = settings_.server;
            host.toLowerCase();
            bool found = ip.fromString(host);
            if (!found && host.endsWith(".local") && mdnsReady_) {
                host.remove(host.length() - 6);
                ip = MDNS.queryHost(host.c_str(), 500);
                found = (uint32_t)ip != 0;
            } else if (!found) found = WiFi.hostByName(host.c_str(), ip) == 1;
            if (found) {
                xSemaphoreTake(addressMutex_, portMAX_DELAY);
                resolved_ = ip.toString();
                xSemaphoreGive(addressMutex_);
            } else Serial.println("[NET] PC host lookup failed; retrying in 30s");
        }
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}
