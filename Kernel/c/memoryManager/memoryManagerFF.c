// FF por First Fit -> algorimto de asignación de memoria (ver en el .md)
// Recorre la lista buscando el primer bloque libre con block->size >= size

#include "../include/memoryManager.h"
#include <stddef.h>

typedef struct MemBlock {
    uint64_t size;              /* Tamano del bloque (sin contar el header) */
    int is_free;                /* 1 si esta libre, 0 si esta asignado */
    int is_kernel;              /* 1 si fue asignado internamente por el kernel */
    struct MemBlock* next;      /* Siguiente bloque en la lista */
} MemBlock;

/* Tamano minimo para que valga la pena splitear el bloque de memoria libre. */ 
#define MIN_SPLIT (sizeof(MemBlock) + 8)

static MemBlock* heap_start = NULL;

void mm_init(void *start, uint64_t size){

    if(size <= sizeof(MemBlock)){
        /* Si el espacio es demasiado pequeño para contener al menos un bloque, no se inicializa. */ 
        return;
    }

    /* Inicializar el heap con un bloque unico que ocupa todo el espacio disponible. */
    heap_start = (MemBlock *)start;
    heap_start->size = size - sizeof(MemBlock);
    heap_start->is_free = 1;
    heap_start->is_kernel = 0;
    heap_start->next = NULL;
}

static void *malloc_impl(uint64_t size, int is_kernel){
    if(size == 0 || heap_start == NULL){
        return NULL;
    }

    /* Alinear a 8 bytes para evitar accesos desalineados. */
    size = (size + 7) & ~(uint64_t)7;

    MemBlock* block = heap_start;

    while(block != NULL){
        if(block->is_free && block->size >= size){
            if(block->size >= size + MIN_SPLIT){
                /* Split si sobra suficiente espacio para un bloque nuevo. */
                MemBlock* split = (MemBlock *)((uint8_t *)(block + 1) + size);
                split->size = block->size - size - sizeof(MemBlock);
                split->is_free = 1;
                split->is_kernel = 0;
                split->next = block->next;
                block->size = size;
                block->next = split;
            }

            block->is_free = 0;
            block->is_kernel = is_kernel;
            return (void*)(block + 1);
        }

        block = block->next;
    }

    return NULL;
}

void *mm_malloc(uint64_t size){
    return malloc_impl(size, USER);
}

void *mm_malloc_kernel(uint64_t size){
    return malloc_impl(size, KERNEL);
}

void mm_free(void *ptr){
    if(ptr == NULL){
        return;
    }

    MemBlock* block = (MemBlock*)ptr - 1;   /* Apunto al header del bloque a liberar. */
    block->is_free = 1;

    /* Coalescensia hacia adelante. Absorber bloques libres consecutivos. */ 
    while(block->next != NULL && block->next->is_free){

        /* Se lo come como el Dibu. */
        block->size += sizeof(MemBlock) + block->next->size;
        block->next = block->next->next;
    }

    /* Coalescencia hacia atras. Si el bloque anterior esta libre, absorber este. */ 
    MemBlock* prev = heap_start;
    while(prev != NULL && prev->next != block){
        prev = prev->next;
    }
    
    if(prev != NULL && prev->is_free){
        prev->size += sizeof(MemBlock) + block->size;
        prev->next = block->next;
    }
}

/* Recorre la lista enlazada de bloques y clasifica cada uno en free / used (USER) /
** used_kernel (KERNEL). Cada bloque aporta su tamano completo (payload + header)
** al total, de modo que la invariante total == free + used + used_kernel se mantenga. */
void mm_status(MemStatus* status){
    if(status == NULL || heap_start == NULL){
        return;
    }

    status->total = 0;
    status->free = 0;
    status->used = 0;
    status->used_kernel = 0;
    status->alloc_count = 0;

    MemBlock* block = heap_start;

    while(block != NULL){
        uint64_t bsz = block->size + sizeof(MemBlock);
        status->total += bsz;

        if(block->is_free){
            status->free += bsz;
        }else if(block->is_kernel){
            status->used_kernel += bsz;
        }else{
            status->used += bsz;
            status->alloc_count++;
        }
        /* Avanzo en el heap hacia el siguiente bloque. */
        block = block->next;
    }
}
