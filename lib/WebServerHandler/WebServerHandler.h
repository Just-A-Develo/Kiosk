#ifndef WEB_SERVER_HANDLER_H
#define WEB_SERVER_HANDLER_H

#include <Arduino.h>
#include <ESPAsyncWebServer.h>
#include <DNSServer.h>
#include <ESP8266mDNS.h>
#include <LittleFS.h>

class WebServerHandler {
public:
    WebServerHandler(uint16_t port); // Constructor met poort
    void begin();
    void dnsSetup(String wifiMode);
    void handleRoot(AsyncWebServerRequest *request);
    void handleSave(AsyncWebServerRequest *request, File file);
    void saveCredentials(const char *ssid, const char *password, String ipStr, File file);
    void saveSSIDToFS(const String &ssid, File file);
    void handle();
    char *getSSID();
    char *getPASS();

private:
    AsyncWebServer server; // Interne server die direct geconfigureerd wordt
};

#endif // WEB_SERVER_HANDLER_H
