#include <LittleFS.h>
#include <PicoMQTT.h>
#include <ArduinoOTA.h>
#include <ESP8266WiFi.h>
#include <Adafruit_NeoPixel.h>
#include <ESP8266WebServer.h>
#include <WiFiUdp.h>
#include <ESP8266Ping.h>

extern "C"
{
#include "user_interface.h"
}

// #define DEBUG
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
SerialTelnet Debug;
#endif

/*    All Parameters    */
// UDP
const int udpPort = 9000;
WiFiUDP udp;
IPAddress broadcastIP;

// Globale buffers
char genericSSID[32];        // Wordt in setup samengesteld
char storedSSID[32] = "";    // Wifi SSID (eventueel uit bestand)
char storedPASS[32] = "";    // Wifi wachtwoord
char wifimode[16] = "";      // Bijvoorbeeld "STA" of "AP"
char fileName[32] = "";      // Bestand voor animatiegegevens
char animationName[32] = ""; // Naam van de animatie (zonder pad of extensie)

// Animations
bool afterFill = false;
uint8_t loopAmount = 0;
uint8_t loopCounter = 0;
uint16_t fileCount = 0;
int firstLineLength = 0;
unsigned int lastDisplayed = 0;
File file;
bool boot = true;

#define LED_PIN 4
#define NUM_LEDS 41
Adafruit_NeoPixel strip = Adafruit_NeoPixel(NUM_LEDS, LED_PIN, NEO_RGB + NEO_KHZ400);

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
bool isFirstRead = true;
unsigned int waitTime = 0;
uint8_t r, g, b = 0;
uint8_t level = 255;
uint8_t mqttProgram = 0;
uint8_t frameCount = 0;

unsigned long lastReconnectAttempt = 0;
const unsigned long RECONNECT_INTERVAL = 500;
bool reconnecting = false;
uint8_t attempt = 0;

// ---------------------------------------------------------------------------
// Functies voor bestandshantering
void closeAnimationFile()
{
  if (file)
  {
    // Serial.printf("Closing file with name: %s\n\r", file.fullName());
    file.close();
    file = File();
  }
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

  closeAnimationFile();
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
  closeAnimationFile();
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
    closeAnimationFile();
    return "";
  }
  file.readStringUntil('\n'); // Sla SSID over
  file.readStringUntil('\n'); // Sla PASS over

  String ip = file.readString();
  closeAnimationFile();
  ip.trim();
  return ip;
}

void saveCredentials(String tempSSID, String tempPASS, String tempIP)
{
  if (LittleFS.exists("/ExternalSSID.txt"))
  {
    LittleFS.remove("/ExternalSSID.txt");
  }
  closeAnimationFile();
  file = LittleFS.open("/ExternalSSID.txt", "w");
  if (!file)
  {
    closeAnimationFile();
    return;
  }

  file.println(tempSSID);
  file.println(tempPASS);
  file.println(tempIP);

  closeAnimationFile();
}

// ---------------------------------------------------------------------------
// AP en WiFi setup
void apSetup()
{
  // zorg dat alles echt af is
  boot = true;
  isFirst = true;
  WiFi.softAPdisconnect(true);
  WiFi.disconnect(true);
  delay(100);

  // AP‑modus aanzetten zonder sleep
  WiFi.mode(WIFI_AP);
  WiFi.setPhyMode(WIFI_PHY_MODE_11B);
  WiFi.setSleep(false);
  WiFi.setSleepMode(WIFI_NONE_SLEEP);
  WiFi.softAP(genericSSID, apPassword);
}

void checkAP()
{
  WiFi.softAPdisconnect(true);
  WiFi.disconnect(true);
  delay(100);
  WiFi.softAP(genericSSID, apPassword);
}

void wifiInit()
{
  // Haal opgeslagen SSID en wachtwoord op en kopieer naar globale buffers
  closeAnimationFile();
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
    // Serial.println("Setting up Wifi");
    strncpy(wifimode, "STA", sizeof(wifimode) - 1);
    wifimode[sizeof(wifimode) - 1] = '\0';

    WiFi.mode(WIFI_STA);
    WiFi.begin(storedSSID, storedPASS);

    uint8_t retries = 0;
    while (WiFi.status() != WL_CONNECTED && retries < 15)
    {
      delay(500);
      retries++;
    }
    if (WiFi.status() != WL_CONNECTED)
    {
      // Serial.println("Can't connect, switching to AP");
      ArduinoOTA.begin();
      apSetup();
    }
  }
  else
  {
    // Serial.println("No saved SSID or Password");
    apSetup();
  }
}

