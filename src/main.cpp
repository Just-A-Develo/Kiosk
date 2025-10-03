#include <LittleFS.h>
#include <PicoMQTT.h>
#include <ArduinoOTA.h>
#include <ESP8266WiFi.h>
#include <Adafruit_NeoPixel.h>
#include <ESPAsyncWebServer.h>
#include <ESPAsyncTCP.h>
#include <WiFiudp.h>

extern "C"
{
#include "user_interface.h"
}

// Save last entry
bool animation = false;
bool color = false;

#define DEBUG
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
    char buf[128];
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

// Global buffers
char genericSSID[32];        // Is made in settup
char storedSSID[32] = "";    // Wifi SSID
char storedPASS[32] = "";    // Wifi password
char fileName[32] = "";      // File for animations
char animationName[32] = ""; // Name of animation (without path)
String ssid = "";
String password = "";

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
// #define NUM_LEDS 78
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
AsyncWebServer server(80);
const char *apPassword = "BOXaPOS1"; // AP password

// MQTT
PicoMQTT::Server mqtt;
bool isFirst = true;
bool isFirstRead = true;
unsigned int waitTime = 0;
uint8_t r, g, b = 255;
uint8_t level = 255;
uint8_t mqttProgram = 0;
uint8_t frameCount = 0;

unsigned long lastReconnectAttempt = 0;
const unsigned long RECONNECT_INTERVAL = 500;
bool reconnecting = false;
uint8_t attempt = 0;

// ---------------------------------------------------------------------------
// Functions for file handling
void closeAnimationFile()
{
  if (file)
  {
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
  ssid = file.readStringUntil('\n');

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
  file.readStringUntil('\n'); // Skip SSID

  password = file.readStringUntil('\n');
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
  file.readStringUntil('\n'); // Skip SSID
  file.readStringUntil('\n'); // Skip PASS

  String ip = file.readString();
  closeAnimationFile();
  ip.trim();
  return ip;
}

// ---------------------------------------------------------------------------
// AP and WiFi setup
bool apSetup()
{
  // Ensure all is dissabled
  boot = true;
  isFirst = true;
  WiFi.mode(WIFI_AP);
  WiFi.setOutputPower(20.5);
  WiFi.softAPConfig(IPAddress(192, 168, 4, 1), IPAddress(192, 168, 4, 1), IPAddress(255, 255, 255, 0));
  return WiFi.softAP(genericSSID, apPassword, 6, 0, 8, 100);
}

void wifiInit()
{
  // Get saved SSID and password and copy to global buffer
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

  // If saved SSID/password is present, try to connect
  if (storedSSID[0] != '\0' && storedPASS[0] != '\0')
  {
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
      if (apSetup() && WiFi.getMode() != WIFI_AP)
        udp.begin(udpPort);
      return;
    }
    else
      udp.begin(udpPort);
    return;
  }
  else
  {
    if (apSetup() && WiFi.getMode() != WIFI_AP)
      udp.begin(udpPort);
    return;
  }
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
  delay(500);
  WiFi.disconnect(true);
  delay(100);
  ESP.restart();
}

// ---------------------------------------------------------------------------
// HTTP-handle function
// ---------------------------------------------------------------------------
const char htmlPage[] PROGMEM = R"rawliteral(
  <html>

