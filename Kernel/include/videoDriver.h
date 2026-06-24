#ifndef VIDEODRIVER_H
#define VIDEODRIVER_H

#include <stdint.h>

uint16_t getScreenWidth(void);
uint16_t getScreenHeight(void);
int validPosition(uint64_t x, uint64_t y);
void setDefaultTextSize(uint64_t size);
uint64_t getDefaultTextSize(void);
void setTextSize(uint8_t size);
void increaseFontSize(void);
void decreaseFontSize(void);
void putPixel(uint32_t hexColor, uint64_t x, uint64_t y);
void fillRect(uint64_t x, uint64_t y, uint64_t w, uint64_t h, uint32_t color);
void videoPutChar(uint8_t c, uint32_t color);
void videoPrint(const char *str, uint32_t color);
void newLine(void);
void scroll(void);
void clearScreen(uint32_t color);

#endif