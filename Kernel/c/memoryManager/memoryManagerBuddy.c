/* 
** Header (8 bytes) -> metadata interna del allocator. 
** Payload -> espacio util para el usuario. Siguen inmediatamente despues del header.
** El bloque completo (header + payload) es potencia de 2.
*/

#include "../include/memoryManager.h"
#include <stddef.h>

#define MIN_ORDER 4     /* 2^4 = 16 bytes (8B header + 8B payload minimo) */ 
#define MAX_ORDER 23    /* 2^23 = 8 MB */ 

/* Numero total de bloques distintos diponibles para Buddy. */
#define ORDERS (MAX_ORDER - MIN_ORDER + 1)  

/* 
** Header presente en TODOS los bloques de Buddy (libres y asignados).
** La estructura completa ocupa 8 bytes, dejando el payload inmediatamente despues del header.
*/
typedef struct{
    uint8_t order;      /* Potencia de 2.*/
    uint8_t is_free;    /* Flag de disponibilidad. */
    uint8_t is_kernel;  /* 1 si fue asignado internamente por el kernel. */
    uint8_t _pad[5];    /* Relleno para alinear a 8 bytes. */
}BlockHdr;             

/* Bloque libre: reutiliza los primeros bytes del payload para el enlace. */ 
typedef struct FreeNode{
    BlockHdr hdr;
    struct FreeNode *next;
}FreeNode;

static void *heap_base = NULL;

/* Contadores de bloques asignados y libres. */
static uint64_t heap_total = 0;
static uint64_t alloc_count = 0;

/* Arreglo de listas enlazadas para cada orden. */
static FreeNode *free_lists[ORDERS];

static int order_idx(int order){
    return order - MIN_ORDER;
}

/* Requested allocation size to smalles Buddy System order. */
static int size_to_order(uint64_t size){
    int order = MIN_ORDER;

    while(((uint64_t)1 << order) < size){
        if(++order > MAX_ORDER){
            return -1;
        }
    }

    return order;
}

/* Computa la direccion de memoria del bloque "Buddy" para el orden dado. */
static FreeNode *get_buddy(FreeNode *block, int order){
    // El offset del bloque dentro del heap determina su buddy usando XOR.
    uintptr_t offset = (uintptr_t)block - (uintptr_t)heap_base;
    // El buddy se encuentra a una distancia de 2^order del bloque actual.
    uintptr_t buddy_off = offset ^ ((uintptr_t)1 << order);

    if(buddy_off + ((uintptr_t)1 << order) > heap_total){
        return NULL;
    }

    return (FreeNode *)((uintptr_t)heap_base + buddy_off);
}

/* ELimina un nodo especifico de la lista. Usado durante la coalescencia. */
static void list_remove(int order, FreeNode *target){
    FreeNode **cur = &free_lists[order_idx(order)];

    while(*cur != NULL && *cur != target){
        cur = &(*cur)->next;
    }

    if(*cur == target){
        /* Skip the node. */
        *cur = target->next;
    }
}

/* 
** Inicializa Buddy System. Setea el heap y lo descompone en bloques de potencia de 2. 
** Greedy: se asignan bloques del mayor order posible, luego se splitean hacia abajo.
*/
void mm_init(void *start, uint64_t size){
    heap_base = start;
    heap_total = size;
    alloc_count = 0;

    for(int i = 0; i < ORDERS; i++){
        free_lists[i] = NULL;
    }

    uintptr_t offset = 0;

    /* Descomponer el heap en bloques de potencia de 2 (Greedy, mayor a menor). */
    for(int order = MAX_ORDER; order >= MIN_ORDER && offset < size; order--){ 
        uint64_t block_size = (uint64_t)1 << order;

        if(offset + block_size <= size){
            /* Crear un nuevo bloque libre y agregarlo a la lista.*/
            FreeNode *block = (FreeNode *)((uintptr_t)start + offset);
            block->hdr.order = (uint8_t)order;
            block->hdr.is_free = 1;
            block->hdr.is_kernel = 0;
            block->next = free_lists[order_idx(order)];
            free_lists[order_idx(order)] = block;
            offset += block_size;
        }
    }
}