<head>
    <title>WiFi Config</title>
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
        
        .version {
          color: lightgray;
          width: 400px;
          text-align: right;
        }

        .buttons {
          display: flex;
          justify-content: space-between;
          width: 100%;
        }
        
        .input-container input:focus {
            border-color: #27a8c9;
            outline: none;
            box-shadow: 0 0 5px rgba(39, 168, 201, 0.5);
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

        input[type='submit'], button {
            background-color: rgb(39, 169, 201);
            color: white;
            width: 49%;
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

        @keyframes fadeInOut {
            0% {opacity: 0;}
            50% {opacity: 1;}
            100% {
                opacity: 0;
            }
        }

        body img.fade {
            animation: fadeInOut 1s ease-in-out;
        }
    </style>
</head>

<body>
<img class="mainImg" src="/assets/icon.webp" alt="BOXaPOS" onclick="showSSIDAlert()" style="cursor: pointer;">
<div class='container'>
<header>WiFi Config</header>)rawliteral";

void handleRoot(AsyncWebServerRequest *request)
{
  String page = FPSTR(htmlPage);
  String ip = (WiFi.getMode() == WIFI_AP || WiFi.getMode() == WIFI_AP_STA)
                  ? WiFi.softAPIP().toString()
                  : WiFi.localIP().toString();

  // Start with only IP
  page += "<p style='margin-bottom: 0px;'>IP: " + ip + "</p>";

  // Condution

  if (ssid != "" || password != "")
  {
    page += "<p style='margin-bottom: -20px; margin-top: 0px;'>";
    if (ssid != "")
    {
      page += "SSID: " + ssid + "<br>";
    }
    if (password != "")
    {
      page += "Password: " + password + "<br>";
    }
    page += "</p>";
  }

  page += F(R"rawliteral(
<form id="wifiForm">
  <div class='input-container ssid'>
    <input type='text' name='ssid'>
  </div>
  <div class='input-container password password-container'>
    <input type='password' name='password' id='password'>
    <span class='password-toggle-icon' onclick='togglePassword()'>
      <svg id='eye-open' width='24' height='24' viewBox='0 0 24 24' fill='none' xmlns='http://www.w3.org/2000/svg' style='display: none;'>
        <path d='M12 4C7 4 3 8 1 12C3 16 7 20 12 20C17 20 21 16 23 12C21 8 17 4 12 4Z' stroke='black' stroke-width='2'/>
        <circle cx='12' cy='12' r='3' fill='black'/>
      </svg>
      <svg id='eye-closed' width='24' height='24' viewBox='0 0 24 24' fill='none' xmlns='http://www.w3.org/2000/svg'>
        <path d='M3 3L21 21M4 12C5.5 9 8.5 6 12 6C15.5 6 18.5 9 20 12C19 14 16 18 12 18C8 18 5 14 4 12Z' stroke='black' stroke-width='2'/>
      </svg>
    </span>
  </div>
  <div class='input-container ip'>
    <input type='text' name='ip' placeholder='Ex. 192.168.1.100'>
  </div>
  <div class='input-container name'>
    <input type='text' name='name' placeholder='Ex. Kiosk1'>
  </div>
  <div class='buttons'>
    <input type='submit' value='Forget SSID' id='removeBtn'>
    <input type='submit' value='Save & Connect' id='saveBtn'>
  </div>
  </form>
 
<script>
  document.getElementById("wifiForm").addEventListener("submit", function(event) {
    const action = event.submitter.value;
    console.log("Submit action:", action);

    if (action === "Forget SSID") {
      event.preventDefault();

      if (confirm("Are you sure?")) {
        event.preventDefault();
        deleteCredentials();
        alert("SSID removed!");
      } else {
        console.log("Verwijderen geannuleerd");
      }

    } else if (action === "Save & Connect") {
      event.preventDefault();
      showLoadingScreen(event);
    }
  });

  function deleteCredentials() {
    fetch('/forget', { method: 'POST' })
      .then(response => {
        if (response.ok) {
          alert("Credentials removed");
          location.reload(); // of navigeer ergens anders
        } else {
          alert("Credentials removed");
        }
      })
      .catch(error => {
        console.error("Credentials removed:", error);
    });
  }

  function togglePassword() {
    const passwordField = document.getElementById('password');
    const eyeOpen = document.getElementById('eye-open');
    const eyeClosed = document.getElementById('eye-closed');
    if (passwordField.type === 'password') {
      passwordField.type = 'text';
      eyeOpen.style.display = 'inline';
      eyeClosed.style.display = 'none';
    } else {
      passwordField.type = 'password';
      eyeOpen.style.display = 'none';
      eyeClosed.style.display = 'inline';
    }
  }

  function showLoadingScreen(event) {
    event.preventDefault();
    document.querySelector('.container').style.display = 'none';
    document.querySelector('.mainImg').style.display = 'none';
    document.getElementById('loading-screen').style.display = 'flex';

    const form = event.target;
    const formData = new FormData(form);
    
    setTimeout(() => {
        fetch('/save', {
            method: 'POST',
            body: formData
        })
        .then(res => res.ok ? res.text() : Promise.reject('Fault with saving'))
        .then(response => {
            console.log('Server response:', response);
            alert('Settings saved!');  // <-- weer terugzetten
            document.querySelector('.container').style.display = 'block';
            document.querySelector('.mainImg').style.display = 'block';
            document.getElementById('loading-screen').style.display = 'none';
        })
        .catch(error => {
            console.error('Error:', error);
            alert("Settings Saved.");
            document.querySelector('.container').style.display = 'block';
            document.getElementById('loading-screen').style.display = 'none';
        });
    }, 2000);
  }
  function loadExtSSID() {
      return "%SSID%";
  }
  function loadExtPass() {
      return "%PASS%";
  }
  const ssid = '%SSID%'; // ESP fills this in

  window.addEventListener("DOMContentLoaded", () => {
    const removeBtn = document.getElementById("removeBtn");
    const saveBtn = document.getElementById("saveBtn");

    if (ssid.length <= 1) {
      removeBtn.style.display = "none";
      saveBtn.style.flex = "1 1 100%"; // laat deze knop alles vullen
    }else {
      removeBtn.style.display = "";
      saveBtn.style.flex = "";
    }
  });
</script>
</div>
<p class='version'> v2.11 </p>
<div id="loading-screen" style="display:none; flex-direction: column; align-items: center;">
  <img src="/assets/icon.webp" alt="Loading..." class="fade">
  <p style="font-size: 18px;">Connecting to WiFi...</p>
</div>
</body>
</html>)rawliteral");

  page.replace("%SSID%", ssid);
  page.replace("%PASS%", password);

  request->send(200, "text/html", page);
}

// Read a batch of frames from LittleFS and update led strip
uint16_t readFrameBatchFromLittleFS(uint16_t receivedFrame)
{
#ifdef DEBUG
  Debug.println("in read");
#else
  yield();
#endif

  // Open the file only once
  if (!file)
  {
    file = LittleFS.open(fileName, "r");
    if (!file)
      return 0;
  }

  if (isFirstRead)
  {
    // Read the first line (header) and extract metadata
    String firstLine = file.readStringUntil('\n');
    firstLine.trim();

    int splitIndex1 = firstLine.indexOf('\t');
    int splitIndex2 = firstLine.indexOf('\t', splitIndex1 + 1);

    if (splitIndex1 == -1 || splitIndex2 == -1)
    {
      return 0;
    }

    String tmpFileName = firstLine.substring(0, splitIndex1);
    afterFill = firstLine.substring(splitIndex1 + 1, splitIndex2).toInt();
    loopAmount = firstLine.substring(splitIndex2 + 1).toInt();

    strncpy(fileName, tmpFileName.c_str(), sizeof(fileName) - 1);
    fileName[sizeof(fileName) - 1] = '\0';

    firstLineLength = file.position();
    isFirstRead = false;

#ifdef DEBUG
    Debug.printf("✅ FileName: '%s', AfterFill: %d, LoopAmount: %d, FirstLineLength: %d\n",
                 fileName, afterFill, loopAmount, firstLineLength);
#else
    yield();
#endif

    // Jump to the start of the first frame
    file.seek(firstLineLength, SeekSet);
#ifdef DEBUG
    Debug.println("seeking done, only once");
#else
    yield();
#endif
  }

  // Check if we are at or past the end of the file
  if (file.position() >= file.size())
  {
    // Reset back to the start of the frame data
    file.seek(firstLineLength, SeekSet);
  }

  // Duration of the frame (ms)
  uint16_t duration = 0;

  // Chunked read to reduce memory usage
  const uint8_t CHUNK_SIZE = 8;
  uint8_t buffer[CHUNK_SIZE * sizeof(LEDFrameData)];

#ifdef DEBUG
  Debug.println("starting chunk read");
#else
  yield();
#endif

  for (uint16_t i = 0; i < NUM_LEDS; i += CHUNK_SIZE)
  {
    uint8_t toRead = (NUM_LEDS - i) < CHUNK_SIZE ? (NUM_LEDS - i) : CHUNK_SIZE;
    size_t expected = toRead * sizeof(LEDFrameData);

    // Read the next chunk
    if (file.read(buffer, expected) != expected)
    {
      // If read fails, rewind and stop this frame
      file.seek(firstLineLength, SeekSet);
      return 0;
    }

    // Process the chunk
    for (uint8_t j = 0; j < toRead; j++)
    {
#ifdef DEBUG
      Debug.println("starting processing");
#else
      yield();
#endif

      uint16_t idx = j * sizeof(LEDFrameData);

      uint8_t r = (buffer[idx + 0] * buffer[idx + 3]) >> 8;
      uint8_t g = (buffer[idx + 1] * buffer[idx + 3]) >> 8;
      uint8_t b = (buffer[idx + 2] * buffer[idx + 3]) >> 8;
      strip.setPixelColor(i + j, strip.Color(r, g, b));

      // Only read duration once (from first LED of the frame)
      if (i == 0 && j == 0)
      {
        duration = buffer[4] | (buffer[5] << 8);
      }
    }
#ifdef DEBUG
    Debug.println("ended processing");
#else
    yield();
#endif

    // Yield to avoid watchdog reset on ESP8266
    yield();
  }

#ifdef DEBUG
  Debug.println("ended chunk read");
#else
  yield();
#endif

  strip.setBrightness(255);

#ifdef DEBUG
  Debug.println("showing on strip");
#else
  yield();
#endif

  strip.show();

#ifdef DEBUG
  Debug.println("done showing on strip");
#else
  yield();
#endif

  return duration;
}

void saveColorToFile(uint8_t r, uint8_t g, uint8_t b, uint8_t intens)
{
  File f = LittleFS.open("/prevAni.dat", "w");
  if (!f)
  {
    return;
  }
  // Save r, g, b, intens as binairy data
  f.write(r);
  f.write(g);
  f.write(b);
  f.write(intens);
  f.close();
}

bool loadColorFromFile()
{
  if (!LittleFS.exists("/prevAni.dat"))
  {
    return false;
  }
  File f = LittleFS.open("/prevAni.dat", "r");
  if (!f)
  {
    return false;
  }
  if (f.size() < 4)
  {
    f.close();
    return false;
  }
  r = f.read();
  g = f.read();
  b = f.read();
  level = f.read();
  f.close();
  return true;
}

void setColor(uint8_t r, uint8_t g, uint8_t b, uint8_t intens)
{
  color = true;
  animation = false;
  uint32_t color = strip.Color(r, g, b);
  strip.setBrightness(intens);
  strip.fill(color, 0, NUM_LEDS);
  strip.show();
}

// Receive and send UDP packet
void handleUdp()
{
  // Handle incoming UDP
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
                          (WiFi.isConnected() ? WiFi.localIP().toString() : WiFi.softAPIP().toString()) +
                          " " + genericSSID;

        udp.beginPacket(remote, port);
        udp.write(response.c_str());
        udp.endPacket();
      }
    }
  }
}

