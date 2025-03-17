#include <Arduino.h>
#include <PicoMQTT.h>
#include <LittleFS.h>
#include <ArduinoOTA.h>
#include <Adafruit_NeoPixel.h>

// #define DEBUG
#ifdef DEBUG
#include <telnetServer.h>
SerialTelnet telnet;
#endif

#include <WiFiUdp.h>
const int udpPort = 9000;
WiFiUDP udp;
IPAddress broadcastIP;

#include <WebServerHandler.h>
WebServerHandler webServer(80);
String wifimode = "";
String fileName = "";
String animationName = "";
bool afterFill = false;
int loopAmount = 0;
int loopCounter = 0;
File file;

void closeAnimationFile()
{
  if (file)
    file.close();
}

String loadExtSSID()
{
  if (!LittleFS.exists("/ExternalSSID.txt"))
  {
    return "";
  }
  closeAnimationFile();
  file = LittleFS.open("/ExternalSSID.txt", "r");
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
  closeAnimationFile();
  file = LittleFS.open("/ExternalSSID.txt", "r");
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
  closeAnimationFile();
  file = LittleFS.open("/ExternalSSID.txt", "r");
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
int readFrameBatchFromLittleFS(int receivedFrame)
{
  if (!file)
  {
#ifdef DEBUG
    telnet.println("❌ Failed to open file!");
#endif
    file.close();
    return 0;
  }

  int offset = receivedFrame * NUM_LEDS * sizeof(LEDFrameData);
  if (!file.seek(offset, SeekSet))
  {
#ifdef DEBUG
    telnet.println("❌ Failed to seek in file!");
#endif
    file.close();
    return 0;
  }

  int duration = 0;

  const int BATCH_SIZE = NUM_LEDS; // 196 LEDs in één keer
  uint8_t buffer[BATCH_SIZE * sizeof(LEDFrameData)];

  // Lees het hele frame in één keer
  if (file.read(buffer, NUM_LEDS * sizeof(LEDFrameData)) != NUM_LEDS * sizeof(LEDFrameData))
  {
#ifdef DEBUG
    telnet.println("❌ Fout bij lezen van frame!");
#endif
    file.close();
    return 0;
  }

  // Verwerk de LED-gegevens
  for (int i = 0; i < NUM_LEDS; i++)
  {
    int index = i * sizeof(LEDFrameData);
    strip.setPixelColor(i, strip.Color(
                               ((buffer[index] * buffer[index + 3]) >> 8),
                               ((buffer[index + 1] * buffer[index + 3]) >> 8),
                               ((buffer[index + 2] * buffer[index + 3]) >> 8)));
    if (i % 10 == 0)
      yield(); // Elke 10 LEDs
  }

  // Voorkom watchdog-reset en toon de animatie
  yield();
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
  closeAnimationFile();
  file = LittleFS.open("/ssid.txt", "r");
  if (!file)
  {
    return "";
  }
  String ssid = file.readString();
  file.close();
  ssid.trim();
  return ssid;
}

void apSetup()
{
  wifimode = "AP";
  String apSSID = loadSSIDFromFS(); // Try to load saved SSID

  if (apSSID == "") // NO saved SSID? Generate one
  {
#ifdef DEBUG
    telnet.println("No SSID found, generating a new one.");
#endif
    srand(analogRead(A0));
    int randomnum = rand();
    apSSID = "KioskLed" + String(randomnum);
    webServer.saveSSIDToFS(apSSID, file); // Save for next boot
  }
#ifdef DEBUG
  else
  {
    telnet.println("SSID loaded from FS: " + apSSID);
  }
#endif
  String apPassword = "KioskLed";
  WiFi.softAP(apSSID.c_str(), apPassword);
#ifdef DEBUG
  telnet.println("Access Point Started");
  telnet.println(apSSID);
  telnet.print("AP IP Address: ");
  telnet.println(WiFi.softAPIP().toString());
#endif
}

void setup()
{
  // Start LittleFS
  if (!LittleFS.begin())
  {
#ifdef DEBUG
    telnet.println("LittleFS mount failed");
#endif
    return;
  }

// Usual setup
#ifdef DEBUG
  telnet.begin(115200);
  telnet.print("Connecting");
#endif
  // AP
  // Check if a local wifi credential is saved
  String storedSSID = loadExtSSID();
  String storedPASS = loadExtPASS();
  String storedIP = loadExtIP();
  storedSSID.toCharArray(webServer.getSSID(), 32);
  storedPASS.toCharArray(webServer.getPASS(), 64);
  IPAddress staticIP;

  if (strlen(webServer.getSSID()) > 0)
  {
    wifimode = "STA";
    WiFi.setOutputPower(16.0);
    WiFi.mode(WIFI_STA);

    // **Geldig IP-adres controleren en toepassen**
    if (storedIP.length() > 0 && staticIP.fromString(storedIP))
    {
#ifdef DEBUG
      telnet.print("Statisch IP ingesteld op: ");
      telnet.println(staticIP.toString());
#endif
      WiFi.config(staticIP, WiFi.gatewayIP(), WiFi.subnetMask());
    }
#ifdef DEBUG
    else
    {
      telnet.println("Geen geldig statisch IP, DHCP wordt gebruikt.");
    }
#endif

    WiFi.begin(webServer.getSSID(), webServer.getPASS());
#ifdef DEBUG
    telnet.print("Connecting to WiFi");
#endif
    int retries = 0;
    while (WiFi.status() != WL_CONNECTED && retries < 15)
    {
      delay(500);
#ifdef DEBUG
      telnet.print(".");
#endif
      retries++;
    }
#ifdef DEBUG
    telnet.println("");
#endif
    if (WiFi.status() != WL_CONNECTED)
    {
      apSetup();
    }
#ifdef DEBUG
    else
    {
      telnet.println("Connected to WiFi!");
      telnet.print("IP Address: ");
      telnet.println(WiFi.localIP().toString());
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
                   telnet.printf("Received message in topic '%s'\r\n", topic);
#endif

                   // **Stap 1: Lees de eerste regel (bestandsnaam, afterFill and loopCount)**
                   String firstLine = "";
                   char c;

                   while (packet.get_remaining_size() > 0 && (c = packet.read()) != '\n' && c != '\r')
                   {
                     firstLine += c;
                   }
                   firstLine.trim();                          // Verwijder overbodige spaties
                   int splitIndex1 = firstLine.indexOf('\t'); // Verwacht "filename<TAB>afterFill<TAB>loopCount"
                   int splitIndex2 = firstLine.indexOf('\t', splitIndex1 + 1);
                   if (splitIndex1 == -1)
                   {
#ifdef DEBUG
                     telnet.println("❌ Invalid first line format!");
#endif
                     return;
                   }

                   String fileName = "/" + firstLine.substring(0, splitIndex1) + ".dat";      // Bestandsnaam
                   afterFill = firstLine.substring(splitIndex1 + 1, splitIndex2).toInt() > 0; // Boolean
                   loopAmount = firstLine.substring(splitIndex2 + 1).toInt();                 // Derde waarde

#ifdef DEBUG
                   telnet.printf("File name: '%s', Afterfill: %d, loop amount: %d\r\n", fileName.c_str(), afterFill, loopAmount);
#endif

                   // **Stap 2: Bestand verwijderen indien nodig**
                   if (LittleFS.exists(fileName))
                   {
#ifdef DEBUG
                     telnet.printf("Removed existing file: %s\r\n", fileName.c_str());
#endif
                     LittleFS.remove(fileName);
                   }

                   // **Stap 3: Open bestand en schrijf binaire data**
                   closeAnimationFile();
                   file = LittleFS.open(fileName, "a");
                   if (!file)
                   {
#ifdef DEBUG
                     telnet.println("❌ Error opening file for writing!");
#endif
                     return;
                   }

                   while (packet.get_remaining_size() >= 6)
                   {
                     uint8_t buffer[6];
                     packet.read(buffer, 6);

#ifdef DEBUG
                     telnet.print("Buffer inhoud: ");
                     for (int i = 0; i < 6; i++)
                     {
                       telnet.print(String(buffer[i]) + " ");
                     }
                     telnet.println("");
#endif

                     file.write(buffer, 6);
                   }

                   file.close();

#ifdef DEBUG
                   telnet.println("✅ File write complete.");
#endif
                 });

  mqtt.subscribe("display/#", [](const char *topic, const char *payload)
                 {
#ifdef DEBUG       
    telnet.printf("Received message in topic '%s'\r\n", topic);
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
      telnet.printf("Received: r = %d, g = %d, b = %d, level = %d\r\n", r,g,b,level);
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
      telnet.println("Received order: " + order);  // Print de ontvangen string
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
          telnet.print("Order " + String(i) + ": " + String(orderArray[i]) + "\n");
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

  webServer.dnsSetup(wifimode);

  // UDP SETUP
  udp.begin(udpPort);
  webServer.begin();
#ifdef DEBUG
  Dir dir = LittleFS.openDir("/");
  while (dir.next()) // Loop through all files
  {
    telnet.println(dir.fileName());
  }
  FSInfo fs_info;
  LittleFS.info(fs_info);
  telnet.printf("Total: %u bytes\nUsed: %u bytes\nFree space: %u\n\r", fs_info.totalBytes, fs_info.usedBytes, (fs_info.totalBytes - fs_info.usedBytes));
#endif
}

int frameCount = 0;
unsigned int lastDisplayed = millis();
int fileCount = 0;
int hueOffset = 0;

void loop()
{
  char incomingPacket[255]; // Buffer voor inkomend bericht

  int packetSize = udp.parsePacket();
  if (packetSize)
  {
    int len = udp.read(incomingPacket, 255);
    if (len > 0)
    {
      incomingPacket[len] = '\0'; // Zet het einde van de string
    }
#ifdef DEBUG
    telnet.printf("📩 Ontvangen: %s van %s\n", incomingPacket, udp.remoteIP().toString().c_str());
#endif
    // **Controleer of het bericht correct is**
    if (strcmp(incomingPacket, "ESP_FIND") == 0)
    {
      String response = "ESP_FOUND " + WiFi.localIP().toString() + " " + loadSSIDFromFS();
#ifdef DEBUG
      telnet.println("📡 Verzenden antwoord: " + response);
#endif
      udp.beginPacket(udp.remoteIP(), udp.remotePort());
      udp.write(response.c_str());
      udp.endPacket();
    }
  }
#ifdef DEBUG
  telnet.handleClient();
#endif
  mqtt.loop();
  ArduinoOTA.handle();
  webServer.handle();

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
      telnet.println(String(waitTime));
      telnet.printf("arrayCounter: %d frameNumber: %d\r\n", arrayCounter, orderArray[arrayCounter]);
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
      closeAnimationFile();
      file = LittleFS.open(fileName.c_str(), "r");
      if (!file)
      {
#ifdef DEBUG
        telnet.println("❌ Failed to open file!");
#endif
        return;
      }
      fileCount = file.size() / NUM_LEDS / sizeof(LEDFrameData);
      isFirst = false;
#ifdef DEBUG
      int byteCount = 0;
      while (file.available())
      {
        uint8_t byte = file.read();
        telnet.printf("%02X ", byte);
        byteCount++;
        if (byteCount % sizeof(LEDFrameData) == 0)
        {
          telnet.println("");
        }
      }
      telnet.printf("Frame Count: %d \n\r", fileCount);
      telnet.printf("File size: %d \n\r", file.size());
#endif
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
        waitTime = readFrameBatchFromLittleFS(frameCount);
#ifdef DEBUG
        telnet.println(String(waitTime));
#endif
        frameCount++;
      }
      else if (afterFill)
        ;
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
