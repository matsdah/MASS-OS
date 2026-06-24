#ifndef LIB_H
#define LIB_H

#include <stdint.h>

void * memset(void * destination, int32_t character, uint64_t length);
void * memcpy(void * destination, const void * source, uint64_t length);
uint8_t inb(uint16_t port);
void outb(uint16_t port, uint8_t value);

/* Intercambia atómicamente *ptr <-> newval; retorna el valor viejo.
 * xchg con operando de memoria tiene LOCK implícito en x86. */
uint64_t atomic_xchg(volatile uint64_t *ptr, uint64_t newval);

#endif