#include "../include/videoDriver.h"
#include "../include/font.h"
#include <stdint.h>
#include "../include/defs.h"
#include <string.h>

typedef struct vbe_mode_info_structure{
    uint16_t attributes;
    uint8_t window_a;
    uint8_t window_b;
    uint16_t granularity;
    uint16_t window_size;
    uint16_t segment_a;
    uint16_t segment_b;
    uint32_t win_func_ptr;
    uint16_t pitch;
    uint16_t width;
    uint16_t height;
    uint8_t w_char;
    uint8_t y_char;
    uint8_t planes;
    uint8_t bpp;
    uint8_t banks;
    uint8_t memory_model;
    uint8_t bank_size;
    uint8_t image_pages;
    uint8_t reserved0;
    uint8_t red_mask;
    uint8_t red_position;
    uint8_t green_mask;
    uint8_t green_position;
    uint8_t blue_mask;
    uint8_t blue_position;
    uint8_t reserved_mask;
    uint8_t reserved_position;
    uint8_t direct_color_attributes;
    uint32_t framebuffer;
    uint32_t off_screen_mem_off;
    uint16_t off_screen_mem_size;
    uint8_t reserved1[206];
} __attribute__ ((packed)) vbe_mode_info_t;

static vbe_mode_info_t * vbe_mode_info = (vbe_mode_info_t *) 0x5C00;

static uint64_t currentX = 0;
static uint64_t currentY = 0;
static const int bgColor = 0x000000;            // Negro
static uint64_t defaultTextSize = TEXT_SIZE;    // Tamaño por defecto
static void updateCursor(void);
static void moveRight(void);
static void drawChar(uint32_t x, uint32_t y, uint8_t c, uint32_t color, uint64_t size);
static void v_fillRectangle(uint64_t x0, uint64_t y0, uint64_t x1, uint64_t y1, uint32_t color);
static void drawFilledRect(uint32_t x, uint32_t y, uint32_t width, uint32_t height, uint32_t color);

// Ancho en píxeles del modo actual
uint16_t getScreenWidth(void){ 
    return vbe_mode_info->width;
}

// Alto en píxeles del modo actual
uint16_t getScreenHeight(void){
    return vbe_mode_info->height;
}

// Permite ajustar el tamaño por defecto usado por putChar
void setDefaultTextSize(uint64_t size){ 
    defaultTextSize = (size == 0) ? (1) : (size);
}

uint64_t getDefaultTextSize(void){
    return defaultTextSize;
}

int validPosition(uint64_t x,  uint64_t y){
    return x < vbe_mode_info->width && y < vbe_mode_info->height;
}

/* FUNCIONES DE MODO TEXTO. */

void increaseFontSize(void){
    if (defaultTextSize < MAX_FONT_SIZE){
        defaultTextSize++;
    }

    updateCursor();
}

void decreaseFontSize(void){
    if(defaultTextSize > 1){
        defaultTextSize--;
    }
    updateCursor();
}

// Dibuja un pixel validando límites y BPP
void putPixel(uint32_t hexColor, uint64_t x, uint64_t y){
    if(!validPosition(x, y)){
        return;
    }

    uint8_t *framebuffer = (uint8_t *)(uintptr_t)vbe_mode_info->framebuffer;
    uint32_t bpp = vbe_mode_info->bpp;

    uint32_t bytesPerPixel = (bpp + 7) / 8;
    uint64_t offset = y * (uint64_t)vbe_mode_info->pitch + x * bytesPerPixel;

    for(uint32_t i = 0; i < bytesPerPixel; i++){
        framebuffer[offset + i] = (hexColor >> (8 * i)) & 0xFF;
    }
}

// Desplaza la pantalla una línea hacia arriba
void scroll(void){
    uint64_t lineHeight = defaultTextSize * FONT_HEIGHT;
    uint8_t * framebuffer = (uint8_t *)(uintptr_t) vbe_mode_info->framebuffer;
    
    for(uint64_t srcY = lineHeight; srcY < vbe_mode_info->height; srcY++){
        uint64_t dstY = srcY - lineHeight;
        uint64_t srcOffset = srcY * vbe_mode_info->pitch;
        uint64_t dstOffset = dstY * vbe_mode_info->pitch;
        memcpy(framebuffer + dstOffset, framebuffer + srcOffset, vbe_mode_info->pitch);
    }

    uint64_t lastLineStart = vbe_mode_info->height - lineHeight;

    v_fillRectangle(0, lastLineStart, vbe_mode_info->width, vbe_mode_info->height, bgColor);
}