// ---------------------------------------------------------------------------
// HTTP-handle functies
const char htmlPage[] PROGMEM = R"rawliteral(
  <html>

<head>
  <title>ESP8266 WiFi Config</title>
  <link rel="icon" type="image/x-icon" href="/assets/icon.ico">
  <meta name='viewport' content='width=device-width, initial-scale=1.0'>
  <style>
    body {
      font-family: Arial, sans-serif;
      background-color: #f0f0f0;
      margin: 0;
      padding: 0;
      display: flex;
      flex-direction: column;
      justify-content: center;
      align-items: center;
      height: 100vh;
    }
    .container {
      width: 90%;
      max-width: 400px;
      background: white;
      padding: 20px;
      border-radius: 8px;
      box-shadow: 0 0 10px rgba(0, 0, 0, 0.1);
      text-align: center;
    }
    header {
      background-color: #27a8c9;
      padding: 15px;
      color: white;
      border-radius: 8px 8px 8px 8px;
      font-size: 20px;
    }
    p {
      font-size: 16px;
      margin: 10px 0;
    }
    form {
      display: flex;
      flex-direction: column;
      margin-top: 15px;
      margin-bottom: 0;
    }
    img {
      width: 100px;
      max-width: 100px;
      min-width: 50px;
      background-color: transparent;
      margin-bottom: 2.5vh;
    }
    .input-container {
      position: relative;
      display: inline-block;
      margin-top: 20px;
    }
    .input-container input {
      width: 100%;
      padding: 10px;
      font-size: 16px;
      border: 2px solid #ccc;
      border-radius: 5px;
      box-sizing: border-box;
    }
    .input-container.ssid::before {
      content: "SSID";
      position: absolute;
      top: -10px;
      left: 10px;
      font-size: 14px;
      color: #555;
      background-color: #fff;
      padding: 0 5px;
    }
    .input-container.password::before {
      content: "Password";
      position: absolute;
      top: -10px;
      left: 10px;
      font-size: 14px;
      color: #555;
      background-color: #fff;
      padding: 0 5px;
    }
    .input-container.ip::before {
      content: "IP Address";
      position: absolute;
      top: -10px;
      left: 10px;
      font-size: 14px;
      color: #555;
      background-color: #fff;
      padding: 0 5px;
    }
    input[type='submit'] {
      background-color:rgb(39, 169, 201);
      color: white;
      font-size: 18px;
      padding: 12px;
      border: none;
      border-radius: 4px;
      cursor: pointer;
      margin-top: 20px;
    }
    input[type='submit']:hover {
      background-color: #45a049;
    }
    @media (max-width: 480px) {
      .container {
        width: 95%;
        padding: 15px;
      }
      header {
        font-size: 18px;
      }
      input {
        font-size: 14px;
        padding: 8px;
      }
      input[type='submit'] {
        font-size: 16px;
        padding: 10px;
      }
    }
    .password-container {
      position: relative;
    }
    .password-container input {
      width: 100%;
      padding-right: 40px;
    }
    .password-toggle-icon {
      position: absolute;
      top: 50%;
      right: 10px;
      transform: translateY(-50%);
      cursor: pointer;
    }
  </style>
</head>
<body>
  <img src="/assets/icon.webp" alt="BOXaPOS">
  <div class='container'>
    <header>WiFi Config</header>
    <p>Current IP Address: )rawliteral";