static void *malloc_impl(uint64_t size, int is_kernel){
    if(size == 0 || heap_base == NULL){
        return NULL;
    }

    int order = size_to_order(size + sizeof(BlockHdr));
    if(order < 0){
        return NULL;
    }

    int found = -1;
    for(int i = order; i <= MAX_ORDER; i++){
        if(free_lists[order_idx(i)] != NULL){
            found = i;
            break;
        }
    }
    if(found < 0){
        return NULL;
    }

    FreeNode *block = free_lists[order_idx(found)];
    free_lists[order_idx(found)] = block->next;

    while(found > order){
        found--;
        FreeNode *buddy = (FreeNode *)((uintptr_t)block + ((uintptr_t)1 << found));
        buddy->hdr.order = (uint8_t)found;
        buddy->hdr.is_free = 1;
        buddy->hdr.is_kernel = 0;
        buddy->next = free_lists[order_idx(found)];
        free_lists[order_idx(found)] = buddy;
        block->hdr.order = (uint8_t)found;
    }

    block->hdr.is_free = 0;
    block->hdr.is_kernel = (uint8_t)is_kernel;
    if(!is_kernel)
        alloc_count++;

    return (void *)((uintptr_t)block + sizeof(BlockHdr));
}

void *mm_malloc(uint64_t size){
    return malloc_impl(size, USER);
}

void *mm_malloc_kernel(uint64_t size){
    return malloc_impl(size, KERNEL);
}

/* Libera un bloque de memoria asignado previamente por Buddy System. */
void mm_free(void *ptr){
    if(ptr == NULL){
        return;
    }
    
    /* Apunta a la primera ubicacion del Header. */
    FreeNode *block = (FreeNode *)((uintptr_t)ptr - sizeof(BlockHdr));

    int order = block->hdr.order;
    if(!block->hdr.is_kernel)
        alloc_count--;
    block->hdr.is_free = 1;

    /* Coalescencia: absorber buddies libres del mismo order */
    while(order < MAX_ORDER){
        FreeNode *buddy = get_buddy(block, order);

        /* Chequea que el buddy esta disponible para fusionar */
        if(buddy == NULL || !buddy->hdr.is_free || buddy->hdr.order != order){
            break;
        }
        
        /* Elimina el buddy de su lista order para que ese bloque no 
        ** aparezca como disponible antes de fusionarlo con block. */
        list_remove(order, buddy);

        /* El bloque fusionado empieza en la menor direccion. */
        if((uintptr_t)buddy < (uintptr_t)block){
            block = buddy;
        }

        /* Actualiza el order del nuevo bloque mas grande. */
        order++;
        block->hdr.order = (uint8_t)order;
    }
 
    /* Vuelvee🐐 a ingresar el bloque liberado a la lista de memorias libres */
    block->next = free_lists[order_idx(order)];
    free_lists[order_idx(order)] = block;
}


/* Devuelve el estado actual del heap. Recorre linealmente todos los bloques
** (libres y asignados) usando el campo `order` del header como salto, y
** clasifica cada uno en free / used (USER) / used_kernel (KERNEL).
** Invariante: total == free + used + used_kernel. */
void mm_status(MemStatus *status){
    if(status == NULL || heap_base == NULL){
        return;
    }

    uint64_t free_bytes = 0;
    uint64_t used_bytes = 0;
    uint64_t used_kernel_bytes = 0;

    uintptr_t off = 0;
    while(off < heap_total){
        FreeNode *b = (FreeNode *)((uintptr_t)heap_base + off);
        uint64_t bsz = (uint64_t)1 << b->hdr.order;

        if(b->hdr.is_free){
            free_bytes += bsz;
        }else if(b->hdr.is_kernel){
            used_kernel_bytes += bsz;
        }else{
            used_bytes += bsz;
        }

        off += bsz;
    }

    status->total = heap_total;
    status->free = free_bytes;
    status->used = used_bytes;
    status->used_kernel = used_kernel_bytes;
    status->alloc_count = alloc_count;
}