// Salta a la línea siguiente con scroll automático
void newLine(void){
    currentX = 0;

    uint64_t stepY = (uint64_t)defaultTextSize * FONT_HEIGHT;

    if(currentY + stepY < vbe_mode_info->height){

        currentY += stepY;
    v_fillRectangle(0, currentY, vbe_mode_info->width, currentY + stepY, bgColor);

    } else{

        scroll();
        currentY = vbe_mode_info->height - stepY;
    }
}

// Imprime un carácter en modo gráfico con soporte para \n y \b
void videoPutChar(uint8_t c, uint32_t color){
    if(c == '\n' || c == '\r'){ 
        newLine();
        return;
    }

    
    if(c == '\b'){
    uint64_t stepX = (uint64_t)FONT_WIDTH * defaultTextSize;
    uint64_t stepY = (uint64_t)FONT_HEIGHT * defaultTextSize;
        if(currentX >= stepX){
            currentX -= stepX;
            v_fillRectangle(currentX, currentY, currentX + stepX, currentY + stepY, bgColor);
            updateCursor();
        }
        return;
    }

    drawChar((uint32_t)currentX, currentY, c, color, defaultTextSize);
    moveRight();
    updateCursor();
}

// Imprime una cadena completa en color
void videoPrint(const char *str, uint32_t color){
    if(str == 0){
        return;
    }
    
    for(unsigned int i = 0; str[i] != '\0'; i++){
        videoPutChar((uint8_t)str[i], color);
    }
}

// Avanza el cursor horizontalmente con wrap
static void moveRight(){
    uint64_t stepX = (uint64_t)FONT_WIDTH * defaultTextSize;
    if(currentX + stepX < vbe_mode_info->width){
        currentX += stepX;
    } else{
        newLine();
    }
}

// Asegura que el cursor esté en una posición válida
static void updateCursor(){
    if(!validPosition(currentX + (uint64_t)FONT_WIDTH * defaultTextSize - 1, currentY + (uint64_t)FONT_HEIGHT * defaultTextSize - 1)){
        newLine();
    }
}

// Imprime una cadena en (x,y) con tamaño escalado
void setTextSize(uint8_t size){
    if(size == 0){
        size = 1;
    }

    defaultTextSize = size;
}


/* FUNCIONES DE MODO GRAFICO */

// Rellena un rectángulo [x0,y0) x [x1,y1)
static void v_fillRectangle(uint64_t x0, uint64_t y0, uint64_t x1, uint64_t y1, uint32_t color) {
    if (x1 <= x0 || y1 <= y0){
        return;
    }

    if(!validPosition(x0, y0)){
        return;
    }
    
    if (x1 > vbe_mode_info->width) x1 = vbe_mode_info->width;
    if (y1 > vbe_mode_info->height) y1 = vbe_mode_info->height;

    uint32_t bytes_per_pixel = (vbe_mode_info->bpp + 7) / 8;
    uint8_t *framebuffer = (uint8_t *)(uintptr_t)vbe_mode_info->framebuffer;
    uint64_t pitch = vbe_mode_info->pitch;

    for(uint64_t y = y0; y < y1; y++){
        uint64_t row_offset = y * pitch + x0 * bytes_per_pixel;
        
        for(uint64_t x = x0; x < x1; x++){
            uint64_t off = row_offset + (x - x0) * bytes_per_pixel;
            
            for(uint32_t b = 0; b < bytes_per_pixel; b++){
                framebuffer[off + b] = (color >> (8 * b)) & 0xFF;
            }

        }
    }
}

// Rellena un rectángulo usando ancho/alto
static void drawFilledRect(uint32_t x, uint32_t y, uint32_t width, uint32_t height, uint32_t color){
    v_fillRectangle(x, y, (uint64_t)x + width, (uint64_t)y + height, color);
}

// Dibuja un carácter usando la bitmap de la fuente
static void drawChar(uint32_t x, uint32_t y, uint8_t c, uint32_t color, uint64_t size){
    if (c >= 128){
        return;
    }

    for(int i = 0; i < FONT_HEIGHT; i++){

        uint8_t line = font[c][i];

        for(int j = 0; j < FONT_WIDTH; j++){

            if((line << j) & 0x80){

                for(uint64_t dy = 0; dy < size; dy++){

                    for(uint64_t dx = 0; dx < size; dx++){

                        putPixel(color, x + (uint64_t)j * size + dx, y + (uint64_t)i * size + dy);

                    }
                }
            }
        }
    }
}

// Limpia toda la pantalla y resetea el cursor
void clearScreen(uint32_t color){
    drawFilledRect(0, 0, vbe_mode_info->width, vbe_mode_info->height, color);
    currentX = 0;
    currentY = 0;
}

void fillRect(uint64_t x, uint64_t y, uint64_t w, uint64_t h, uint32_t color){
    if(w == 0 || h == 0){
        return;
    }
    v_fillRectangle(x, y, x + w, y + h, color);
}