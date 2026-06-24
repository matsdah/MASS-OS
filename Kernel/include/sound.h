#ifndef SOUND_H
#define SOUND_H

#include <stdint.h>

#define PIT_BASE_HZ 1193180
#define PC_SPEAKER_PORT 0x61
#define PIT_CHANNEL2_DATA_PORT 0x42
#define PIT_CONTROL_PORT 0x43
#define SPEAKER_OFF_MASK 0xFC
#define PIT_SQUARE_WAVE_MODE 0xB6
#define SPEAKER_ENABLE_BITS 3

void startSpeaker(uint32_t freq);
void beep(uint32_t freq, uint64_t time);
void turnOff(void);

#endif