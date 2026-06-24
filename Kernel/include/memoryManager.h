#ifndef MEMORY_MANAGER_H
#define MEMORY_MANAGER_H

#include <stdint.h>

#define KERNEL 1
#define USER 0

/* Estructura para reportar el estado de la memoria. Ambos managers deben usar la misma estructura.
** Invariante: total == free + used + used_kernel. */
typedef struct {
    uint64_t total;         /* Bytes totales administrados. */
    uint64_t free;          /* Bytes libres. */
    uint64_t used;          /* Bytes asignados solo por usuario (mm_malloc). */
    uint64_t used_kernel;   /* Bytes asignados solo por kernel (mm_malloc_kernel). */
    uint64_t alloc_count;   /* Cantidad de bloques asignados actualmente por usuario. */
} MemStatus;

/* Inicializa el memory manager sobre el bloque [start, start+size) */
void mm_init(void *start, uint64_t size);

/* Reserva 'size' bytes. Retorna NULL si no hay espacio. */
void *mm_malloc(uint64_t size);

/* Igual que mm_malloc pero marca el bloque como interno del kernel: no se
   contabiliza en alloc_count, de modo que mm_status refleje solo las
   allocaciones hechas por el usuario (via sys_malloc). */
void *mm_malloc_kernel(uint64_t size);

/* Libera el bloque apuntado por 'ptr'. Si ptr es NULL, no hace nada. */
void mm_free(void *ptr);

/* Llena 'status' con el estado actual de la memoria. */ 
void mm_status(MemStatus *status);

#endif