void handleRoot()
{
  String page = FPSTR(htmlPage);
  String ip = (WiFi.getMode() == WIFI_AP || WiFi.getMode() == WIFI_AP_STA)
                  ? WiFi.softAPIP().toString()
                  : WiFi.localIP().toString();
  page += ip;
  page += F("</p>"
            "<form action='/save' method='POST'>"
            "<div class='input-container ssid'><input type='text' name='ssid' required></div>"
            "<div class='input-container password password-container'>"
            "<input type='password' name='password' id='password' required>"
            "<span class='password-toggle-icon' onclick='togglePassword()'></span>"
            "<span class='password-toggle-icon' onclick='togglePassword()'>"
            "<svg id='eye-open' width='24' height='24' viewBox='0 0 24 24' fill='none' xmlns='http://www.w3.org/2000/svg' style='display: none;'>"
            "<path d='M12 4C7 4 3 8 1 12C3 16 7 20 12 20C17 20 21 16 23 12C21 8 17 4 12 4Z' stroke='black' stroke-width='2'/>"
            "<circle cx='12' cy='12' r='3' fill='black'/></svg>"
            "<svg id='eye-closed' width='24' height='24' viewBox='0 0 24 24' fill='none' xmlns='http://www.w3.org/2000/svg'>"
            "<path d='M3 3L21 21M4 12C5.5 9 8.5 6 12 6C15.5 6 18.5 9 20 12C19 14 16 18 12 18C8 18 5 14 4 12Z' stroke='black' stroke-width='2'/>"
            "</svg></span></div>"
            "<div class='input-container ip'><input type='text' name='ip' placeholder='Ex. 192.168.1.100'></div>"
            "<div class='input-container name'><input type='text' name='name' placeholder='Ex. Kiosk1'></div>"
            "<input type='submit' value='Save & Connect'>"
            "</form><script>function togglePassword() {"
            "const passwordField = document.getElementById('password');"
            "const eyeOpen = document.getElementById('eye-open');"
            "const eyeClosed = document.getElementById('eye-closed');"
            "if (passwordField.type === 'password'){passwordField.type = 'text';eyeOpen.style.display = 'inline';eyeClosed.style.display = 'none';}"
            "else{passwordField.type = 'password';eyeOpen.style.display = 'none';eyeClosed.style.display = 'inline';}}"
            "</script></div></body></html>");

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

  server.send(200, "text/html", "<h1>Settings saved!</h1>");
  saveCredentials(tempSSID, tempPASS, tempIP);
  wifiInit();
}

// Lees een batch frames uit LittleFS en update de LED-strip
uint8_t readFrameBatchFromLittleFS(uint16_t receivedFrame)
{
  if (!file)
  {
    // Serial.printf("No file, opening: %s \n\r", fileName);
    file = LittleFS.open(fileName, "r");
    if (!file)
    {
      return 0;
    }
  }
  //  Lees de eerste lijn (tot de newline)
  if (isFirstRead)
  {
    // Lees de eerste regel tot de newline
    String firstLine = file.readStringUntil('\n');
    firstLine.trim(); // Verwijdert extra spaties/tabs aan het begin/einde

    // Zoek de indexen van de tab-tekens
    int splitIndex1 = firstLine.indexOf('\t');
    int splitIndex2 = firstLine.indexOf('\t', splitIndex1 + 1);

    // Controleer of de tab-tekens correct gevonden zijn
    if (splitIndex1 == -1 || splitIndex2 == -1)
    {
      return 0;
    }

    // Haal de waarden eruit
    String tmpFileName = firstLine.substring(0, splitIndex1);
    afterFill = firstLine.substring(splitIndex1 + 1, splitIndex2).toInt();
    loopAmount = firstLine.substring(splitIndex2 + 1).toInt();

    // Converteer de bestandsnaam naar een char array
    strncpy(fileName, tmpFileName.c_str(), sizeof(fileName) - 1);
    fileName[sizeof(fileName) - 1] = '\0';

    // Sla de huidige bestandspostie op als firstLineLength
    firstLineLength = file.position();
    isFirstRead = false;
#ifdef DEBUG
    // Debug output
    Debug.printf("✅ FileName: '%s', AfterFill: %d, LoopAmount: %d, FirstLineLength: %d\n",
                 fileName, afterFill, loopAmount, firstLineLength);
#endif
  }

  int multiplier = receivedFrame * NUM_LEDS * sizeof(LEDFrameData);
  uint16_t offset = multiplier + firstLineLength;
#ifdef DEBUG
  Debug.println((String)offset);
#endif
  if (!file.seek(offset, SeekSet))
  {
#ifdef DEBUG
    Debug.println("Seek fault");
#endif
    closeAnimationFile();
    return 0;
  }
  yield();
  uint16_t duration = 0;
  uint8_t buffer[NUM_LEDS * sizeof(LEDFrameData)];
#ifdef DEBUG
  Debug.printf("📏 File size: %d, Offset: %d, Required: %d\n", file.size(), offset, offset + NUM_LEDS * sizeof(LEDFrameData));
#endif
  if (offset + NUM_LEDS * sizeof(LEDFrameData) > file.size())
  {
#ifdef DEBUG
    Debug.println("⚠️ Offset buiten bestandslimiet!");
#endif
    closeAnimationFile();
    return 0;
  }

  if (file.read(buffer, NUM_LEDS * sizeof(LEDFrameData)) != NUM_LEDS * sizeof(LEDFrameData))
  {
// Serial.println("Closing file after read");
#ifdef DEBUG
    Debug.println("Read fault");
#endif
    closeAnimationFile();
    return 0;
  }

  strip.setBrightness(255);
  for (uint8_t i = 0; i < NUM_LEDS; i++)
  {
    yield();
    uint16_t index = i * sizeof(LEDFrameData);
    if ((unsigned int)(index + 3) >= NUM_LEDS * sizeof(LEDFrameData))
    {
      Serial.println("⚠️ Index out of bounds!");
      break;
    }

    // Pas intensiteit toe bij het instellen van de kleur
    strip.setPixelColor(i, strip.Color(
                               ((buffer[index] * buffer[index + 3]) >> 8),
                               ((buffer[index + 1] * buffer[index + 3]) >> 8),
                               ((buffer[index + 2] * buffer[index + 3]) >> 8)));
  }
  duration = buffer[4] | (buffer[5] << 8);
  strip.show();
  yield();

  return duration;
}

