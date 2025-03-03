#include <LEDFrameRAM.h>

// Global variables
File frameFile;  // Bestandshandle voor animatieframes
LEDFrame frame1[led_count]; // Frame buffer
unsigned long lastFrameTime = 0;
int currentFrame = 0;

void LEDFrameRAM::init()
{
    if (!LittleFS.begin()) {
        Serial.println("LittleFS mount failed!");
        return;
    }
    
    frameFile = LittleFS.open("/defaultAnimation.dat", "r"); // Open binaire frame-data
    if (!frameFile) {
        Serial.println("Kan defaultAnimation.dat niet openen!");
        return;
    }

    Serial.println("LittleFS geladen en bestand geopend.");
    
    // Lees direct het eerste frame
    loadFrame(0);
}

void LEDFrameRAM::loadFrame(int frameIndex) {
    if (!frameFile) return;

    // Bereken offset: frameIndex * frame_size
    int offset = frameIndex * led_count * 8; // 8 bytes per LED
    frameFile.seek(offset, SeekSet);

    // Lees het frame in de buffer
    for (int i = 0; i < led_count; i++) {
        uint8_t buffer[8];
        if (frameFile.read(buffer, 8) != 8) {
            Serial.println("❌ Fout bij lezen van frame!");
            return;
        }

        // Pak de waarden uit
        frame1[i].i = buffer[1];  // LED index
        frame1[i].r = buffer[2];
        frame1[i].g = buffer[3];
        frame1[i].b = buffer[4];
        frame1[i].intensity = buffer[5];
        frame1[i].duration = buffer[6] | (buffer[7] << 8); // Little-endian
    }

    Serial.print("✅ Frame "); Serial.print(frameIndex); Serial.println(" geladen.");
}


void LEDFrameRAM::displayFrame(int currentFrame, Adafruit_NeoPixel &strip)
{
    LEDFrame led;
    int i = 0;
    for (i = 0; i < led_count; i++)
    {
        led = frame1[i];  // Use reference to avoid unnecessary copies
        strip.setPixelColor(i, strip.Color(led.r, led.g, led.b));
    }
    strip.setPixelColor(68, strip.Color(frame1[67].r, frame1[67].g, frame1[67].b));
    strip.setPixelColor(69, strip.Color(frame1[67].r, frame1[67].g, frame1[67].b));
    strip.setBrightness(frame1[0].intensity);
    strip.show(); // Only one show() at the end
    yield();  // Yield after frame update
}

void LEDFrameRAM::showDefaultSetup(Adafruit_NeoPixel &strip)
{
    unsigned long currentTime = millis();
    
    if (currentTime - lastFrameTime >= frame1[0].duration)
    {
        lastFrameTime = currentTime;
        displayFrame(currentFrame, strip);
        yield(); // WiFi blijft responsief

        // Ga naar volgend frame
        currentFrame = (currentFrame + 1) % frame_count;
        loadFrame(currentFrame);  // Haal het volgende frame uit LittleFS
    }
}
