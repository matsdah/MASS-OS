#include "mvar.h"
#include "process.h"
#include "scheduler.h"
#include "lib.h"
#include <stddef.h>

#define MAX_MVARS 16
#define MVAR_NAME_LEN 32
#define MVAR_Q_SIZE MAX_PROCESSES

typedef enum {
    MVAR_EMPTY = 0,
    MVAR_FULL  = 1
} MVarState;

typedef struct {
    char name[MVAR_NAME_LEN];
    MVarState state;
    char value;
    int in_use;

    /* Cola circular de escritores bloqueados */
    uint64_t wq[MVAR_Q_SIZE];
    int wq_head;
    int wq_tail;
    int wq_count;

    /* Cola circular de lectores bloqueados */
    uint64_t rq[MVAR_Q_SIZE];
    int rq_head;
    int rq_tail;
    int rq_count;
} MVar;

static MVar mvar_table[MAX_MVARS];

static int mvar_str_eq(const char *a, const char *b){
    if(!a || !b) return 0;
    while(*a && (*a == *b)){ a++; b++; }
    return ((unsigned char)*a == (unsigned char)*b);
}

static void mvar_str_copy(char *dst, const char *src, int max){
    int i = 0;
    while(i < max - 1 && src && src[i]){
        dst[i] = src[i]; i++;
    }
    dst[i] = '\0';
}

/* Helpers de cola circular */
static void wq_push(MVar *mv, uint64_t pid){
    if(mv->wq_count >= MVAR_Q_SIZE) return;
    mv->wq[mv->wq_tail] = pid;
    mv->wq_tail = (mv->wq_tail + 1) % MVAR_Q_SIZE;
    mv->wq_count++;
}

static uint64_t wq_pop(MVar *mv){
    uint64_t pid = mv->wq[mv->wq_head];
    mv->wq_head = (mv->wq_head + 1) % MVAR_Q_SIZE;
    mv->wq_count--;
    return pid;
}

static void rq_push(MVar *mv, uint64_t pid){
    if(mv->rq_count >= MVAR_Q_SIZE) return;
    mv->rq[mv->rq_tail] = pid;
    mv->rq_tail = (mv->rq_tail + 1) % MVAR_Q_SIZE;
    mv->rq_count++;
}

static uint64_t rq_pop(MVar *mv){
    uint64_t pid = mv->rq[mv->rq_head];
    mv->rq_head = (mv->rq_head + 1) % MVAR_Q_SIZE;
    mv->rq_count--;
    return pid;
}

void mvar_init(void){
    for(int i = 0; i < MAX_MVARS; i++){
        mvar_table[i].in_use = 0;
        mvar_table[i].name[0] = '\0';
        mvar_table[i].state = MVAR_EMPTY;
        mvar_table[i].value = 0;
        mvar_table[i].wq_head = 0;
        mvar_table[i].wq_tail = 0;
        mvar_table[i].wq_count = 0;
        mvar_table[i].rq_head = 0;
        mvar_table[i].rq_tail = 0;
        mvar_table[i].rq_count = 0;
    }
}

int64_t mvar_create(const char *name){
    if(!name) return 0;

    /* ¿Ya existe? */
    for(int i = 0; i < MAX_MVARS; i++){
        if(mvar_table[i].in_use && mvar_str_eq(mvar_table[i].name, name)){
            return 0;  /* ya existe */
        }
    }

    /* Buscar slot libre */
    int slot = -1;
    for(int i = 0; i < MAX_MVARS; i++){
        if(!mvar_table[i].in_use){
            slot = i;
            break;
        }
    }
    if(slot < 0) return 0;

    MVar *mv = &mvar_table[slot];
    mvar_str_copy(mv->name, name, MVAR_NAME_LEN);
    mv->state = MVAR_EMPTY;
    mv->value = 0;
    mv->wq_head = mv->wq_tail = mv->wq_count = 0;
    mv->rq_head = mv->rq_tail = mv->rq_count = 0;
    mv->in_use = 1;
    return 1;
}

