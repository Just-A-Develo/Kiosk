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
    Serial.print("Showing frame: ");
    Serial.println(currentFrame);
    yield();  // Yield after frame update
}

void LEDFrameRAM::showDefaultSetup(Adafruit_NeoPixel &strip)
{
    unsigned long currentTime = millis();
    
    if (currentTime - lastFrameTime >= frame1[0].duration)
    {
        lastFrameTime = currentTime;
        displayFrame(currentFrame, strip);
        yield(); // Allow WiFi processing before loading next frame

        // Load the next frame from PROGMEM in **smaller chunks**
        currentFrame = (currentFrame + 1) % frame_count;
        
        for (int i = 0; i < led_count; i++)
        {
            frame1[i].i = pgm_read_byte(&frames[currentFrame][i].i);
            frame1[i].r = pgm_read_byte(&frames[currentFrame][i].r);
            frame1[i].g = pgm_read_byte(&frames[currentFrame][i].g);
            frame1[i].b = pgm_read_byte(&frames[currentFrame][i].b);
            frame1[i].intensity = pgm_read_byte(&frames[currentFrame][i].intensity);

            if (i % 10 == 0) yield();  // Allow system tasks every 10 LEDs
        }
    }
}