#include <Arduino.h>
#include <PicoMQTT.h>
#include "FS.h"
#include <LittleFS.h>
#include <ArduinoOTA.h>
#include <LEDFrameRAM.h>
#include <ESP8266WiFi.h>
#include <WiFiClient.h>
#include <ESP8266WebServer.h>
#include <DNSServer.h>
#include <ESP8266mDNS.h>
#include <StreamString.h>

//#define DEBUG

#ifdef DEBUG
WiFiServer telnetServer(23);
WiFiClient telnetClient;
class SerialTelnet
{
public:
  void begin(long baud)
  {
    Serial.begin(baud);
  }

  void println(const String &msg)
  {
    Serial.println(msg);
    telnetClient.println(msg);
  }

  void print(const String &msg)
  {
    Serial.print(msg);
    telnetClient.print(msg);
  }

  void printf(const char *format, ...)
  {
    char buf[128]; // Buffer voor de output
    va_list args;
    va_start(args, format);
    vsnprintf(buf, sizeof(buf), format, args);
    va_end(args);
    Serial.print(buf);
    telnetClient.printf(buf);
  }
};

// Gebruik dit in plaats van Serial:
SerialTelnet Debug;
#endif

ESP8266WebServer server(80);
char ssid[32] = "";
char ip[32] = "";
char password[64] = "";
String wifimode = "";

LEDFrameRAM LedRam;

DNSServer dnsServer;

void saveCredentials(const char *ssid, const char *password, String ipStr)
{
  if (LittleFS.exists("/ExternalSSID.txt"))
  {
    LittleFS.remove("/ExternalSSID.txt");
  }
  File file = LittleFS.open("/ExternalSSID.txt", "w");
  if (file)
  {
    file.println(ssid);
    file.println(password);
    file.print(ipStr);
    file.close();
#ifdef DEBUG
    Debug.println("SSID saved to LittleFS.");
#endif
  }
  else
  {
#ifdef DEBUG
    Debug.println("Failed to save SSID.");
#endif
  }
}

void handleRoot()
{
  StreamString temp;
  temp.reserve(2200); // Meer ruimte voor extra input veld
  temp.printf("\
  <html>\
    <head>\
      <title>ESP8266 WiFi Config</title>\
      <meta name='viewport' content='width=device-width, initial-scale=1.0'>\
      <style>\
        body { font-family: Arial, sans-serif; background-color: #f0f0f0; margin: 0; padding: 0; display: flex; justify-content: center; align-items: center; height: 100vh; }\
        .container { width: 90%%; max-width: 400px; background: white; padding: 20px; border-radius: 8px; box-shadow: 0 0 10px rgba(0,0,0,0.1); text-align: center; }\
        header { background-color: #4CAF50; padding: 15px; color: white; border-radius: 8px 8px 0 0; font-size: 20px; }\
        p { font-size: 16px; margin: 10px 0; }\
        form { display: flex; flex-direction: column; margin-top: 15px; }\
        label { font-size: 14px; text-align: left; margin: 5px 0 2px; font-weight: bold; }\
        input[type='text'], input[type='password'] {\
          padding: 10px; font-size: 16px; width: 100%%;\
          border: 1px solid #ccc; border-radius: 4px; box-sizing: border-box;\
        }\
        input[type='submit'] {\
          background-color: #4CAF50; color: white; font-size: 18px;\
          padding: 12px; border: none; border-radius: 4px; cursor: pointer;\
          margin-top: 10px;\
        }\
        input[type='submit']:hover { background-color: #45a049; }\
        @media (max-width: 480px) {\
          .container { width: 95%%; padding: 15px; }\
          header { font-size: 18px; }\
          input { font-size: 14px; padding: 8px; }\
          input[type='submit'] { font-size: 16px; padding: 10px; }\
        }\
      </style>\
    </head>\
    <body>\
      <div class='container'>\
        <header>WiFi Config</header>\
        <p>ESP IP Address: %s</p>\
        <form action='/save' method='POST'>\
          <label for='ssid'>SSID:</label>\
          <input type='text' name='ssid' required>\
          <label for='password'>Password:</label>\
          <input type='password' name='password' required>\
          <label for='ip'>IP Address:</label>\
          <input type='text' name='ip' placeholder='192.168.1.100'>\
          <input type='submit' value='Save & Connect'>\
        </form>\
      </div>\
    </body>\
  </html>",
              WiFi.localIP().toString().c_str());

  server.send(200, "text/html", temp.c_str());
}