void setColor(uint8_t r, uint8_t g, uint8_t b, uint8_t intens)
{
  uint32_t color = strip.Color(r, g, b);
  strip.setBrightness(intens);
  strip.fill(color, 0, NUM_LEDS);

  strip.show();
}

// Receive and send UDP packet
void handleUdp()
{
  // Verwerk inkomende UDP-pakketten
  char incomingPacket[255];
  int packetSize = udp.parsePacket();
  if (packetSize)
  {
    int len = udp.read(incomingPacket, 255);
    if (len > 0)
      incomingPacket[len] = '\0';

    if (strcmp(incomingPacket, "ESP_FIND") == 0)
    {
      IPAddress remote = udp.remoteIP();
      uint16_t port = udp.remotePort();
      if (remote && port > 0)
      {
        String response = "ESP_FOUND " +
                          (WiFi.getMode() == WIFI_AP ? WiFi.softAPIP().toString() : WiFi.localIP().toString()) + " " + genericSSID;

        udp.beginPacket(remote, port);
        udp.write(response.c_str());
        udp.endPacket();

        isFirst = true;
      }
    }
  }
}

// ---------------------------------------------------------------------------
// Setup en Loop
void setup()
{
  Serial.begin(115200);
#ifdef DEBUG
  Debug.begin(115200);
  Debug.print("Connecting");
#endif
  wifi_set_sleep_type(NONE_SLEEP_T);

  // Stel genericSSID samen op basis van "BOXaPOS" en chip-ID
  uint32_t chipID = ESP.getChipId();
  char chipIDStr[9];
  snprintf(chipIDStr, sizeof(chipIDStr), "%08X", chipID);
  snprintf(genericSSID, sizeof(genericSSID), "%s%s", "BOXaPOS", chipIDStr);

  // Mount LittleFS
  if (!LittleFS.begin())
  {
    return;
  }

  // Start WiFi
  wifiInit();
  delay(4000);
  // Webserver: Serve static files and define routes
  server.serveStatic("/assets/icon.webp", LittleFS, "/assets/icon.webp");
  server.serveStatic("/assets/icon.ico", LittleFS, "/assets/icon.ico");
  server.on("/", handleRoot);
  server.on("/save", HTTP_POST, handleSave);
  server.begin();

  // Start OTA
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
    uint8_t splitIndex1 = firstLine.indexOf('\t');
    uint8_t splitIndex2 = firstLine.indexOf('\t', splitIndex1 + 1);
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
      closeAnimationFile();
      LittleFS.remove(fileName);
    }
    // Stap 3: Open bestand en schrijf binaire data
    closeAnimationFile();
    file = LittleFS.open(fileName, "a");
    if (!file) {
      return;
    }
    file.printf("%s\t%d\t%d\n",fileName, afterFill, loopAmount);
    while (packet.get_remaining_size() >= 6) {
      
      uint8_t buffer[6];
      packet.read(buffer, 6);
      file.write(buffer, 6);
    }
    closeAnimationFile(); });

  mqtt.subscribe("display/#", [](const char *topic, const char *payload)
                 {
    if (!strcmp(topic, "display/color")) {
      mqttProgram = 3;
      // Gebruik tijdelijk een String voor het splitsen
      String color = payload;
      uint8_t comma1 = color.indexOf(',');
      uint8_t comma2 = color.lastIndexOf(',');
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
      isFirst = true;
      boot = true;
      mqttProgram = 0;
    }
    /*
    if (!strcmp(topic, "display/getIP")) {
      IPAddress ip = WiFi.localIP();
      String ipString = ip.toString();
      mqtt.publish("display/returnIP", ipString.c_str());
    } 
    */ });

  mqtt.begin();
  strip.begin();
  strip.show(); // Zet de LED-strip uit (initialiseer)
  udp.begin(udpPort);
  lastRainbow = millis();
  lastDisplayed = millis();
