#include "../include/exceptions.h"
#include "../include/videoDriver.h"
#include "../include/keyboardDriver.h"
#include "../include/interrupts.h"
#include "../include/naiveConsole.h"
#include <stdint.h>

Exception exceptionsArray[] = 
	{&zeroDivision, 
	0, 
	0, 
	0, 
	0, 
	0, 
	&invalidOpcode};

char * exceptionMessage[] = { "zeroDivision Exception!", "invalidOpcode Exception!"};

// regsArray es llenado por la ISR (asm/interrupts.asm) al ocurrir una excepción

static void printHex64(uint64_t val){
	char buff[32];
	// convertir a hex con padding a 16 dígitos
	char tmp[32];
	uint32_t n = uintToBase(val, tmp, 16);
	uint32_t pad = (n < 16) ? (16 - n) : 0;
	uint32_t k = 0;
	for(uint32_t i = 0; i < pad; i++){
		buff[k++] = '0';
	}

	for(uint32_t i = 0; i < n; i++){
		buff[k++] = tmp[i];
	}
	
	buff[k] = 0;
	videoPrint(buff, 0xFFFFFF);
}

static void printRegistersSnapshot(void){
	const char * regs[] = {
		"RAX: 0x", "RBX: 0x", "RCX: 0x", "RDX: 0x", "RBP: 0x", "RDI: 0x", "RSI: 0x",
		"R8 : 0x", "R9 : 0x", "R10: 0x", "R11: 0x", "R12: 0x", "R13: 0x", "R14: 0x", "R15: 0x",
		"RIP: 0x", "CS : 0x", "RFL: 0x", "RSP: 0x", "SS : 0x"
	};

	for(int i = 0; i < 20; i++){
		videoPrint(regs[i], 0xFFFFFF);
		printHex64(regsArray[i]);
		newLine();
	}
}

void zeroDivision(void){
	exceptionHandler(exceptionMessage[0]);
}

void invalidOpcode(void){
	exceptionHandler(exceptionMessage[1]);
}

// Llama al handler de la excepción si existe
void exceptionDispatcher(int exception){
	Exception ex = exceptionsArray[exception];

	if(ex != 0){
		ex();
	}
}

// Mensaje de error y espera a ENTER para continuar
void exceptionHandler(char * msg){
	newLine();
	videoPrint(msg, 0xFFFFFF);
	newLine();
	videoPrint("Snapshot de registros:", 0xFFFFFF);
	newLine();
	printRegistersSnapshot();
	videoPrint("Presiona ENTER para continuar...", 0xFFFFFF);
	newLine();

	_sti();
	int c;

	do{
		_hlt();
	}while((c = getFromBuffer()) != '\n');

	newLine();
}