void handleSave()
{
  if (server.hasArg("ssid") && server.hasArg("password"))
  {
    String newSSID = server.arg("ssid");
    String newPassword = server.arg("password");
    String ipStr = server.arg("ip"); // Ophalen van het ingevoerde IP-adres
#ifdef DEBUG
    Debug.println("Nieuwe instellingen ontvangen:");
    Debug.print("SSID: ");
    Debug.println(newSSID);
    Debug.print("Password: ");
    Debug.println(newPassword);
    Debug.print("IP Address: ");
    Debug.println(ipStr);
#endif

    // **Validatie van het IP-adres**
    IPAddress staticIP;
#ifdef DEBUG
    if (ipStr.length() > 0 && staticIP.fromString(ipStr))
    {
      Debug.println("Geldig IP-adres ontvangen. Toevoegen aan WiFi-instellingen.");
    }
    else
    {
      Debug.println("Ongeldig IP-adres! Standaard DHCP wordt gebruikt.");
      Debug.print("DHCP IP:");
      Debug.println(WiFi.localIP().toString());
    }
#endif
    newSSID.toCharArray(ssid, 32);
    newPassword.toCharArray(password, 64);
    saveCredentials(ssid, password, ipStr);
    server.send(200, "text/html", "<h3>Saved! Rebooting...</h3>");
    WiFi.disconnect();
    delay(1000);
    if (staticIP)
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
    server.send(400, "text/html", "Missing SSID or Password");
  }
}

String loadExtSSID()
{
  if (!LittleFS.exists("/ExternalSSID.txt"))
  {
    return "";
  }
  File file = LittleFS.open("/ExternalSSID.txt", "r");
  if (!file)
  {
    return "";
  }
  String ssid = file.readStringUntil('\n');
  file.close();
  ssid.trim();
  return ssid;
}

String loadExtPASS()
{
  if (!LittleFS.exists("/ExternalSSID.txt"))
  {
    return "";
  }
  File file = LittleFS.open("/ExternalSSID.txt", "r");
  if (!file)
  {
    return "";
  }
  file.readStringUntil('\n'); // Skip SSID line
  String password = file.readStringUntil('\n');
  file.close();
  password.trim();
  return password;
}

String loadExtIP()
{
  if (!LittleFS.exists("/ExternalSSID.txt"))
  {
    return "";
  }
  File file = LittleFS.open("/ExternalSSID.txt", "r");
  if (!file)
  {
    return "";
  }
  file.readStringUntil('\n'); // Skip SSID line
  file.readStringUntil('\n'); // Skip SSID line
  String ip = file.readString();
  file.close();
  ip.trim();
  return ip;
}

#include <Adafruit_NeoPixel.h>

// #define DEBUG // comment out to dissable Serial prints

#define LED_PIN 4

Adafruit_NeoPixel strip = Adafruit_NeoPixel(NUM_LEDS, LED_PIN, NEO_BRG + NEO_KHZ400);

#include <iostream>
#include <sstream>
#include <vector>

struct LEDFrameData
{
  uint8_t frameIndex;
  uint8_t ledIndex;
  uint8_t r;
  uint8_t g;
  uint8_t b;
  uint8_t intensity;
  uint16_t delayMs;
};

PicoMQTT::Server mqtt;

// Buffer to save frames
LEDFrameData displayBuffer[NUM_LEDS];
std::vector<LEDFrameData> frameBuffer;
const size_t MAX_FRAMES_IN_BUFFER = 30; // Amount of frames that will be saved in memory before writing to LittleFS

bool isFirst = true;
unsigned int waitTime = 0;
int arrayCounter = 0;
int r, g, b = 0;
int level = 255;

// Function to write a batch of frames to LittleFS
void saveFrameBatchToLittleFS(const std::vector<LEDFrameData> &buffer)
{
  if (buffer.empty())
  {
#ifdef DEBUG
    Debug.println("No data to save.");
#endif
    return;
  }

  // use the frameIndex from first frame in buffer as filename (ex. frame15.txt)
  String filename = "/frame_" + String(buffer[0].frameIndex) + ".dat";
  if (LittleFS.exists(filename))
  {
    LittleFS.remove(filename);
  }

  File file = LittleFS.open(filename, "a");

  if (file)
  {
    // Make a buffer of the data size to upload in 1 go
    size_t bufferSize = buffer.size() * sizeof(LEDFrameData); // Calculate buffer size
    uint8_t *dataBuffer = new uint8_t[bufferSize];            // Dynamic memory for data

    // Fill with all frames
    size_t offset = 0;
    for (const auto &frameData : buffer)
    {
      memcpy(dataBuffer + offset, &frameData, sizeof(LEDFrameData)); // Copy frame to buffer
      offset += sizeof(LEDFrameData);                                // increase offset
    }

    // Write the whole buffer in one go to LittleFS
    file.write(dataBuffer, bufferSize);
    file.close();
    Dir dir = LittleFS.openDir("/");

    // Remove buffer once completed
    delete[] dataBuffer;
  }
  else
  {
#ifdef DEBUG
    Debug.println("Failed to open file for writing");
#endif
  }
}

