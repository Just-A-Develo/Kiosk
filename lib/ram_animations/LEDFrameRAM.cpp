#include <LEDFrameRAM.h>

// Global variables
LEDFrame frame1[led_count]; // Only one frame buffer now
unsigned long lastFrameTime = 0;
int currentFrame = 0;

void LEDFrameRAM::init()
{
    // Load the first frame directly into frame1
    memcpy_P(frame1, &frames[currentFrame], sizeof(LEDFrame) * led_count);
}

void LEDFrameRAM::displayFrame(int currentFrame, Adafruit_NeoPixel &strip)
{
    int i = 0;
    LEDFrame led;
    for (i = 0; i < led_count; i++)
    {
        led = frame1[i];
        strip.setPixelColor(i, strip.Color(led.r, led.g, led.b));
        yield();
    }
    strip.setPixelColor(i + 1, strip.Color(led.r, led.g, led.b));
    strip.setBrightness(led.intensity);
    yield();
    strip.show();
    Serial.print("Showing frame: ");
    Serial.println(currentFrame);
}

void LEDFrameRAM::showDefaultSetup(Adafruit_NeoPixel &strip)
{
    unsigned long currentTime = millis();
    if (frames[currentFrame] == nullptr)
    {
        Serial.println("Error: Null frame data!");
        return;
    }
    if (currentTime - lastFrameTime >= frame1[0].duration)
    {
        Serial.println(frame1[0].duration);
        lastFrameTime = currentTime;
        yield();
        displayFrame(currentFrame, strip);

        // Load the next frame directly into frame1 from PROGMEM
        currentFrame = (currentFrame + 1) % frame_count;
        for (int i = 0; i < led_count; i++)
        {
            frame1[i].i = pgm_read_byte(&frames[currentFrame][i].i);
            frame1[i].r = pgm_read_byte(&frames[currentFrame][i].r);
            frame1[i].g = pgm_read_byte(&frames[currentFrame][i].g);
            frame1[i].b = pgm_read_byte(&frames[currentFrame][i].b);
            frame1[i].intensity = pgm_read_byte(&frames[currentFrame][i].intensity);
            yield(); // Allow background processing
        }
    }
}
