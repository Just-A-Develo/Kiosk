#ifndef LEDFRAMERAM_H
#define LEDFRAMERAM_H

#include <LEDFrames.h>
#include <Adafruit_NeoPixel.h>
#include <FS.h>
#include <LittleFS.h>


#define LED_PIN 4
#define NUM_LEDS 69

class LEDFrameRAM
{
public:
    void init(); // Method to handle frame transitions
    void showDefaultSetup(Adafruit_NeoPixel &strip); // Method to handle frame transitions
    void swapFrames(); // Swap two LED frames
    void displayFrame(int frameIndex, Adafruit_NeoPixel &strip); // Display a frame on LEDs
    void loadFrame(int frameIndex); // Display a frame on LEDs
};

#endif