int readFrameBatchFromLittleFS(int receivedFrame)
{
  const char *filename = "/animation.dat";
  File file = LittleFS.open(filename, "r");

  if (!file)
  {
#ifdef DEBUG
    Debug.println("❌ Failed to open file!");
#endif
    return 0;
  }

  int offset = receivedFrame * led_count * 8; // 8 bytes per LED
  file.seek(offset, SeekSet);
  int duration = 0;
  int frameIndex = 0;
  // Lees het frame in de buffer
  for (int i = 0; i < NUM_LEDS; i++)
  {
    uint8_t buffer[8];
    if (file.read(buffer, 8) != 8)
    {
      #ifdef DEBUG
      Debug.println("❌ Fout bij lezen van frame!");
      #endif
      return 0;
    }
// Pak de waarden uit
#ifdef DEBUG
    Debug.printf("Led %d = %d, %d, %d\n\r", i, ((buffer[2] * buffer[5]) >> 8), ((buffer[3] * buffer[5]) >> 8), ((buffer[4] * buffer[5]) >> 8));
#endif
    strip.setPixelColor(i, strip.Color(((buffer[2] * buffer[5]) >> 8), ((buffer[3] * buffer[5]) >> 8), ((buffer[4] * buffer[5]) >> 8)));
    duration = buffer[6] | (buffer[7] << 8); // Little-endian
    frameIndex = buffer[0];
  }

  file.close();
#ifdef DEBUG
  Debug.printf("DisplayedFrame: %d\r\n", frameIndex);
#endif
  strip.show();

  return duration;
}

// Function to empty buffer before writing
void flushFrameBufferToStorage()
{
  if (frameBuffer.size() > 0)
  {
    saveFrameBatchToLittleFS(frameBuffer);
    frameBuffer.clear(); // Empty again after saving
  }
}

void setColor(int r, int g, int b, int intens)
{
  uint32_t color = strip.Color(r, g, b);
  strip.setBrightness(intens);
  strip.fill(color, 0, NUM_LEDS);
  strip.show();
}

int mqttProgram = 0;
int arraySize = 0;
int *orderArray;

String loadSSIDFromFS()
{
  if (!LittleFS.exists("/ssid.txt"))
  {
    return "";
  }
  File file = LittleFS.open("/ssid.txt", "r");
  if (!file)
  {
    return "";
  }
  String ssid = file.readString();
  file.close();
  ssid.trim();
  return ssid;
}

void saveSSIDToFS(const String &ssid)
{
  File file = LittleFS.open("/ssid.txt", "w");
  if (file)
  {
    file.print(ssid); // Write the generated SSID
    file.close();
#ifdef DEBUG
    Debug.println("SSID saved to LittleFS.");
#endif
  }
#ifdef DEBUG
  else
  {
    Debug.println("Failed to save SSID.");
  }
#endif
}

void apSetup()
{
  wifimode = "AP";
  String apSSID = loadSSIDFromFS(); // Try to load saved SSID

  if (apSSID == "") // NO saved SSID? Generate one
  {
#ifdef DEBUG
    Debug.println("No SSID found, generating a new one.");
#endif
    srand(analogRead(A0));
    int randomnum = rand();
    apSSID = "KioskLed" + String(randomnum);
    saveSSIDToFS(apSSID); // Save for next boot
  }
#ifdef DEBUG
  else
  {
    Debug.println("SSID loaded from FS: " + apSSID);
  }
#endif
  String apPassword = "KioskLed";
  WiFi.softAP(apSSID.c_str(), apPassword);
#ifdef DEBUG
  Debug.println("Access Point Started");
  Debug.println(apSSID);
  Debug.print("AP IP Address: ");
  Debug.println(WiFi.softAPIP().toString());
#endif
}

