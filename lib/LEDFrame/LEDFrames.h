#ifndef LEDFRAMES_H
#define LEDFRAMES_H
#include <stdint.h>


#define frame_count 248
#define led_count 68
#define LED_ARRAY_SIZE (frame_count * led_count * 7)

struct LEDFrame
{
    uint8_t i;
    uint8_t r, g, b;
    uint8_t intensity;
    uint16_t duration;
};

extern LEDFrame frames[frame_count][led_count];

#endif
