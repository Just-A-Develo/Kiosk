#include <FS.h>
#include <LittleFS.h>
#include <PicoMQTT.h>
#include <ArduinoOTA.h>
#include <WiFiClient.h>
#include <ESP8266WiFi.h>
#include <Adafruit_NeoPixel.h>
#include <ESP8266WebServer.h>
#include <WiFiUdp.h>

/*    All Parameters    */
// UDP
const int udpPort = 9000;
WiFiUDP udp;
IPAddress broadcastIP;

// Globale buffers
char genericSSID[32];     // Wordt in setup samengesteld
char storedSSID[32] = ""; // Wifi SSID (eventueel uit bestand)
char storedPASS[64] = ""; // Wifi wachtwoord
char wifimode[16] = "";   // Bijvoorbeeld "STA" of "AP"

char fileName[64] = "";      // Bestand voor animatiegegevens
char animationName[32] = ""; // Naam van de animatie (zonder pad of extensie)

// Animations
bool afterFill = false;
int loopAmount = 0;
int loopCounter = 0;
unsigned int lastDisplayed = 0;
int fileCount = 0;
int hueOffset = 0;
File file;

#define LED_PIN 4
#define NUM_LEDS 65
Adafruit_NeoPixel strip = Adafruit_NeoPixel(NUM_LEDS, LED_PIN, NEO_BRG + NEO_KHZ400);

struct LEDFrameData
{
  uint8_t r;
  uint8_t g;
  uint8_t b;
  uint8_t intensity;
  uint16_t delayMs;
};

unsigned long lastRainbow = 0;
unsigned long rainbowDelay = 50;

// Webserver
ESP8266WebServer server(80);
const char *apPassword = "BOXaPOS1"; // AP wachtwoord

// MQTT
PicoMQTT::Server mqtt;
bool isFirst = true;
unsigned int waitTime = 0;
int arrayCounter = 0;
int r, g, b = 0;
int level = 255;
int mqttProgram = 0;
int frameCount = 0;

unsigned long lastReconnectAttempt = 0;
const unsigned long RECONNECT_INTERVAL = 500;
bool reconnecting = false;
int attempt = 0;

// ---------------------------------------------------------------------------
// Functies voor bestandshantering
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
  file.readStringUntil('\n'); // Sla SSID over
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
  file.readStringUntil('\n'); // Sla SSID over
  file.readStringUntil('\n'); // Sla PASS over
  String ip = file.readString();
  file.close();
  ip.trim();
  return ip;
}

