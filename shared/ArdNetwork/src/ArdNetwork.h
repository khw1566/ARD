#pragma once
#include <Arduino.h>
#include <DNSServer.h>
#include <ESPmDNS.h>
#include <Preferences.h>
#include <WebServer.h>
#include <WiFi.h>
#include "network_settings.h"

class ArdNetwork {
public:
    bool begin(const char *role, const char *legacyNamespace, const char *defaultServer);
    String serverHost() const { return String(settings_.server); }
    String serverAddress();
    String httpUrl() { return String("http://") + serverAddress() + ":5000"; }
    void printSetup() const;
private:
    static void taskEntry(void *arg);
    void run();
    void page();
    void save();
    bool authorize();
    static String escape(const String &value);
    ard_network_settings_t settings_ = {};
    WebServer web_{80};
    DNSServer dns_;
    String hostname_, apName_, apPassword_, token_;
    String resolved_;
    SemaphoreHandle_t addressMutex_ = nullptr;
    TaskHandle_t task_ = nullptr;
    uint32_t restartAt_ = 0;
    bool mdnsReady_ = false;
};
