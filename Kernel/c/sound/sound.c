#include <stdint.h>
#include "sound.h"
#include "time.h"

extern uint8_t inb(uint16_t port);
extern void outb(uint16_t port, uint8_t data);

// Apaga el parlante de PC
void turnOff(void){
    uint8_t tmp = inb(PC_SPEAKER_PORT) & SPEAKER_OFF_MASK;
    outb(PC_SPEAKER_PORT, tmp);
}

// Configura PIT Ch2 y habilita parlante a 'freq' Hz (0 apaga)
void startSpeaker(uint32_t freq){
    if(freq == 0){
        turnOff();
        return;
    }

    uint32_t div = PIT_BASE_HZ / freq;
    
    outb(PIT_CONTROL_PORT, PIT_SQUARE_WAVE_MODE); 
    outb(PIT_CHANNEL2_DATA_PORT, (uint8_t)(div & 0xFF));
    outb(PIT_CHANNEL2_DATA_PORT, (uint8_t)(div >> 8));
    uint8_t tmp = inb(PC_SPEAKER_PORT);

    if((tmp & 3) != 3){
        outb(PC_SPEAKER_PORT, tmp | SPEAKER_ENABLE_BITS);
    }

    return;
}

// Beep bloqueante: suena 'time' ticks y apaga
void beep(uint32_t freq, uint64_t time){
    startSpeaker(freq);
    sleep(time);
    turnOff();
}