void setup()
{
  // Start LittleFS
  if (!LittleFS.begin())
  {
#ifdef DEBUG
    Debug.println("LittleFS mount failed");
#endif
    return;
  }

// Usual setup
#ifdef DEBUG
  Debug.begin(115200);
  Debug.print("Connecting");
#endif
  // AP
  // Check if a local wifi credential is saved
  String storedSSID = loadExtSSID();
  String storedPASS = loadExtPASS();
  String storedIP = loadExtIP();
  storedSSID.toCharArray(ssid, 32);
  storedPASS.toCharArray(password, 64);
  IPAddress staticIP;

  if (strlen(ssid) > 0)
  {
    wifimode = "STA";
    WiFi.setOutputPower(16.0);
    WiFi.mode(WIFI_STA);

    // **Geldig IP-adres controleren en toepassen**
    if (storedIP.length() > 0 && staticIP.fromString(storedIP))
    {
#ifdef DEBUG
      Debug.print("Statisch IP ingesteld op: ");
      Debug.println(staticIP.toString());
#endif
      WiFi.config(staticIP, WiFi.gatewayIP(), WiFi.subnetMask());
    }
#ifdef DEBUG
    else
    {
      Debug.println("Geen geldig statisch IP, DHCP wordt gebruikt.");
    }
#endif

    WiFi.begin(ssid, password);
#ifdef DEBUG
    Debug.print("Connecting to WiFi");
#endif
    int retries = 0;
    while (WiFi.status() != WL_CONNECTED && retries < 15)
    {
      delay(500);
#ifdef DEBUG
      Debug.print(".");
#endif
      retries++;
    }
#ifdef DEBUG
    Debug.println("");
#endif
    if (WiFi.status() != WL_CONNECTED)
    {
      apSetup();
    }
#ifdef DEBUG
    else
    {
      Debug.println("Connected to WiFi!");
      Debug.print("IP Address: ");
      Debug.println(WiFi.localIP().toString());
    }
#endif
  }
  else
  {
    apSetup();
  }
  ArduinoOTA.begin();

  // Subscribe to a topic pattern and attach a callback
  mqtt.subscribe("update/#", [](const char *topic, PicoMQTT::IncomingPacket &packet)
                 {
#ifdef DEBUG
    Debug.printf("Received message in topic '%s'\r\n", topic);
#endif
    std::map<uint8_t, std::vector<LEDFrameData>> frameGroups;
    String filename = "/animation.dat";
    if (LittleFS.exists(filename))
    {
#ifdef DEBUG
      Debug.println("Removed animation.dat");
#endif
      LittleFS.remove(filename);
    }
    File file = LittleFS.open(filename, "a");
    if (!file) {
#ifdef DEBUG
      Debug.println("Error opening file for writing!");
#endif
      return;
    }
    while (packet.get_remaining_size() >= 8) {
        uint8_t buffer[8];
        packet.read(buffer, 8);
#ifdef DEBUG
        Debug.print("Buffer inhoud: ");
        for (int i = 0; i < 8; i++) {
          Debug.print(String(buffer[i]));
          Debug.print(" ");
        }
        Debug.println("\r\n");
#endif
        file.write(buffer, 8);
      }
      file.close(); });

  mqtt.subscribe("display/#", [](const char *topic, const char *payload)
                 {
#ifdef DEBUG       
    Debug.printf("Received message in topic '%s'\r\n", topic);
#endif
    if (!strcmp(topic, "display/color"))
    {
      mqttProgram = 3;
      String color = payload;
      int comma1 = color.indexOf(',');
      int comma2 = color.lastIndexOf(',');
      r = color.substring(0, comma1).toInt();
      g = color.substring(comma1 + 1, comma2).toInt();
      b = color.substring(comma2 + 1).toInt();
      #ifdef DEBUG
      Debug.printf("Received: r = %d, g = %d, b = %d, level = %d\r\n", r,g,b,level);
      #endif
    }
    if (!strcmp(topic, "display/colorBrightness"))
    {
      mqttProgram = 3;
      String brightness = payload;
      level = brightness.toInt();
      setColor(r, g, b, level);
    }
    if (!strcmp(topic, "display/displayEeprom"))
    {
      isFirst = true;
      mqttProgram = 2;
      delete[] orderArray;
      return;
    }
    if (!strcmp(topic, "display/order"))
    {
      mqttProgram = 1;
      arrayCounter = 0;

      String order(payload);  // Set payload to String
#ifdef DEBUG
      Debug.println("Received order: " + order);  // Print de ontvangen string
#endif
      // Maak een dynamische array
      delete[] orderArray;
      orderArray = new int[order.length()];
      arraySize = 0;

      int start = 0;
      for (int i = 0; i < (int)order.length(); i++) {
          char c = order[i];
          // Check if the character is a space, tab or comma
          if (c == ' ' || c == '\t' || c == ',') {
              if (start < i) {  // Ensure no empty values
                  String number = order.substring(start, i);
                  orderArray[arraySize++] = number.toInt();
              }
              start = i + 1;  // Go to next
          }
      }

      // Add last number
      if (start < (int)order.length()) {
          String number = order.substring(start);
          orderArray[arraySize++] = number.toInt();
      }

#ifdef DEBUG
      for (int i = 0; i < arraySize; i++) {
          Debug.print("Order " + String(i) + ": " + String(orderArray[i]) + "\n");
      }
#endif
      return;
    }
    if (!strcmp(topic, "display/reset"))
    {
      mqttProgram = 9;
      Dir dir = LittleFS.openDir("/");
      while (dir.next())
      {        
        LittleFS.remove(dir.fileName());
      }
      WiFi.disconnect();
      delay(1000);
      ESP.restart();
    } 
    if (!strcmp(topic, "display/resetFrames"))
    {
      mqttProgram = 9;
      Dir dir = LittleFS.openDir("/");
      while (dir.next()) {
        String filename = dir.fileName();
        if (filename != "ExternalSSID.txt" && filename != "defaultAnimation.dat" && filename != "ssid.txt") {
          LittleFS.remove(filename);
        }
      }
    mqttProgram = 0;
    }
    if (!strcmp(topic, "display/default"))
    {
      mqttProgram = 0;
    }
    if (!strcmp(topic, "display/getIP"))
    {
      IPAddress ip = WiFi.localIP();
      String ipString = ip.toString();
      mqtt.publish("display/returnIP", ipString.c_str());
    } });

  // Start the broker
  mqtt.begin();
  strip.begin();
  strip.show(); // Initialize the strip to off

  LedRam.init();

  if (wifimode == "AP")
  {
    dnsServer.start(53, "boxapos.local", WiFi.softAPIP());
  }
  else if (wifimode == "STA")
  {
    MDNS.begin("boxapos");
  }

  server.on("/", handleRoot);
  server.on("/save", HTTP_POST, handleSave);
  server.begin();
  #ifdef DEBUG
  Dir dir = LittleFS.openDir("/");
  while (dir.next()) // Loop through all files
  {
    Debug.println(dir.fileName());
  }

  FSInfo fs_info;
  LittleFS.info(fs_info);
  Debug.printf("Total: %u bytes\nUsed: %u bytes\nFree space: %u\n\r", fs_info.totalBytes, fs_info.usedBytes, (fs_info.totalBytes - fs_info.usedBytes));
  telnetServer.begin();
  #endif
}
int loopCount = 0;
unsigned int lastDisplayed = millis();
int fileCount = 0;
void loop()
{
  #ifdef DEBUG
  if (telnetServer.hasClient())
  {
    if (telnetClient)
      telnetClient.stop();
    telnetClient = telnetServer.accept();
  }

  if (telnetClient && telnetClient.connected())
  {
    while (telnetClient.available())
    {
      Serial.write(telnetClient.read());
    }
  }
  #endif
  mqtt.loop();
  ArduinoOTA.handle();
  server.handleClient();
  dnsServer.processNextRequest();

  if (!frameBuffer.empty())
  {
    flushFrameBufferToStorage();
  }
  if (mqttProgram == 0)
  {
    LedRam.showDefaultSetup(strip);
  }

  if (mqttProgram == 1)
  {
    if (millis() - lastDisplayed >= waitTime)
    {
      if (arrayCounter >= arraySize)
      {
        arrayCounter = 0;
      }
      waitTime = readFrameBatchFromLittleFS(orderArray[arrayCounter]);
#ifdef DEBUG
      Debug.println(String(waitTime));
      Debug.printf("arrayCounter: %d frameNumber: %d\r\n", arrayCounter, orderArray[arrayCounter]);
#endif
      arrayCounter++;
      lastDisplayed = millis();
    }
  }

  if (mqttProgram == 2)
  {
    if (isFirst)
    {
      fileCount = 0;
      Dir dir = LittleFS.openDir("/");
      while (dir.next()) // Loop through all files
      {
        fileCount++;
      }
      isFirst = false; // Ensures it's only done the first time
#ifdef DEBUG
      Debug.println(String(fileCount));
#endif
    }

    if (millis() - lastDisplayed >= waitTime)
    {
      if (loopCount >= fileCount)
      {
        loopCount = 0;
      }
      waitTime = readFrameBatchFromLittleFS(loopCount);
#ifdef DEBUG
      Debug.println(String(waitTime));
#endif
      loopCount++;
      lastDisplayed = millis();
    }

    if (mqttProgram == 3)
    {
    }
  }
}
