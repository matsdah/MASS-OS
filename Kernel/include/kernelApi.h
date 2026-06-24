#ifndef KERNEL_API_H
#define KERNEL_API_H

#include <stdint.h>

// Funciones globales del kernel usadas por el loader/ASM
void clearBSS(void * bssAddress, uint64_t bssSize);
void * getStackBase(void);
void * initializeKernelBinary(void);

#endif