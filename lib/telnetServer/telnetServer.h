#ifndef TELNETSERVER_H
#define TELNETSERVER_H

#include <Arduino.h>
#include <ESP8266WiFi.h>

class SerialTelnet
{
public:
    SerialTelnet();
    void begin(long baud);
    void println(const String &msg);
    void print(const String &msg);
    void printf(const char *format, ...);
    void handleClient();

private:
    WiFiServer server;
    WiFiClient client;
};

#endif // TELNETSERVER_H
