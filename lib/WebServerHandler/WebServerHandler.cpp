#include "WebServerHandler.h"
#include <ESP8266WiFi.h>
#include "config.h" // Bevat functies zoals saveSSIDToFS en saveCredentials

AsyncWebServer server(80);
DNSServer dnsServer;
char ssid[32] = "";
char ip[32] = "";
char password[64] = "";

// Constructor die de interne AsyncWebServer direct initialiseert met de opgegeven poort
WebServerHandler::WebServerHandler(uint16_t port)
    : server(port) {}

void WebServerHandler::begin()
{
    server.on("/", HTTP_GET, std::bind(&WebServerHandler::handleRoot, this, std::placeholders::_1));
    server.on("/", HTTP_GET, [this](AsyncWebServerRequest *request)
              { handleRoot(request); });

    server.on("/save", HTTP_POST, [this](AsyncWebServerRequest *request)
              {
                  File file = LittleFS.open("/wifi_credentials.txt", "w"); // Open een bestand
                  if (!file)
                  {
                      request->send(500, "text/plain", "Failed to open file");
                      return;
                  }
                  handleSave(request, file);
                  file.close(); // Vergeet niet het bestand te sluiten
              });

    server.begin();

    server.serveStatic("/assets/icon.png", LittleFS, "/assets/icon.png");
    server.begin();
}

void WebServerHandler::saveCredentials(const char *ssid, const char *password, String ipStr, File file)
{
    if (file)
        file.close();
    file = LittleFS.open("/ExternalSSID.txt", "w");
    if (file)
    {
        file.println(ssid);
        file.println(password);
        file.print(ipStr);
        file.close();
#ifdef DEBUG
        telnet.println("SSID saved to LittleFS.");
#endif
    }
    else
    {
#ifdef DEBUG
        telnet.println("Failed to save SSID.");
#endif
    }
}

void WebServerHandler::handleRoot(AsyncWebServerRequest *request)
{
    String temp;
    temp.reserve(2200);
    temp += F("<html><head>"
            "<title>ESP8266 WiFi Config</title>"
            "<meta name='viewport' content='width=device-width, initial-scale=1.0'>"
            "<style>"
            "body {font-family: Arial, sans-serif;background-color: #f0f0f0;margin: 0;padding: 0;display: flex;flex-direction: column;justify-content: center;align-items: center;height: 100vh;}"
            ".container {width: 90%;max-width: 400px;background: white;padding: 20px;border-radius: 8px;box-shadow: 0 0 10px rgba(0, 0, 0, 0.1);text-align: center;}"
            "header {background-color: #27a8c9;padding: 15px;color: white;border-radius: 8px 8px 8px 8px;font-size: 20px;}"
            "p {font-size: 16px;margin: 10px 0;}"
            "form {display: flex;flex-direction: column;margin-top: 15px;margin-bottom: 0;}"
            "img {width: 100px;max-width: 100px;min-width: 50px;background-color: transparent;margin-bottom: 2.5vh;}"
            ".input-container {position: relative;display: inline-block;margin-top: 20px;}"
            ".input-container input {width: 100%;padding: 10px;font-size: 16px;border: 2px solid #ccc;border-radius: 5px;box-sizing: border-box;}"
            ".input-container.ssid::before {content: \"SSID\";position: absolute;top: -10px;left: 10px;font-size: 14px;color: #555;background-color: #fff;padding: 0 5px;}"
            ".input-container.password::before {content: \"Password\";position: absolute;top: -10px;left: 10px;font-size: 14px;color: #555;background-color: #fff;padding: 0 5px;}"
            ".input-container.ip::before {content: \"IP Address\";position: absolute;top: -10px;left: 10px;font-size: 14px;color: #555;background-color: #fff;padding: 0 5px;}"
            "input[type='submit'] {background-color: #27a8c9;color: white;font-size: 18px;padding: 12px;border: none;border-radius: 4px;cursor: pointer;margin-top: 20px;}"
            "input[type='submit']:hover {background-color: #45a049;}"
            "@media (max-width: 480px) {.container {  width: 95%;  padding: 15px;}"
            "header {  font-size: 18px;}"
            "input {  font-size: 14px;  padding: 8px;}"
            "input[type='submit'] {  font-size: 16px;  padding: 10px;}}"
            "</style></head>"
            "<body>"
            "<img src=\"assets/icon.png\" alt=\"\">"
            "<div class='container'>"
            "<header>WiFi Config</header>"
            "<p>Current IP Address: ");

  temp += WiFi.localIP().toString();
  temp += F("</p>"
            "<form action='/save' method='POST'>"
            "<div class='input-container ssid'><input type='text' name='ssid' required></div>"
            "<div class='input-container password'><input type='password' name='password' required></div>"
            "<div class='input-container ip'><input type='text' name='ip' placeholder='Ex. 192.168.1.100'></div>"
            "<input type='submit' value='Save & Connect'>"
            "</form></div></body></html>");


    request->send(200, "text/html", temp);
}

