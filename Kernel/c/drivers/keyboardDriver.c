#include "../include/videoDriver.h"
#include "../include/keyboardDriver.h"
#include "../include/naiveConsole.h"
#include "../include/defs.h"
#include "../include/lib.h"
#include "../include/process.h"
#include <stdint.h>

static PCB *kbd_waiting_process = NULL;

void kbd_set_waiting(struct PCB *p) {
    kbd_waiting_process = p;
}

void kbd_clear_waiting(struct PCB *p) {
    if(kbd_waiting_process == p){
        kbd_waiting_process = NULL;
    }
}

char kbd_min[KBD_LENGTH] = {
    0,  27, '1','2','3','4','5','6','7','8','9','0','-','=', '\b', // backspace
    '\t','q','w','e','r','t','y','u','i','o','p','[',']','\n',     //tab y enter
    0,'a','s','d','f','g','h','j','k','l',';','\'','`',
    0,'\\','z','x','c','v','b','n','m',',','.','/', 0, '*',
    0,' ',
};

char kbd_mayus[KBD_LENGTH] = {
    0,  27, '!', '@', '#', '$', '%', '^', '&', '*', '(', ')', '_', '+', '\b', // backspace
    '\t','Q','W','E','R','T','Y','U','I','O','P','{','}','\n',     // tab y enter
    0,'A','S','D','F','G','H','J','K','L',':','"','~',
    0,'|','Z','X','C','V','B','N','M','<','>','?', 0, '*',
    0,' ',
};

char * kbd_manager[] = {kbd_min, kbd_mayus};
static __attribute__((unused)) uint8_t pressedKeys[LETTERS] = {0};

static char buff[BUFF_LENGTH];
static char registersBuff[REGISTERS_BUFFER_SIZE];
static int shift = 0;
static int caps = 0;
static int ctrl = 0;
static int buff_size = 0;
static int start_index = 0;
static int end_index = 0;
static int boolRegisters = 0;
static void storeSnapshot(void);

/* Estado de terminal cooked mode */
int tty_mode = TTY_RAW;
char tty_line[256];
int tty_line_len = 0;
int tty_line_ready = 0;
int tty_eof = 0;

int tty_get_mode(void){
    return tty_mode;
}

void tty_set_mode(int mode){
    tty_mode = mode;
    tty_line_len = 0;
    tty_line_ready = 0;
    tty_eof = 0;
}

// Inserta un caracter en el buffer circular de teclado
void writeBuff(unsigned char c){
    buff[end_index] = c;
    end_index = (end_index + 1) % BUFF_LENGTH;
    if(buff_size < BUFF_LENGTH){
        buff_size++;
    } else{
        start_index = (start_index + 1) % BUFF_LENGTH; 
    }
}

// Reinicia el buffer de teclado
void clearBuff(void){
    buff_size = 0;
    start_index = 0;
    end_index = 0;
}

// Extrae un byte del buffer; -1 si vacío
uint8_t getFromBuffer(void){
    if(buff_size == 0){
        return (uint8_t) - 1;
    }

    buff_size--;
    uint8_t result = buff[start_index];
    start_index = (start_index + 1) % BUFF_LENGTH;

    return result;
}

// Copia hasta 'count' bytes disponibles del buffer de teclado
uint64_t readKeyBuff(char * buff, uint64_t count){
    uint64_t i;
    for(i = 0; i < count && i < (uint64_t)buff_size; i++){
        buff[i] = getFromBuffer();
    }

    return i;
}

