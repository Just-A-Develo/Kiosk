#include <Arduino.h>
#include <PicoMQTT.h>
#include <LittleFS.h>
#include <ArduinoOTA.h>
#include <ESPAsyncWebServer.h>
#include <DNSServer.h>
#include <ESP8266mDNS.h>
#include <Adafruit_NeoPixel.h>

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

AsyncWebServer server(80);
char ssid[32] = "";
char ip[32] = "";
char password[64] = "";
String wifimode = "";
String fileName = "";
String animationName = "";
bool afterFill = false;
int loopAmount = 0;
int loopCounter = 0;

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

void handleRoot(AsyncWebServerRequest *request)
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

void handleSave(AsyncWebServerRequest *request)
{
  if (request->hasParam("ssid", true) && request->hasParam("password", true))
  {
    String newSSID = request->getParam("ssid", true)->value();
    String newPassword = request->getParam("password", true)->value();
    String ipStr = request->hasParam("ip", true) ? request->getParam("ip", true)->value() : "";

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
    bool useStaticIP = false;

    if (ipStr.length() > 0 && staticIP.fromString(ipStr))
    {
#ifdef DEBUG
      Debug.println("Geldig IP-adres ontvangen. Toevoegen aan WiFi-instellingen.");
#endif
      useStaticIP = true;
    }
    else
    {
#ifdef DEBUG
      Debug.println("Ongeldig IP-adres! Standaard DHCP wordt gebruikt.");
#endif
    }

    newSSID.toCharArray(ssid, 32);
    newPassword.toCharArray(password, 64);
    saveCredentials(ssid, password, ipStr); // Opslaan in EEPROM/LittleFS

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

// #define DEBUG // comment out to dissable Serial prints

#define LED_PIN 4
#define NUM_LEDS 70

Adafruit_NeoPixel strip = Adafruit_NeoPixel(NUM_LEDS, LED_PIN, NEO_BRG + NEO_KHZ400);

struct LEDFrameData
{
  uint8_t r;
  uint8_t g;
  uint8_t b;
  uint8_t intensity;
  uint16_t delayMs;
};

PicoMQTT::Server mqtt;

bool isFirst = true;
unsigned int waitTime = 0;
int arrayCounter = 0;
int r, g, b = 0;
int level = 255;

// Function to write a batch of frames to LittleFS
int readFrameBatchFromLittleFS(int receivedFrame, String name)
{
  File file = LittleFS.open(name.c_str(), "r");
  if (!file)
  {
#ifdef DEBUG
    // Debug.println("❌ Failed to open file!");
#endif
    return 0;
  }

  int offset = receivedFrame * NUM_LEDS * sizeof(LEDFrameData); // 8 bytes per LED
  file.seek(offset, SeekSet);
  int duration = 0;
  // Lees het frame in de buffer
  for (int i = 0; i < NUM_LEDS; i++)
  {
    uint8_t buffer[sizeof(LEDFrameData)];
    if (file.read(buffer, sizeof(LEDFrameData)) != sizeof(LEDFrameData))
    {
#ifdef DEBUG
      Debug.println("❌ Fout bij lezen van frame!");
#endif
      return 0;
    }
// Pak de waarden uit
#ifdef DEBUG
    Debug.printf("Led %d = %d, %d, %d\n\r", i, ((buffer[0] * buffer[3]) >> 8), ((buffer[1] * buffer[3]) >> 8), ((buffer[2] * buffer[3]) >> 8));
#endif
    strip.setPixelColor(i, strip.Color(((buffer[0] * buffer[3]) >> 8), ((buffer[1] * buffer[3]) >> 8), ((buffer[2] * buffer[3]) >> 8)));
    duration = buffer[4] | (buffer[5] << 8); // Little-endian
  }

  file.close();
  strip.show();

  return duration;
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

                   // **Stap 1: Lees de eerste regel (bestandsnaam, afterFill and loopCount)**
                   String firstLine = "";
                   char c;

                   while (packet.get_remaining_size() > 0 && (c = packet.read()) != '\n' && c != '\r')
                   {
                     firstLine += c;
                   }
                   firstLine.trim(); // Verwijder overbodige spaties
                   int splitIndex1 = firstLine.indexOf('\t'); // Verwacht "filename<TAB>afterFill<TAB>loopCount"
                   int splitIndex2 = firstLine.indexOf('\t', splitIndex1 + 1);
                   if (splitIndex1 == -1)
                   {
#ifdef DEBUG
                     Debug.println("❌ Invalid first line format!");
#endif
                     return;
                   }

                   String fileName = "/" + firstLine.substring(0, splitIndex1) + ".dat";      // Bestandsnaam
                   afterFill = firstLine.substring(splitIndex1 + 1, splitIndex2).toInt() > 0; // Boolean
                   loopAmount = firstLine.substring(splitIndex2 + 1).toInt();                 // Derde waarde

#ifdef DEBUG
                   Debug.printf("File name: '%s', Afterfill: %d, loop amount: %d\r\n", fileName.c_str(), afterFill, loopAmount);
#endif

                   // **Stap 2: Bestand verwijderen indien nodig**
                   if (LittleFS.exists(fileName))
                   {
#ifdef DEBUG
                     Debug.printf("Removed existing file: %s\r\n", fileName.c_str());
#endif
                     LittleFS.remove(fileName);
                   }

                   // **Stap 3: Open bestand en schrijf binaire data**
                   File file = LittleFS.open(fileName, "a");
                   if (!file)
                   {
#ifdef DEBUG
                     Debug.println("❌ Error opening file for writing!");
#endif
                     return;
                   }

                   while (packet.get_remaining_size() >= 6)
                   {
                     uint8_t buffer[6];
                     packet.read(buffer, 6);

#ifdef DEBUG
                     Debug.print("Buffer inhoud: ");
                     for (int i = 0; i < 6; i++)
                     {
                       Debug.print(String(buffer[i]) + " ");
                     }
                     Debug.println("");
#endif

                     file.write(buffer, 6);
                   }

                   file.close();

#ifdef DEBUG
                   Debug.println("✅ File write complete.");
#endif
                 });

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
      loopCounter = 0;
      delete[] orderArray;
      animationName = payload;
      animationName.trim();
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
      mqttProgram = 0;
      Dir dir = LittleFS.openDir("/");
      while (dir.next()) {
        String filename = dir.fileName();
        if (filename != "ExternalSSID.txt" && filename != "ssid.txt" && filename != "icon.png") {
          LittleFS.remove(filename);
        }
      }
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
  server.serveStatic("/assets/icon.png", LittleFS, "/assets/icon.png");
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
int frameCount = 0;
unsigned int lastDisplayed = millis();
int fileCount = 0;
int hueOffset = 0;
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
  dnsServer.processNextRequest();

  if (mqttProgram == 0)
  {
    strip.rainbow(hueOffset, 1, 255, 255, true); // Gebruik de ingebouwde rainbow functie
    strip.show();
    hueOffset += 256;
    delay(5);
  }

  if (mqttProgram == 1)
  {
    if (millis() - lastDisplayed >= waitTime)
    {
      if (arrayCounter >= arraySize)
      {
        arrayCounter = 0;
      }
      // waitTime = readFrameBatchFromLittleFS(orderArray[arrayCounter]);
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
      fileName = "/" + animationName + ".dat";
      File file = LittleFS.open(fileName.c_str(), "r");
#ifdef DEBUG
      int byteCount = 0;
      while (file.available())
      {
        uint8_t byte = file.read();
        Debug.printf("%02X ", byte); // Print byte als hex

        byteCount++;
        if (byteCount % sizeof(LEDFrameData) == 0)
        { // Nieuwe lijn na 8 bytes
          Debug.println("");
        }
      }
#endif
      fileCount = file.size() / NUM_LEDS / sizeof(LEDFrameData);
      isFirst = false; // Ensures it's only done the first time
#ifdef DEBUG
      Debug.printf("Frame Count: %d \n\r", fileCount);
      Debug.printf("File size: %d \n\r", file.size());
#endif
      file.close();
    }

    if (millis() - lastDisplayed >= waitTime)
    {
      if (frameCount >= fileCount)
      {
        frameCount = 0;
        loopCounter++;
      }
      if (loopAmount == 0 || loopCounter < loopAmount)
      {
        waitTime = readFrameBatchFromLittleFS(frameCount, fileName.c_str());
#ifdef DEBUG
        Debug.println(String(waitTime));
#endif
        frameCount++;
      }
      else if (afterFill)
      {
        readFrameBatchFromLittleFS(fileCount-1, fileName.c_str());
      }
      else
      {
        strip.clear();
        strip.show();
      }
      lastDisplayed = millis();
    }
  }

  if (mqttProgram == 3)
  {
  }
}