void WebServerHandler::saveSSIDToFS(const String &ssid, File file)
{
    if (file)
        file.close();
    file = LittleFS.open("/ssid.txt", "w");
    if (file)
    {
        file.print(ssid); // Write the generated SSID
        file.close();
#ifdef DEBUG
        telnet.println("SSID saved to LittleFS.");
#endif
    }
#ifdef DEBUG
    else
    {
        telnet.println("Failed to save SSID.");
    }
#endif
}

void WebServerHandler::handleSave(AsyncWebServerRequest *request, File file)
{
    if (request->hasParam("ssid", true) && request->hasParam("password", true) && request->hasParam("name", true))
    {
        String newSSID = request->getParam("ssid", true)->value();
        String newPassword = request->getParam("password", true)->value();
        String ipStr = request->hasParam("ip", true) ? request->getParam("ip", true)->value() : "";
        String name = request->hasParam("name", true) ? request->getParam("name", true)->value() : "";
        if (name.length() > 0)
        {
            saveSSIDToFS(name, file);
        }

#ifdef DEBUG
        telnet.println("Nieuwe instellingen ontvangen:");
        telnet.print("SSID: ");
        telnet.println(newSSID);
        telnet.print("Password: ");
        telnet.println(newPassword);
        telnet.print("IP Address: ");
        telnet.println(ipStr);
#endif

        // **Validatie van het IP-adres**
        IPAddress staticIP;
        bool useStaticIP = false;

        if (ipStr.length() > 0 && staticIP.fromString(ipStr))
        {
#ifdef DEBUG
            telnet.println("Geldig IP-adres ontvangen. Toevoegen aan WiFi-instellingen.");
#endif
            useStaticIP = true;
        }
        else
        {
#ifdef DEBUG
            telnet.println("Ongeldig IP-adres! Standaard DHCP wordt gebruikt.");
#endif
        }

        newSSID.toCharArray(ssid, 32);
        newPassword.toCharArray(password, 64);
        saveCredentials(ssid, password, ipStr, file); // Opslaan in EEPROM/LittleFS

        request->send(200, "text/html", "<h3>Saved! Rebooting...</h3>");

        WiFi.disconnect();
        delay(1000);

        if (useStaticIP)
        {
            WiFi.config(staticIP, WiFi.gatewayIP(), WiFi.subnetMask());
        }

        WiFi.begin(newSSID.c_str(), newPassword.c_str());
        dnsServer.stop();
        delay(500);
        MDNS.begin("boxapos");
    }
    else
    {
        request->send(400, "text/html", "Missing SSID or Password");
    }
}

void WebServerHandler::dnsSetup(String wifiMode)
{
    if (wifiMode == "AP")
    {
        dnsServer.start(53, "boxapos.local", WiFi.softAPIP());
    }
    else if (wifiMode == "STA")
    {
        MDNS.begin("boxapos");
    }
}

void WebServerHandler::handle()
{
    dnsServer.processNextRequest();
}

char *WebServerHandler::getSSID()
{
    return ssid;
}
char *WebServerHandler::getPASS()
{
    return password;
}
