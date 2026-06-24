#ifndef KEYBOARDDRIVER_H
#define KEYBOARDDRIVER_H

#include <defs.h>
#include <stdint.h>

void writeBuff(unsigned char c);
void clearBuff(void);
void handlePressedKey(void);
uint64_t copyRegistersBuffer(char * buff);
uint8_t getFromBuffer(void);
uint64_t readKeyBuff(char * buff, uint64_t count);
uint32_t intToHexa(uint64_t value, char * dest);
extern uint64_t regsArray[];
uint8_t kbd_scancode_read(void);

// Registra el proceso que esta bloqueado esperando input de teclado.
// handlePressedKey lo desbloquea cuando llega un caracter.
struct PCB;
void kbd_set_waiting(struct PCB *p);

// Limpia el waiter si apunta a p (proceso que muere). Evita un puntero colgante
// a un PCB liberado/reciclado cuando el lector de terminal es matado o termina.
void kbd_clear_waiting(struct PCB *p);

// Obtiene/establece el modo de la terminal (TTY_RAW o TTY_COOKED)
int tty_get_mode(void);
void tty_set_mode(int mode);

/* Variables de estado cooked (visibles para fd_read) */
extern int tty_mode;
extern char tty_line[];
extern int tty_line_len;
extern int tty_line_ready;
extern int tty_eof;

#endif