// ---------------------------------------------------------------------------
// HTTP-handle functies
const char htmlPage[] PROGMEM = R"rawliteral(
  <html><head>
  <title>ESP8266 WiFi Config</title>
  <meta name='viewport' content='width=device-width, initial-scale=1.0'>
  <style>
  body {font-family: Arial, sans-serif;background-color: #f0f0f0;margin: 0;padding: 0;display: flex;flex-direction: column;justify-content: center;align-items: center;height: 100vh;}
  .container {width: 90%;max-width: 400px;background: white;padding: 20px;border-radius: 8px;box-shadow: 0 0 10px rgba(0, 0, 0, 0.1);text-align: center;}
  header {background-color: #27a8c9;padding: 15px;color: white;border-radius: 8px 8px 8px 8px;font-size: 20px;}
  p {font-size: 16px;margin: 10px 0;}
  form {display: flex;flex-direction: column;margin-top: 15px;margin-bottom: 0;}
  img {width: 100px;max-width: 100px;min-width: 50px;background-color: transparent;margin-bottom: 2.5vh;}
  .input-container {position: relative;display: inline-block;margin-top: 20px;}
  .input-container input {width: 100%;padding: 10px;font-size: 16px;border: 2px solid #ccc;border-radius: 5px;box-sizing: border-box;}
  .input-container.ssid::before {content: "SSID";position: absolute;top: -10px;left: 10px;font-size: 14px;color: #555;background-color: #fff;padding: 0 5px;}
  .input-container.password::before {content: "Password";position: absolute;top: -10px;left: 10px;font-size: 14px;color: #555;background-color: #fff;padding: 0 5px;}
  .input-container.ip::before {content: "IP Address";position: absolute;top: -10px;left: 10px;font-size: 14px;color: #555;background-color: #fff;padding: 0 5px;}
  input[type='submit'] {background-color: #27a8c9;color: white;font-size: 18px;padding: 12px;border: none;border-radius: 4px;cursor: pointer;margin-top: 20px;}
  input[type='submit']:hover {background-color: #45a049;}
  @media (max-width: 480px) {.container {  width: 95%;  padding: 15px;}
  header {  font-size: 18px;}
  input {  font-size: 14px;  padding: 8px;}
  input[type='submit'] {  font-size: 16px;  padding: 10px;}}
  </style></head>
  <body>
  <img src="/assets/icon.png" alt="BOXaPOS">
  <div class='container'>
  <header>WiFi Config</header>
  <p>Current IP Address: )rawliteral";

void handleRoot()
{
  String page = FPSTR(htmlPage);
  page += WiFi.softAPIP().toString();
  page += F("</p><form action='/save' method='POST'>"
            "<div class='input-container ssid'><input type='text' name='ssid' required></div>"
            "<div class='input-container password'><input type='password' name='password' required></div>"
            "<div class='input-container ip'><input type='text' name='ip' placeholder='Ex. 192.168.1.100'></div>"
            "<div class='input-container name'><input type='text' name='name' placeholder='Ex. Kiosk1'></div>"
            "<input type='submit' value='Save & Connect'></form></div></body></html>");

  server.send(200, "text/html", page);
}

void handleSave()
{
  // Haal de waarden op uit de server-argumenten (als Strings)
  String sSSID = server.arg("ssid");
  String sPassword = server.arg("password");
  String sIP = server.arg("ip");
  String sName = server.arg("name");

  // Kopieer de waarden naar vaste buffers
  char tempSSID[32], tempPASS[64], tempIP[16], tempName[32];
  strncpy(tempSSID, sSSID.c_str(), sizeof(tempSSID) - 1);
  strncpy(tempPASS, sPassword.c_str(), sizeof(tempPASS) - 1);
  strncpy(tempIP, sIP.c_str(), sizeof(tempIP) - 1);
  strncpy(tempName, sName.c_str(), sizeof(tempName) - 1);
  tempSSID[sizeof(tempSSID) - 1] = '\0';
  tempPASS[sizeof(tempPASS) - 1] = '\0';
  tempIP[sizeof(tempIP) - 1] = '\0';
  tempName[sizeof(tempName) - 1] = '\0';

  Serial.print("Received SSID: ");
  Serial.println(tempSSID);
  Serial.print("Received Password: ");
  Serial.println(tempPASS);
  Serial.print("Received IP: ");
  Serial.println(tempIP);
  Serial.print("Received Name: ");
  Serial.println(tempName);

  // Hier kun je de waarden verder verwerken (bijv. opslaan naar LittleFS of globalen updaten)

  server.send(200, "text/html", "<h1>Settings saved!</h1>");
}

// Lees een batch frames uit LittleFS en update de LED-strip
int readFrameBatchFromLittleFS(int receivedFrame)
{
  if (!file)
  {
    file = LittleFS.open(fileName, "r");
    if (!file)
    {
#ifdef DEBUG
      // telnet.println("❌ Failed to open file!");
#endif
      return 0;
    }
  }

  int offset = receivedFrame * NUM_LEDS * sizeof(LEDFrameData);
  if (!file.seek(offset, SeekSet))
  {
#ifdef DEBUG
    // telnet.println("❌ Failed to seek in file!");
#endif
    file.close();
    return 0;
  }

  int duration = 0;
  const int BATCH_SIZE = NUM_LEDS;
  uint8_t buffer[BATCH_SIZE * sizeof(LEDFrameData)];

  if (file.read(buffer, NUM_LEDS * sizeof(LEDFrameData)) != NUM_LEDS * sizeof(LEDFrameData))
  {
    file.close();
    return 0;
  }

  for (int i = 0; i < NUM_LEDS; i++)
  {
    int index = i * sizeof(LEDFrameData);
    // Pas intensiteit toe bij het instellen van de kleur
    strip.setPixelColor(i, strip.Color(
                               ((buffer[index] * buffer[index + 3]) >> 8),
                               ((buffer[index + 1] * buffer[index + 3]) >> 8),
                               ((buffer[index + 2] * buffer[index + 3]) >> 8)));
    if (i % 10 == 0)
      ESP.wdtFeed();
  }
  duration = buffer[4] | (buffer[5] << 8);
  strip.show();
  ESP.wdtFeed();

  return duration;
}

void setColor(int r, int g, int b, int intens)
{
  uint32_t color = strip.Color(r, g, b);
  strip.setBrightness(intens);
  strip.fill(color, 0, NUM_LEDS);
  strip.show();
  yield();
}

// ---------------------------------------------------------------------------
// AP en WiFi setup
void apSetup()
{
  WiFi.softAP(genericSSID, apPassword);
}

void wifiInit()
{
  // Haal opgeslagen SSID en wachtwoord op en kopieer naar globale buffers
  String extSSID = loadExtSSID();
  String extPASS = loadExtPASS();
  if (extSSID.length() > 0 && extPASS.length() > 0)
  {
    strncpy(storedSSID, extSSID.c_str(), sizeof(storedSSID) - 1);
    strncpy(storedPASS, extPASS.c_str(), sizeof(storedPASS) - 1);
    storedSSID[sizeof(storedSSID) - 1] = '\0';
    storedPASS[sizeof(storedPASS) - 1] = '\0';
  }

  // Als opgeslagen SSID/wachtwoord aanwezig zijn, probeer dan te verbinden
  if (storedSSID[0] != '\0' && storedPASS[0] != '\0')
  {
    Serial.println("Setting up Wifi");
    strncpy(wifimode, "STA", sizeof(wifimode) - 1);
    wifimode[sizeof(wifimode) - 1] = '\0';

    WiFi.mode(WIFI_STA);
    WiFi.setOutputPower(16.0);
    WiFi.persistent(false);
    WiFi.setSleepMode(WIFI_NONE_SLEEP);
    WiFi.setAutoReconnect(true);
    WiFi.begin(storedSSID, storedPASS);

    int retries = 0;
    while (WiFi.status() != WL_CONNECTED && retries < 15)
    {
      delay(500);
      retries++;
    }
    if (WiFi.status() != WL_CONNECTED)
    {
      Serial.println("Can't connect, switching to AP");
      ArduinoOTA.begin();
      apSetup();
    }
  }
  else
  {
    Serial.println("No saved SSID or Password");
    apSetup();
  }
}

// ---------------------------------------------------------------------------
// Setup en Loop
void setup()
{
  Serial.begin(115200);
  Serial.println();
  Serial.print("Configuring access point...");

  // Stel genericSSID samen op basis van "BOXaPOS" en chip-ID
  uint32_t chipID = ESP.getChipId();
  char chipIDStr[9];
  snprintf(chipIDStr, sizeof(chipIDStr), "%08X", chipID);
  snprintf(genericSSID, sizeof(genericSSID), "%s%s", "BOXaPOS", chipIDStr);
  Serial.println(genericSSID);

  // Start WiFi
  wifiInit();

  // Print het AP IP-adres
  IPAddress myIP = WiFi.softAPIP();
  Serial.print("AP IP address: ");
  Serial.println(myIP);

  // Mount LittleFS
  if (!LittleFS.begin())
  {
    Serial.println("LittleFS mount failed!");
    return;
  }

  // Webserver: Serve statische bestanden en definieer routes
  server.serveStatic("/assets/icon.png", LittleFS, "/assets/icon.png");
  server.on("/", handleRoot);
  server.on("/save", HTTP_POST, handleSave);
  server.begin();
  Serial.println("HTTP server started");

  ArduinoOTA.begin();

  // MQTT-subscriptions
  mqtt.subscribe("update/#", [](const char *topic, PicoMQTT::IncomingPacket &packet)
                 {
    // Stap 1: Lees de eerste regel (bestandsnaam, afterFill en loopCount)
    String firstLine = "";
    char c;
    while (packet.get_remaining_size() > 0 && (c = packet.read()) != '\n' && c != '\r') {
      firstLine += c;
    }
    firstLine.trim();
    int splitIndex1 = firstLine.indexOf('\t');
    int splitIndex2 = firstLine.indexOf('\t', splitIndex1 + 1);
    if (splitIndex1 == -1) {
      return;
    }
    // Bouw de bestandsnaam: "/" + bestandsnaam + ".dat"
    String tmpFileName = "/" + firstLine.substring(0, splitIndex1) + ".dat";
    // Sla de waarden op in globale variabelen (na conversie naar char array)
    strncpy(fileName, tmpFileName.c_str(), sizeof(fileName)-1);
    fileName[sizeof(fileName)-1] = '\0';
    afterFill = (firstLine.substring(splitIndex1 + 1, splitIndex2).toInt() > 0);
    loopAmount = firstLine.substring(splitIndex2 + 1).toInt();

    // Stap 2: Verwijder bestaand bestand indien nodig
    if (LittleFS.exists(fileName)) {
      LittleFS.remove(fileName);
    }
    // Stap 3: Open bestand en schrijf binaire data
    closeAnimationFile();
    file = LittleFS.open(fileName, "a");
    if (!file) {
      return;
    }
    while (packet.get_remaining_size() >= 6) {
      uint8_t buffer[6];
      packet.read(buffer, 6);
      file.write(buffer, 6);
    }
    file.close(); });

  mqtt.subscribe("display/#", [](const char *topic, const char *payload)
                 {
    if (!strcmp(topic, "display/color")) {
      mqttProgram = 3;
      // Gebruik tijdelijk een String voor het splitsen
      String color = payload;
      int comma1 = color.indexOf(',');
      int comma2 = color.lastIndexOf(',');
      r = color.substring(0, comma1).toInt();
      g = color.substring(comma1 + 1, comma2).toInt();
      b = color.substring(comma2 + 1).toInt();
    }
    if (!strcmp(topic, "display/colorBrightness")) {
      mqttProgram = 3;
      String brightness = payload;
      level = brightness.toInt();
      setColor(r, g, b, level);
    }
    if (!strcmp(topic, "display/displayEeprom")) {
      isFirst = true;
      mqttProgram = 2;
      loopCounter = 0;
      // Sla de animatienaam op in een char array
      strncpy(animationName, payload, sizeof(animationName)-1);
      animationName[sizeof(animationName)-1] = '\0';
    }
    if (!strcmp(topic, "display/reset")) {
      mqttProgram = 9;
      Dir dir = LittleFS.openDir("/");
      while (dir.next()) {
        LittleFS.remove(dir.fileName());
      }
      WiFi.disconnect();
      delay(1000);
      ESP.restart();
    }
    if (!strcmp(topic, "display/resetFrames")) {
      mqttProgram = 0;
      Dir dir = LittleFS.openDir("/");
      while (dir.next()) {
        String fname = dir.fileName();
        if (fname != "ExternalSSID.txt" && fname != "ssid.txt" && fname != "icon.png") {
          LittleFS.remove(fname);
        }
      }
    }
    if (!strcmp(topic, "display/default")) {
      mqttProgram = 0;
    }
    if (!strcmp(topic, "display/getIP")) {
      IPAddress ip = WiFi.localIP();
      String ipString = ip.toString();
      mqtt.publish("display/returnIP", ipString.c_str());
    } });

  mqtt.begin();
  strip.begin();
  strip.show(); // Zet de LED-strip uit (initialiseer)
  udp.begin(udpPort);
  lastRainbow = millis();
  lastDisplayed = millis();
}

void loop()
{
  // Verwerk OTA-updates en MQTT
  ArduinoOTA.handle();
  mqtt.loop();
  ESP.wdtFeed();
  server.handleClient();

  // Verwerk inkomende UDP-pakketten
  char incomingPacket[255];
  int packetSize = udp.parsePacket();
  if (packetSize)
  {
    int len = udp.read(incomingPacket, 255);
    if (len > 0)
      incomingPacket[len] = '\0';

#ifdef DEBUG
    // telnet.printf("📩 Ontvangen: %s van %s\n", incomingPacket, udp.remoteIP().toString().c_str());
#endif

    if (strcmp(incomingPacket, "ESP_FIND") == 0)
    {
      // Bouw antwoordstring op (tijdelijke String hier, want het samenstellen is eenvoudiger)
      String response = "ESP_FOUND " +
                        (WiFi.getMode() == WIFI_AP ? WiFi.softAPIP().toString() : WiFi.localIP().toString()) + " " + genericSSID;
#ifdef DEBUG
      // telnet.println("📡 Verzenden antwoord: " + response);
#endif
      udp.beginPacket(udp.remoteIP(), udp.remotePort());
      udp.write(response.c_str());
      udp.endPacket();
      isFirst = true;
    }
  }

  // LED Animaties
  if (mqttProgram == 0)
  {
    if (isFirst)
    {
      fileCount = 0;
      closeAnimationFile();
      file = LittleFS.open("/defaultRainbow.dat", "r");
      if (!file)
      {
        return;
      }
      fileCount = file.size() / (NUM_LEDS * sizeof(LEDFrameData));
      isFirst = false;
    }
    if (millis() - lastDisplayed >= waitTime)
    {
      if (frameCount >= fileCount)
      {
        frameCount = 0;
        loopCounter++;
      }
      waitTime = readFrameBatchFromLittleFS(frameCount);
      frameCount++;
      lastDisplayed = millis();
    }
  }

  else if (mqttProgram == 2)
  {
    if (isFirst)
    {
      fileCount = 0;
      // Bouw de bestandsnaam voor de animatie op: "/" + animatieNaam + ".dat"
      String tmp = "/" + String(animationName) + ".dat";
      strncpy(fileName, tmp.c_str(), sizeof(fileName) - 1);
      fileName[sizeof(fileName) - 1] = '\0';
      closeAnimationFile();
      file = LittleFS.open(fileName, "r");
      if (!file)
      {
#ifdef DEBUG
        // telnet.println("❌ Failed to open file!");
#endif
        return;
      }
      fileCount = file.size() / (NUM_LEDS * sizeof(LEDFrameData));
      isFirst = false;
#ifdef DEBUG
      int byteCount = 0;
      while (file.available())
      {
        uint8_t byte = file.read();
        // telnet.printf("%02X ", byte);
        if (++byteCount % sizeof(LEDFrameData) == 0)
          ; // telnet.println("");
      }
      // telnet.printf("Frame Count: %d \n\r", fileCount);
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
        frameCount++;
      }
      else
      {
        strip.clear();
        strip.show();
        yield();
      }
      lastDisplayed = millis();
    }
  }
}