bool isSavedWifiAvailable(const String &savedSSID)
{
  int n = WiFi.scanNetworks();
  for (int i = 0; i < n; i++)
  {
    String foundSSID = WiFi.SSID(i);
    if (foundSSID == savedSSID)
    {
      return true; // Network available
    }
  }
  return false; // Network not available
}

String processor(const String &var)
{
  if (var == "SSID")
    return ssid;
  if (var == "PASS")
    return password;
  if (var == "IP")
    return (WiFi.getMode() == WIFI_AP || WiFi.getMode() == WIFI_AP_STA) ? WiFi.softAPIP().toString() : WiFi.localIP().toString();
  return String();
}

// ---------------------------------------------------------------------------
// Setup and Loop
void setup()
{
  Serial.begin(115200);

  // Set generic SSID based on BOXaPOS + chip ID
  uint32_t chipID = ESP.getChipId();
  char chipIDStr[9];
  snprintf(chipIDStr, sizeof(chipIDStr), "%08X", chipID);
  snprintf(genericSSID, sizeof(genericSSID), "%s%s", "BOXaPOS", chipIDStr);
  strip.begin();
  // Mount LittleFS
  if (!LittleFS.begin())
  {
    setColor(255, 0, 0, 255);
    return;
  }

  // Start WiFi
  wifiInit();

  delay(4000);
  strip.show();
  // Webserver: Serve static files and define routes
  server.serveStatic("/assets/icon.webp", LittleFS, "/assets/icon.webp");
  server.serveStatic("/assets/icon.ico", LittleFS, "/assets/icon.ico");
  server.on("/", HTTP_GET, handleRoot);

  server.on("/forget", HTTP_POST, [](AsyncWebServerRequest *request)
            {
              request->send(200, "text/plain", "WiFi credentials forgotten");
              closeAnimationFile();
              if (LittleFS.exists("/ExternalSSID.txt")) {
                  File file = LittleFS.open("/ExternalSSID.txt", "w"); // Open for writing (truncates file)
                  if (!file) {
                      return;
                  }
                  // Writing nothing truncates the file
                  file.close();
              } else {
              }
              LittleFS.end();
              ESP.restart(); });

  server.on("/save", HTTP_POST, [](AsyncWebServerRequest *request)
            {
  if (!request->hasParam("ssid", true) || !request->hasParam("password", true)) {
    request->send(400, "text/plain", "Missing SSID or Password");
    return;
  }
  String sSSID = request->getParam("ssid", true)->value();
  String sPassword = request->getParam("password", true)->value();
  String sIP = request->hasParam("ip", true) ? request->getParam("ip", true)->value() : "";
  String sName = request->hasParam("name", true) ? request->getParam("name", true)->value() : "";

  char tempSSID[32], tempPASS[64], tempIP[16], tempName[32];
  strncpy(tempSSID, sSSID.c_str(), sizeof(tempSSID) - 1);
  strncpy(tempPASS, sPassword.c_str(), sizeof(tempPASS) - 1);
  strncpy(tempIP, sIP.c_str(), sizeof(tempIP) - 1);
  strncpy(tempName, sName.c_str(), sizeof(tempName) - 1);
  tempSSID[sizeof(tempSSID) - 1] = '\0';
  tempPASS[sizeof(tempPASS) - 1] = '\0';
  tempIP[sizeof(tempIP) - 1] = '\0';
  tempName[sizeof(tempName) - 1] = '\0';

  // HTML response
  String html = "<h1>Settings saved!</h1>";
  html += "<p><b>SSID:</b> " + String(tempSSID) + "</p>";
  html += "<p><b>Password:</b> " + String(tempPASS) + "</p>";
  html += "<p><b>IP Address:</b> " + String(tempIP) + "</p>";
  html += "<p><b>Name:</b> " + String(tempName) + "</p>";
  request->send(200, "text/html", html);
  
  saveCredentials(tempSSID, tempPASS, tempIP); });
  server.begin();

  // Start OTA
  ArduinoOTA.begin();

  // MQTT-subscriptions
  mqtt.subscribe("update/#", [](const char *topic, PicoMQTT::IncomingPacket &packet)
                 {
    // Step 1: Read first line (filename, afterfill, loopcount)
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
    // Build file name: "/" + fileName + ".dat"
    String tmpFileName = "/" + firstLine.substring(0, splitIndex1) + ".dat";
    // Save value in global variable (after convertion to char)
    strncpy(fileName, tmpFileName.c_str(), sizeof(fileName)-1);
    fileName[sizeof(fileName)-1] = '\0';
    afterFill = (firstLine.substring(splitIndex1 + 1, splitIndex2).toInt() > 0);
    loopAmount = firstLine.substring(splitIndex2 + 1).toInt();

    // Step 2: Remove file if exists
    if (LittleFS.exists(fileName)) {
      closeAnimationFile();
      LittleFS.remove(fileName);
    }
    // Step 3: Open file and write binairy
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
    closeAnimationFile(); 
    // Open the file you just wrote as read only
    File srcFile = LittleFS.open(fileName, "r");
    if (!srcFile) {
      return;
    }

    // Open target file for writing
    File prevFile = LittleFS.open("/prevAni.dat", "w");
    if (!prevFile) {
      srcFile.close();
      return;
    }

    // Copy byte by byte
    const size_t bufSize = 64;
    uint8_t buf[bufSize];
    while (srcFile.available()) {
      size_t bytesRead = srcFile.read(buf, bufSize);
      prevFile.write(buf, bytesRead);
    }

    // Close both files
    srcFile.close();
    prevFile.close(); });

  mqtt.subscribe("display/color", [](const char *topic, const char *payload)
                 {
      mqttProgram = 3;
      // Use temp String vor splitsing
      String color = payload;
      color.trim();                     // Remove whitesapce and new line char
      color.replace(" ", "");          // Remove spaces
      uint8_t comma1 = color.indexOf(',');
      uint8_t comma2 = color.indexOf(',', comma1 + 1);
      uint8_t comma3 = color.lastIndexOf(',');
      r = color.substring(0, comma1).toInt();
      g = color.substring(comma1 + 1, comma2).toInt();
      b = color.substring(comma2 + 1, comma3).toInt();
      level = color.substring(comma3 + 1).toInt();
      saveColorToFile(r,g,b,level);

      setColor(r,g,b,level); });

  mqtt.subscribe("display/displayEeprom", [](const char *topic, const char *payload)
                 {
      isFirst = true;
      mqttProgram = 2;
      loopCounter = 0;
      // Save animation name in a char array
      strncpy(animationName, payload, sizeof(animationName)-1);
      animationName[sizeof(animationName)-1] = '\0'; });

  mqtt.subscribe("display/reset", [](const char *topic, const char *payload)
                 {
      mqttProgram = 9;
      Dir dir = LittleFS.openDir("/");
      while (dir.next()) {
        LittleFS.remove(dir.fileName());
      }
      WiFi.disconnect();
      delay(1000);
      ESP.restart(); });

  mqtt.subscribe("display/resetFrames", [](const char *topic, const char *payload)
                 {
      mqttProgram = 0;
      Dir dir = LittleFS.openDir("/");
      while (dir.next()) {
        String fname = dir.fileName();
        if (fname != "ExternalSSID.txt" && fname != "ssid.txt" && fname != "icon.png") {
          LittleFS.remove(fname);
        }
      } });

  mqtt.subscribe("display/default", [](const char *topic, const char *payload)
                 {
      isFirst = true;
      boot = true;
      mqttProgram = 0; });
  /*
  mqtt.subscribe("display/getIp", [](const char *topic, const char *payload){
    IPAddress ip = WiFi.localIP();
    String ipString = ip.toString();
    mqtt.publish("display/returnIP", ipString.c_str());
  }});
  */

  mqtt.begin();

  setColor(255, 165, 0, 255);
  udp.begin(udpPort);
  delay(4000);
  strip.show();
  delay(1000);
  lastRainbow = millis();
  lastDisplayed = millis();
#ifdef DEBUG
  telnetServer.begin();
#endif
  FSInfo fs_info;
  LittleFS.info(fs_info);
}