int64_t mvar_put(const char *name, char value){
    if(!name) return -1;

    MVar *mv = NULL;
    for(int i = 0; i < MAX_MVARS; i++){
        if(mvar_table[i].in_use && mvar_str_eq(mvar_table[i].name, name)){
            mv = &mvar_table[i];
            break;
        }
    }
    if(!mv) return -1;

    if(mv->state == MVAR_EMPTY){
        /* Escribir inmediatamente y despertar lector si hay */
        mv->value = value;
        mv->state = MVAR_FULL;
        if(mv->rq_count > 0){
            uint64_t pid = rq_pop(mv);
            process_unblock(pid);
        }
        return 0;
    }

    /* FULL: bloquear escritor */
    PCB *cur = process_current();
    if(!cur) return -1;
    wq_push(mv, cur->pid);
    cur->state = PROCESS_BLOCKED;
    force_switch = 1;
    return -2;  /* bloqueado: el caller debe reintentar */
}

int64_t mvar_take(const char *name){
    if(!name) return -1;

    MVar *mv = NULL;
    for(int i = 0; i < MAX_MVARS; i++){
        if(mvar_table[i].in_use && mvar_str_eq(mvar_table[i].name, name)){
            mv = &mvar_table[i];
            break;
        }
    }
    if(!mv) return -1;

    if(mv->state == MVAR_FULL){
        /* Leer, vaciar y despertar escritor si hay */
        char val = mv->value;
        mv->state = MVAR_EMPTY;
        if(mv->wq_count > 0){
            uint64_t pid = wq_pop(mv);
            /* El escritor despertado ejecutara su put en su proximo turno.
             * Pero hay una optimizacion: podemos pasarle el valor directamente
             * para evitar un context switch extra. Sin embargo, como el escritor
             * podria haber sido matado, es mas seguro dejar que haga su propia
             * llamada a mvar_put. Dejamos que despierte y reintente. */
            process_unblock(pid);
        }
        return (int64_t)(unsigned char)val;
    }

    /* EMPTY: bloquear lector */
    PCB *cur = process_current();
    if(!cur) return -1;
    rq_push(mv, cur->pid);
    cur->state = PROCESS_BLOCKED;
    force_switch = 1;
    return -2;  /* bloqueado: el caller debe reintentar */
}

int64_t mvar_destroy(const char *name){
    if(!name) return 0;

    MVar *mv = NULL;
    for(int i = 0; i < MAX_MVARS; i++){
        if(mvar_table[i].in_use && mvar_str_eq(mvar_table[i].name, name)){
            mv = &mvar_table[i];
            break;
        }
    }
    if(!mv) return 0;

    /* Despertar a todos los escritores bloqueados */
    while(mv->wq_count > 0){
        uint64_t pid = wq_pop(mv);
        PCB *p = process_get(pid);
        if(p && p->state == PROCESS_BLOCKED){
            process_unblock(pid);
        }
    }

    /* Despertar a todos los lectores bloqueados */
    while(mv->rq_count > 0){
        uint64_t pid = rq_pop(mv);
        PCB *p = process_get(pid);
        if(p && p->state == PROCESS_BLOCKED){
            process_unblock(pid);
        }
    }

    mv->in_use = 0;
    mv->name[0] = '\0';
    return 1;
}

/* Remueve un PID de una cola circular reconstruyendola. */
static void queue_remove_pid(uint64_t *queue, int *head, int *tail, int *count, int max, uint64_t pid){
    if(*count <= 0) return;
    uint64_t new_q[MVAR_Q_SIZE];
    int new_count = 0;
    int idx = *head;
    for(int i = 0; i < *count; i++){
        if(queue[idx] != pid){
            new_q[new_count++] = queue[idx];
        }
        idx = (idx + 1) % max;
    }
    if(new_count < *count){
        /* El PID estaba en la cola */
        for(int i = 0; i < new_count; i++){
            queue[i] = new_q[i];
        }
        *head = 0;
        *tail = new_count;
        *count = new_count;
    }
}

void mvar_cleanup_for_process(uint64_t pid){
    for(int i = 0; i < MAX_MVARS; i++){
        if(!mvar_table[i].in_use) continue;
        MVar *mv = &mvar_table[i];
        queue_remove_pid(mv->wq, &mv->wq_head, &mv->wq_tail, &mv->wq_count, MVAR_Q_SIZE, pid);
        queue_remove_pid(mv->rq, &mv->rq_head, &mv->rq_tail, &mv->rq_count, MVAR_Q_SIZE, pid);
    }
}