// Traduce el scancode leído por la ISR y lo almacena en el buffer
void handlePressedKey(void){
    uint8_t scancode = kbd_scancode_read();

    if(scancode == L_SHIFT || scancode == R_SHIFT){
        shift = 1;
    } else if(scancode == (L_SHIFT | BREAK_CODE) || scancode == (R_SHIFT | BREAK_CODE)){
        shift = 0;
    } else if(scancode == L_CONTROL){
        ctrl = 1;
        return;
    } else if(scancode == (L_CONTROL | BREAK_CODE)){
        ctrl = 0;
        return;
    } else if(scancode == 0x3B){  // F1: snapshot de registros
        storeSnapshot();
        boolRegisters = 1;
        return;
    } else if(scancode == L_ARROW || scancode == R_ARROW || scancode == UP_ARROW || scancode == DOWN_ARROW || scancode == 0 || scancode > BREAK_CODE){
        return;
    } else if(ctrl && scancode == 0x2E){  // Ctrl + C: matar foreground
        process_kill_foreground();
        return;
    } else if(ctrl && scancode == 0x20){  // Ctrl + D: enviar EOF
        if(tty_mode == TTY_COOKED && kbd_waiting_process != NULL){
            if(tty_line_len > 0){
                tty_line_ready = 1;
            } else {
                tty_eof = 1;
            }
            kbd_waiting_process->state = PROCESS_READY;
            kbd_waiting_process = NULL;
        } else {
            writeBuff(0x04);
            if(kbd_waiting_process != NULL){
                kbd_waiting_process->state = PROCESS_READY;
                kbd_waiting_process = NULL;
            }
        }
        return;
    } else if(scancode == CAPS_LOCK){
        caps = !caps;
    } else if(!(scancode & BREAK_CODE)){
        char c = kbd_manager[(shift + caps) % 2][scancode];

        if(tty_mode == TTY_COOKED && kbd_waiting_process != NULL){
            if(c == '\n'){
                videoPutChar(c, 0xFFFFFF);
                tty_line[tty_line_len++] = c;
                tty_line_ready = 1;
                kbd_waiting_process->state = PROCESS_READY;
                kbd_waiting_process = NULL;
            } else if(c == '\b'){
                if(tty_line_len > 0){
                    tty_line_len--;
                    videoPutChar('\b', 0xFFFFFF);
                }
            } else {
                if(tty_line_len < (int)sizeof(tty_line) - 1){
                    videoPutChar(c, 0xFFFFFF);
                    tty_line[tty_line_len++] = c;
                }
            }
        } else {
            writeBuff(c);

            // Despertar al proceso que esperaba input
            if(kbd_waiting_process != NULL){
                kbd_waiting_process->state = PROCESS_READY;
                kbd_waiting_process = NULL;
            }
        }
    }
}

// Copia el snapshot de registros a 'buff' si está disponible
uint64_t copyRegistersBuffer(char * buff){
    if(boolRegisters){
        int i;
        for(i = 0; registersBuff[i]; i++){
            buff[i] = registersBuff[i];
        }

        buff[i] = 0;
        return 1; 
    }

    return 0;
}

// Serializa los registros guardados por la ISR en formato legible
void storeSnapshot(void){
    const char * regs[] = {"RAX: 0x", "RBX: 0x", "RCX: 0x", "RDX: 0x", "RBP: 0x", "RDI: 0x", "RSI: 0x",  
     "R8: 0x", "R9: 0x", "R10: 0x", "R11: 0x", "R12: 0x", "R13: 0x", "R14: 0x", "R15: 0x", "RIP: 0x", "CS: 0x", "RFLAGS: 0x", "RSP: 0x", "SS: 0x", 0};

    uint32_t j = 0;

    for(int i = 0; regs[i]; i++){

        for(int k = 0; regs[i][k]; k++){
            registersBuff[j++] = regs[i][k];
        }

        j += intToHexa(regsArray[i], registersBuff + j);

        registersBuff[j] = '\n';
        j++;
    }

  registersBuff[j] = 0;
}

/* Escribe en `dest` la representación hexadecimal (mayúsculas) del valor
usando exactamente hasta 16 dígitos, rellenando con ceros a la izquierda
para completar 16 dígitos. */
uint32_t intToHexa(uint64_t value, char *dest){
    if (!dest){
        return 0;
    }

    int zerosPad = 16;
    uint64_t aux = value;

    /* una iteración por nibble no nulo */
    while(aux){
        aux >>= 4;
        --zerosPad;
    }

    uint32_t k = 0;
    for(int i = 0; i < zerosPad; i++){
        dest[k++] = '0';
    }

    /* Escribir la parte significativa si value != 0. uintToBase escribe los dígitos
       y deja un '\0' al final; la función devuelve la cantidad de dígitos escritos. */
    if(value){
        k += uintToBase(value, dest + k, 16);
    }

    dest[k] = 0;

    return k;
}