static unsigned long lastTry = 0;
static bool isReconnecting = false;
int wifiScanResult = -2; // -2 = not yet scanned

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

  // Handle OTA and MQTT
  ArduinoOTA.handle();
  mqtt.loop();
  handleUdp();

  // === LED Animations ===
  if (mqttProgram == 0)
  {
    if ((WiFi.getMode() == WIFI_AP || WiFi.getMode() == WIFI_AP_STA) && boot)
    {
      if (!animation && color && loadColorFromFile())
      {
        setColor(r, g, b, level);
      }
      else
      {
        setColor(39, 169, 201, 64);
      }
      boot = false;
      isFirst = true;
    }

    if (WiFi.getMode() == WIFI_STA && WiFi.status() == WL_CONNECTED)
    {
      boot = true;

      // First try to open lastanim.dat
      closeAnimationFile();
      file = LittleFS.open("/prevAni.dat", "r");

      if (file.size() > 100)
      {
        fileCount = 0;

        if (!file)
        {
          return;
        }
        fileCount = file.size() / (NUM_LEDS * sizeof(LEDFrameData));
        isFirst = false;
        isFirstRead = true;
      }
      else if (file.size() == 4)
      {
        if (loadColorFromFile())
        {
          setColor(r, g, b, level);
        }
      }

      else
      {
        // If lastanim.dat doesn't exist, use default.dat
        fileCount = 0;
        closeAnimationFile();
        file = LittleFS.open("/default.dat", "r");

        if (!file)
        {
          return;
        }
        fileCount = file.size() / (NUM_LEDS * sizeof(LEDFrameData));
        isFirst = false;
        isFirstRead = true;
      }

      // Play frame logic
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
  }

  if (mqttProgram == 2)
  {
#ifdef DEBUG
    Debug.println("in program 2");
#endif
    if (isFirst)
    {
      animation = true;
      fileCount = 0;
      String tmp = "/" + String(animationName) + ".dat";
      strncpy(fileName, tmp.c_str(), sizeof(fileName) - 1);
      fileName[sizeof(fileName) - 1] = '\0';
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
        waitTime = readFrameBatchFromLittleFS(frameCount);
        waitTime = waitTime + 100;

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
#ifdef DEBUG
    Debug.println("out program 2");
#endif
  }

  // === WiFi reconnect logic ===
  if (ssid.length() > 0 && password.length() > 0 && WiFi.status() != WL_CONNECTED)
  {
#ifdef DEBUG
    Debug.println("wifi reconnect started");
#endif
    if (!isReconnecting)
    {
      WiFi.mode(WIFI_AP_STA);
      WiFi.setOutputPower(20.5);
      WiFi.softAPConfig(IPAddress(192, 168, 4, 1), IPAddress(192, 168, 4, 1), IPAddress(255, 255, 255, 0));
      WiFi.softAP(genericSSID, apPassword, 6, 0, 8, 100);
      mqttProgram = 0;
      isReconnecting = true;
      wifiScanResult = -2;     // Start scanning
      WiFi.scanNetworks(true); // Async scan
    }

    if (wifiScanResult == -2)
    {
      int result = WiFi.scanComplete();
      if (result >= 0)
      {
        wifiScanResult = result;
        bool ssidFound = false;
        for (int i = 0; i < result; i++)
        {
          if (WiFi.SSID(i) == ssid)
          {
            ssidFound = true;
            break;
          }
        }
        WiFi.scanDelete();

        if (ssidFound)
        {
          WiFi.begin(ssid, password);
        }
        else
        {
          wifiScanResult = -1; // Try again later
          lastTry = millis();
        }
      }
    }

    // Periodically start new scan
    if (millis() - lastTry > 10000 && wifiScanResult != -2)
    {
      lastTry = millis();
      WiFi.scanNetworks(true);
      wifiScanResult = -2;
    }
#ifdef DEBUG
    Debug.println("wifi reconnect ended");
#endif
  }
  // After connection: Go to STA only
  if (WiFi.status() == WL_CONNECTED && isReconnecting)
  {
    wifiInit();
    isReconnecting = false;
    mqttProgram = 0;
  }
}