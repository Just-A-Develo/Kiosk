#include <Arduino.h>
#include <PicoMQTT.h>
#include "FS.h"
#include <LittleFS.h>
#include <ArduinoOTA.h>
#include <LEDFrameRAM.h>
LEDFrameRAM ledRAM;

#include <ESP8266WebServer.h>

ESP8266WebServer server(80);
char ssid[32] = "";
char password[64] = "";

void saveCredentials(const char *ssid, const char *password)
{
  if (LittleFS.exists("/ExternalSSID.txt"))
  {
    LittleFS.remove("/ExternalSSID.txt");
  }
  File file = LittleFS.open("/ExternalSSID.txt", "w");
  if (file)
  {
    file.println(ssid);
    file.print(password);
    file.close();
#ifdef DEBUG
    Serial.println("SSID saved to LittleFS.");
#endif
  }
  else
  {
#ifdef DEBUG
    Serial.println("Failed to save SSID.");
#endif
  }
}

void handleRoot()
{
  String page = "<html><head><style>"
                "body { font-family: Arial, sans-serif; background-color: #f0f0f0; margin: 0; padding: 0; }"
                "header { background-color: #4CAF50; padding: 20px; text-align: center; color: white; }"
                "h2 { margin: 0; font-size: 24px; }"
                "p { font-size: 16px; }"
                "form { display: flex; flex-direction: column; align-items: center; margin-top: 20px; }"
                "input[type='text'], input[type='password'] {"
                "  padding: 10px; font-size: 16px; width: 80%; max-width: 400px; margin: 10px 0; border: 1px solid #ccc; border-radius: 4px;"
                "}"
                "input[type='submit'] {"
                "  background-color: #4CAF50; color: white; font-size: 18px; padding: 12px 20px; border: none; border-radius: 4px; cursor: pointer;"
                "  width: 80%; max-width: 400px; margin: 10px 0;"
                "}"
                "input[type='submit']:hover { background-color: #45a049; }"
                "</style></head><body>"
                "<header><h2>WiFi Config</h2></header>"
                "<div style='text-align: center; margin-top: 20px;'>"
                "<p>ESP IP Address: " +
                WiFi.localIP().toString() + "</p>"
                                            "<form action='/save' method='POST'>"
                                            "<label for='ssid'>SSID:</label><input type='text' name='ssid' required><br>"
                                            "<label for='password'>Password:</label><input type='password' name='password' required><br>"
                                            "<input type='submit' value='Save & Connect'></form>"
                                            "</div></body></html>";

  server.send(200, "text/html", page);
}

void handleSave()
{
  if (server.hasArg("ssid") && server.hasArg("password"))
  {
    String newSSID = server.arg("ssid");
    String newPassword = server.arg("password");
    newSSID.toCharArray(ssid, 32);
    newPassword.toCharArray(password, 64);
    saveCredentials(ssid, password);
    server.send(200, "text/html", "<h3>Saved! Rebooting...</h3>");
    delay(2000);
    ESP.restart();
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
  String password = file.readString();
  file.close();
  password.trim();
  return password;
}

#include <Adafruit_NeoPixel.h>

// #define DEBUG // comment out to dissable Serial prints

#define LED_PIN 4
#define NUM_LEDS 100

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
    Serial.println("No data to save.");
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
    Serial.println("Failed to open file for writing");
#endif
  }
}

int readFrameBatchFromLittleFS(int fileIndex)
{
  // Set file name to call
  String filename = "/frame_" + String(fileIndex) + ".dat";
  File file = LittleFS.open(filename, "r"); // open file in read-only

  if (file)
  {
#ifdef DEBUG
    Serial.println("File opened successfully!");
#endif

    // Calculate file size
    size_t fileSize = file.size();

    // Confirm if file is a multiple of LEDFrameData (To confirm correct format)
    if (fileSize % sizeof(LEDFrameData) != 0)
    {
#ifdef DEBUG
      Serial.println("Invalid file size. It might not contain valid frame data.");
#endif
      file.close();
      return 0;
    }

    // Calculate frame count
    size_t numFrames = fileSize / sizeof(LEDFrameData);

    // Make a buffer to safe frames
    LEDFrameData *frameDataBuffer = new LEDFrameData[numFrames];

    // Read data
    file.read((uint8_t *)frameDataBuffer, fileSize);
    file.close();

    // Print data to ledstrip
    for (size_t i = 0; i < numFrames; i++)
    {
      strip.setPixelColor(i, strip.Color(frameDataBuffer[i].r, frameDataBuffer[i].g, frameDataBuffer[i].b));
    }
    waitTime = frameDataBuffer[0].delayMs;
    strip.setBrightness(255);
    strip.show();
    // Delete buffer
    delete[] frameDataBuffer;
  }
  else
  {
#ifdef DEBUG
    Serial.printf("Failed to open the file=: %s\n\r", filename.c_str());
#endif
    return 0;
  }
  return waitTime;
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
    Serial.println("SSID saved to LittleFS.");
#endif
  }
  else
  {
#ifdef DEBUG
    Serial.println("Failed to save SSID.");
#endif
  }
}