#ifdef DEBUG
  telnetServer.begin();
#endif
}

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
  // Verwerk OTA-updates en MQTT
  ArduinoOTA.handle();
  mqtt.loop();
  server.handleClient();

  // Wifi reconnect enkel in STA
  if ((WiFi.getMode() == WIFI_STA && WiFi.status() != WL_CONNECTED) && (loadExtPASS().length() > 0 && loadExtSSID().length() > 0))
  {
    static unsigned long lastTry = 0;
    if (millis() - lastTry > 1000)
    {
      lastTry = millis();
      Serial.println("Trying to reconnect");
      wifiInit();
    }
  }
  handleUdp();

  // LED Animaties
  if (mqttProgram == 0)
  {
    if (WiFi.getMode() == WIFI_AP && boot)
    {
      Serial.println("AP mode");
      setColor(39, 169, 201, 64);
      boot = false;
    }

    if (WiFi.getMode() == WIFI_STA && WiFi.status() == WL_CONNECTED)
    {
      boot = true;
      if (isFirst)
      {
        fileCount = 0;
        closeAnimationFile();
        file = LittleFS.open("/default.dat", "r");
        yield();
        if (!file)
        {
          return;
        }
        fileCount = file.size() / (NUM_LEDS * sizeof(LEDFrameData));
#ifdef DEBUG
        Debug.println((String)fileCount);
#endif
        isFirst = false;
        isFirstRead = true;
      }
      if (millis() - lastDisplayed >= waitTime)
      {
        if (frameCount >= fileCount)
        {
          frameCount = 0;
          loopCounter++;
        }
        waitTime = readFrameBatchFromLittleFS(frameCount);
        yield();
        frameCount++;
        lastDisplayed = millis();
      }
    }
  }

  if (mqttProgram == 2)
  {
    if (isFirst)
    {
      fileCount = 0;
      String tmp = "/" + String(animationName) + ".dat";
      strncpy(fileName, tmp.c_str(), sizeof(fileName) - 1);
      fileName[sizeof(fileName) - 1] = '\0';
      // Serial.println(fileName);
      closeAnimationFile();
      file = LittleFS.open(fileName, "r");
      isFirst = false;
      isFirstRead = true;
      if (!file)
      {
        return;
      }
      fileCount = file.size() / (NUM_LEDS * sizeof(LEDFrameData));
#ifdef DEBUG
      Debug.println((String)file.size());
#endif
    }

    if (millis() - lastDisplayed >= waitTime && file)
    {
      if (frameCount >= fileCount)
      {
        frameCount = 0;
        loopCounter++;
      }
      if (loopAmount == 0 || loopCounter < loopAmount)
      {
        // Serial.printf("Displaying file: %s\n\r", fileName);
        waitTime = readFrameBatchFromLittleFS(frameCount);
        yield();
        frameCount++;
      }
      else if (afterFill)
      {
        readFrameBatchFromLittleFS(frameCount - 1);
      }

      else
      {
        strip.clear();
        strip.show();
      }
      lastDisplayed = millis();
    }
  }
}
