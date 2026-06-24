#include "naiveConsole.h"

static char buffer[64] = { '0' };
static uint8_t * const video = (uint8_t*)0xB8000;
static uint8_t * currentVideo = (uint8_t*)0xB8000;
static const uint32_t width = 80;
static const uint32_t height = 25 ;
static uint8_t * limit = (uint8_t*)0xB8FA0;

// Imprime una cadena en modo texto (usa ncPrintChar)
void ncPrint(const char * string){
	int i;

	for(i = 0; string[i] != 0; i++){
		ncPrintChar(string[i]);
	}
}

// Imprime un caracter con manejo de \n y \b
void ncPrintChar(char character){
	switch (character){
	case '\b':
		currentVideo -= 2;
		*currentVideo = 0;
		break;
	case '\n':
		ncNewline();
		break;
	default:
		*currentVideo = character;
		currentVideo += 2;
		break;
	}

	if(currentVideo < video || currentVideo >= limit){
		currentVideo = video;
	}
}

// Imprime un entero decimal
void ncPrintDec(uint64_t value){
	ncPrintBase(value, 10);
}

// Imprime un entero en hexadecimal
void ncPrintHex(uint64_t value){
	ncPrintBase(value, 16);
}

// Imprime un entero en binario
void ncPrintBin(uint64_t value){
	ncPrintBase(value, 2);
}

void ncPrintBase(uint64_t value, uint32_t base){
    uintToBase(value, buffer, base);
    ncPrint(buffer);
}

void ncClear(void){
	uint32_t i;
	for(i = 0; i < height * width; i++){
		video[i * 2] = ' ';
	}
	currentVideo = video;
}

// Avanza a la siguiente línea respetando el ancho
void ncNewline(void){
	do{
		ncPrintChar(' ');
	}
	while((uint64_t)(currentVideo - video) % (width * 2) != 0);
}

uint32_t uintToBase(uint64_t value, char * buff, uint32_t base){
	char *p = buff;
	char *p1, *p2;
	uint32_t digits = 0;

	do{
		uint32_t remainder = value % base;
		*p++ = (remainder < 10) ? remainder + '0' : remainder + 'A' - 10;
		digits++;
	}

	while (value /= base);

	// Terminate string in buffer.
	*p = 0;

	//Reverse string in buffer.
	p1 = buff; // corregido: invertir el buffer de salida, no el global
	p2 = p - 1;
	while (p1 < p2){
		char tmp = *p1;
		*p1 = *p2;
		*p2 = tmp;
		p1++;
		p2--;
	}

	return digits;
}