void apSetup()
{
  String apSSID = loadSSIDFromFS(); // Try to load saved SSID

  if (apSSID == "") // NO saved SSID? Generate one
  {
    Serial.println("No SSID found, generating a new one.");
    srand(analogRead(A0));
    int randomnum = rand();
    apSSID = "KioskLed" + String(randomnum);
    saveSSIDToFS(apSSID); // Save for next boot
  }
  else
  {
    Serial.println("SSID loaded from FS: " + apSSID);
  }
  String apPassword = "KioskLed";
  WiFi.softAP(apSSID.c_str(), apPassword);
  Serial.println("Access Point Started");
  Serial.println(apSSID);
  Serial.print("AP IP Address: ");
  Serial.println(WiFi.softAPIP());
}

void setup()
{
  // Start LittleFS
  if (!LittleFS.begin())
  {
#ifdef DEBUG
    Serial.println("LittleFS mount failed");
#endif
    return;
  }

  // Usual setup
  Serial.begin(115200);
  Serial.print("Connecting");
  // AP
  WiFi.setOutputPower(16.0);
  WiFi.mode(WIFI_AP_STA);
  // Check if a local wifi credential is saved
  String storedSSID = loadExtSSID();
  String storedPASS = loadExtPASS();
  storedSSID.toCharArray(ssid, 32);
  storedPASS.toCharArray(password, 64);

  if (strlen(ssid) > 0)
  {
    WiFi.begin(ssid, password);
    Serial.print("Connecting to WiFi");
    int retries = 0;
    while (WiFi.status() != WL_CONNECTED && retries < 20)
    {
      delay(500);
      Serial.print(".");
      retries++;
    }
    Serial.println();
    if (WiFi.status() == WL_CONNECTED)
    {
      Serial.println("Connected to WiFi!");
      Serial.print("IP Address: ");
      Serial.println(WiFi.localIP());
    }
  }
  apSetup();

  ArduinoOTA.begin();

  // Subscribe to a topic pattern and attach a callback
  mqtt.subscribe("update/#", [](const char *topic, PicoMQTT::IncomingPacket &packet)
                 {
#ifdef DEBUG
    Serial.printf("Received message in topic '%s'\n", topic);
#endif
    std::map<uint8_t, std::vector<LEDFrameData>> frameGroups;

    while (packet.get_remaining_size() >= 8) { 
        uint8_t data[6];
        uint16_t delayMS;
        packet.read(data, 6);
        uint8_t byte1 = packet.read();
        uint8_t byte2 = packet.read();
        delayMS = byte1 | (byte2 << 8);

        LEDFrameData frameData;
        frameData.frameIndex = data[0];
        frameData.ledIndex = data[1];
        frameData.r = data[2];
        frameData.g = data[3];
        frameData.b = data[4];
        frameData.intensity = data[5];
        frameData.delayMs = delayMS;

        frameGroups[frameData.frameIndex].push_back(frameData);
    }

    for (auto &entry : frameGroups) {
        uint8_t frameIndex = entry.first;
        auto &frames = entry.second;
        String filename = "/frame_" + String(frameIndex) + ".dat";

        if (LittleFS.exists(filename)) {
            LittleFS.remove(filename);
        }

        File file = LittleFS.open(filename, "w");
        if (file) {
            file.write((uint8_t *)frames.data(), frames.size() * sizeof(LEDFrameData));
            file.close();
        }
#ifdef DEBUG
        Serial.printf("Saved %d frames to %s\n", frames.size(), filename.c_str());
        FSInfo fs_info;
        LittleFS.info(fs_info);
        Serial.printf("LittleFS Total: %d bytes\n", fs_info.totalBytes);
        Serial.printf("LittleFS Used: %d bytes\n", fs_info.usedBytes);
        Serial.printf("LittleFS Free: %d bytes\n", fs_info.totalBytes - fs_info.usedBytes);
#endif
    } });

  mqtt.subscribe("display/#", [](const char *topic, const char *payload)
                 {
#ifdef DEBUG       
    Serial.printf("Received message in topic '%s'\n", topic);
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
      Serial.printf("Received: r = %d, g = %d, b = %d, level = %d", r,g,b,level);
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
      Serial.println("Received order: " + order);  // Print de ontvangen string
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
          Serial.print("Order " + String(i) + ": " + String(orderArray[i]) + "\n");
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
      
    } 
    if (!strcmp(topic, "display/default"))
    {
      mqttProgram = 0;
    } });

  // Start the broker
  mqtt.begin();
  strip.begin();
  strip.show(); // Initialize the strip to off

  server.on("/", handleRoot);
  server.on("/save", HTTP_POST, handleSave);
  server.begin();
  
  ledRAM.init();
}
int loopCount = 0;
unsigned int lastDisplayed = millis();
int fileCount = 0;
void loop()
{
  mqtt.loop();
  ArduinoOTA.handle();
  server.handleClient();

  if (!frameBuffer.empty())
  {
    flushFrameBufferToStorage();
  }
  if (mqttProgram == 0)
  {
    ledRAM.showDefaultSetup(strip);
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
      Serial.println(waitTime);
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
      Serial.println(fileCount);
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
      Serial.println(waitTime);
#endif
      loopCount++;
      lastDisplayed = millis();
    }

    if (mqttProgram == 3)
    {
    }
  }
}
