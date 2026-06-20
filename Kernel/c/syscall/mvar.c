#include "mvar.h"
#include "process.h"
#include "scheduler.h"
#include "lib.h"
#include <stddef.h>

#define MAX_MVARS 16
#define MVAR_NAME_LEN 32
#define MVAR_Q_SIZE MAX_PROCESSES

#define LOCKED   1u
#define UNLOCKED 0u

typedef enum {
    MVAR_EMPTY = 0,
    MVAR_FULL  = 1
} MVarState;

/* Entrada de cola: empaqueta el PID junto con el valor pendiente del
 * escritor. Asi el valor a escribir vive en la cola del MVar (donde
 * pertenece) y no en el PCB del proceso. Para la rq el valor queda en 0. */
typedef struct {
    uint64_t pid;
    char     value;
} mvar_q_entry;

typedef struct {
    char name[MVAR_NAME_LEN];
    MVarState state;
    char value;
    int in_use;

    /* Cola circular de escritores bloqueados */
    mvar_q_entry wq[MVAR_Q_SIZE];
    int wq_head;
    int wq_tail;
    int wq_count;

    /* Cola circular de lectores bloqueados */
    mvar_q_entry rq[MVAR_Q_SIZE];
    int rq_head;
    int rq_tail;
    int rq_count;

    /* Contador de serves consecutivos del nivel mas alto (anti-starvation) */
    int wq_consecutive;
    int rq_consecutive;

    volatile uint64_t lock;   /* Spinlock (test-and-set via xchg). */
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

/* Spinlock: test-and-set via xchg atómico. En single-core con IF=0
   (syscall handler) nunca itera, pero garantiza la atomicidad exigida. */
static void mvar_lock(volatile uint64_t *l){
    while(atomic_xchg(l, LOCKED) == LOCKED){
        /* spin */
    }
}

static void mvar_unlock(volatile uint64_t *l){
    atomic_xchg(l, UNLOCKED);
}

/* Helpers de cola circular FIFO (orden de llegada).
   El scheduler se encarga de la prioridad; el MVar solo garantiza
   que los procesos se desbloqueen en el orden en que llegaron. */
static void wq_push(MVar *mv, uint64_t pid, char value){
    if(mv->wq_count >= MVAR_Q_SIZE) return;
    mv->wq[mv->wq_tail].pid = pid;
    mv->wq[mv->wq_tail].value = value;
    mv->wq_tail = (mv->wq_tail + 1) % MVAR_Q_SIZE;
    mv->wq_count++;
}

static mvar_q_entry wq_pop(MVar *mv){
    mvar_q_entry e = mv->wq[mv->wq_head];
    mv->wq_head = (mv->wq_head + 1) % MVAR_Q_SIZE;
    mv->wq_count--;
    return e;
}

static void rq_push(MVar *mv, uint64_t pid){
    if(mv->rq_count >= MVAR_Q_SIZE) return;
    mv->rq[mv->rq_tail].pid = pid;
    mv->rq[mv->rq_tail].value = 0;
    mv->rq_tail = (mv->rq_tail + 1) % MVAR_Q_SIZE;
    mv->rq_count++;
}

static mvar_q_entry rq_pop(MVar *mv){
    mvar_q_entry e = mv->rq[mv->rq_head];
    mv->rq_head = (mv->rq_head + 1) % MVAR_Q_SIZE;
    mv->rq_count--;
    return e;
}

/* Extrae de la cola circular el PID con mayor prioridad.
   Anti-starvation: tras 'prioridad' serves seguidos del nivel mas alto,
   da un turno al de menor prioridad. */
static mvar_q_entry wq_pop_highest(MVar *mv){
    mvar_q_entry empty = {0, 0};
    if(mv->wq_count <= 0) return empty;

    int high_idx = -1, low_idx = -1;
    int high_prio = -1, low_prio = 999;
    int idx = mv->wq_head;
    for(int i = 0; i < mv->wq_count; i++){
        PCB *p = process_get(mv->wq[idx].pid);
        int prio = p ? p->priority : 0;
        if(prio > high_prio){ high_prio = prio; high_idx = idx; }
        if(prio < low_prio){  low_prio  = prio; low_idx  = idx; }
        idx = (idx + 1) % MVAR_Q_SIZE;
    }
    if(high_idx < 0) return wq_pop(mv);

    int target_idx;
    if(mv->wq_consecutive >= high_prio && low_idx != high_idx){
        target_idx = low_idx;
        mv->wq_consecutive = 0;
    } else {
        target_idx = high_idx;
        mv->wq_consecutive++;
    }

    mvar_q_entry ret = mv->wq[target_idx];
    mvar_q_entry new_q[MVAR_Q_SIZE];
    int new_count = 0;
    idx = mv->wq_head;
    for(int i = 0; i < mv->wq_count; i++){
        if(idx != target_idx) new_q[new_count++] = mv->wq[idx];
        idx = (idx + 1) % MVAR_Q_SIZE;
    }
    for(int i = 0; i < new_count; i++) mv->wq[i] = new_q[i];
    mv->wq_head = 0;
    mv->wq_tail = new_count;
    mv->wq_count = new_count;
    return ret;
}

static mvar_q_entry rq_pop_highest(MVar *mv){
    mvar_q_entry empty = {0, 0};
    if(mv->rq_count <= 0) return empty;

    int high_idx = -1, low_idx = -1;
    int high_prio = -1, low_prio = 999;
    int idx = mv->rq_head;
    for(int i = 0; i < mv->rq_count; i++){
        PCB *p = process_get(mv->rq[idx].pid);
        int prio = p ? p->priority : 0;
        if(prio > high_prio){ high_prio = prio; high_idx = idx; }
        if(prio < low_prio){  low_prio  = prio; low_idx  = idx; }
        idx = (idx + 1) % MVAR_Q_SIZE;
    }
    if(high_idx < 0) return rq_pop(mv);

    int target_idx;
    if(mv->rq_consecutive >= high_prio && low_idx != high_idx){
        target_idx = low_idx;
        mv->rq_consecutive = 0;
    } else {
        target_idx = high_idx;
        mv->rq_consecutive++;
    }

    mvar_q_entry ret = mv->rq[target_idx];
    mvar_q_entry new_q[MVAR_Q_SIZE];
    int new_count = 0;
    idx = mv->rq_head;
    for(int i = 0; i < mv->rq_count; i++){
        if(idx != target_idx) new_q[new_count++] = mv->rq[idx];
        idx = (idx + 1) % MVAR_Q_SIZE;
    }
    for(int i = 0; i < new_count; i++) mv->rq[i] = new_q[i];
    mv->rq_head = 0;
    mv->rq_tail = new_count;
    mv->rq_count = new_count;
    return ret;
}

/* Remueve un PID de una cola circular reconstruyendola. */
static void queue_remove_pid(mvar_q_entry *queue, int *head, int *tail, int *count, int max, uint64_t pid){
    if(*count <= 0) return;
    mvar_q_entry new_q[MVAR_Q_SIZE];
    int new_count = 0;
    int idx = *head;
    for(int i = 0; i < *count; i++){
        if(queue[idx].pid != pid){
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
        mvar_table[i].wq_consecutive = 0;
        mvar_table[i].rq_consecutive = 0;
        mvar_table[i].lock = UNLOCKED;
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
    mv->wq_consecutive = 0;
    mv->rq_consecutive = 0;
    mv->lock = UNLOCKED;
    mv->in_use = 1;
    return 1;
}

/* Escribe un valor en la MVar.
 * - Si EMPTY y hay lectores esperando: handoff directo (el lector
 *   despierta con el valor en RAX, la MVar queda EMPTY).
 * - Si EMPTY y no hay lectores: escribe y pasa a FULL.
 * - Si FULL: bloquea al escritor (encola PID+valor); despierta con 0
 *   cuando un take hace el handoff (escribe su valor en la MVar).
 * Retorna 0 en exito, -1 si la MVar no existe. */
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

    mvar_lock(&mv->lock);

    if(mv->state == MVAR_EMPTY){
        if(mv->rq_count > 0){
            /* Handoff: entregar el valor directamente al lector. */
            mvar_q_entry e = rq_pop_highest(mv);
            PCB *r = process_get(e.pid);
            if(r != NULL){
                /* Escribir el valor en el slot RAX guardado del lector. */
                r->rsp[14] = (uint64_t)(unsigned char)value;
            }
            /* MVar queda EMPTY: el lector "tomó" el valor. */
            mvar_unlock(&mv->lock);
            process_unblock(e.pid);
            return 0;
        }

        /* Sin lectores: escribir y pasar a FULL. */
        mv->value = value;
        mv->state = MVAR_FULL;
        mvar_unlock(&mv->lock);
        return 0;
    }

    /* FULL: bloquear escritor. El valor viaja en la entrada de la cola. */
    PCB *cur = process_current();
    if(!cur){
        mvar_unlock(&mv->lock);
        return -1;
    }
    wq_push(mv, cur->pid, value);
    mvar_unlock(&mv->lock);

    /* process_block via state+force_switch; el ISR guarda 0 en rsp[14]
       antes del yield. Cuando un take despierte al escritor, su valor
       ya habra sido escrito en la MVar (handoff). */
    cur->state = PROCESS_BLOCKED;
    force_switch = 1;
    return 0;
}

/* Lee y consume el valor de la MVar.
 * - Si FULL y hay escritores esperando: handoff (lee el valor actual,
 *   escribe el valor del escritor despertado, MVar queda FULL).
 * - Si FULL y no hay escritores: lee y pasa a EMPTY.
 * - Si EMPTY: bloquea al lector; despierta con el valor en RAX cuando
 *   un put hace el handoff.
 * Retorna el valor leido (0..255), -1 si la MVar no existe. */
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

    mvar_lock(&mv->lock);

    if(mv->state == MVAR_FULL){
        char val = mv->value;

        if(mv->wq_count > 0){
            /* Handoff: leer valor actual + escribir valor del escritor. */
            mvar_q_entry e = wq_pop_highest(mv);
            mv->value = e.value;       /* el escritor ya "escribió" */
            mv->state = MVAR_FULL;     /* queda FULL con el nuevo valor */
            mvar_unlock(&mv->lock);
            /* El escritor despierta con 0 (guardado por el ISR al bloquear). */
            process_unblock(e.pid);
            return (int64_t)(unsigned char)val;
        }

        /* Sin escritores: leer y pasar a EMPTY. */
        mv->state = MVAR_EMPTY;
        mvar_unlock(&mv->lock);
        return (int64_t)(unsigned char)val;
    }

    /* EMPTY: bloquear lector. */
    PCB *cur = process_current();
    if(!cur){
        mvar_unlock(&mv->lock);
        return -1;
    }
    rq_push(mv, cur->pid);
    mvar_unlock(&mv->lock);

    /* El ISR guarda 0 en rsp[14] antes del yield. Cuando un put
       despierte al lector, sobrescribira rsp[14] con el valor real. */
    cur->state = PROCESS_BLOCKED;
    force_switch = 1;
    return 0;
}

/* Destruye una MVar. Despierta a todos los procesos bloqueados con -1
 * (MVar destruida) escrito en su slot RAX guardado. */
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

    mvar_lock(&mv->lock);

    /* Recolectar PIDs a despertar y marcar -1 en su RAX guardado. */
    uint64_t to_wake[2 * MVAR_Q_SIZE];
    int wake_count = 0;

    while(mv->wq_count > 0){
        mvar_q_entry e = wq_pop(mv);
        PCB *p = process_get(e.pid);
        if(p && p->state == PROCESS_BLOCKED){
            p->rsp[14] = (uint64_t)(int64_t)(-1);
            to_wake[wake_count++] = e.pid;
        }
    }

    while(mv->rq_count > 0){
        mvar_q_entry e = rq_pop(mv);
        PCB *p = process_get(e.pid);
        if(p && p->state == PROCESS_BLOCKED){
            p->rsp[14] = (uint64_t)(int64_t)(-1);
            to_wake[wake_count++] = e.pid;
        }
    }

    mv->in_use = 0;
    mv->name[0] = '\0';

    mvar_unlock(&mv->lock);

    /* Despertar tras liberar el lock. */
    for(int i = 0; i < wake_count; i++){
        process_unblock(to_wake[i]);
    }

    return 1;
}

/* Limpia referencias de un proceso que muere.
 * Si el proceso estaba bloqueado en una cola, lo remueve.
 * Si tras la limpieza hay un desbalance, despertar a un sucesor
 * haciendo el handoff correspondiente. */
void mvar_cleanup_for_process(uint64_t pid){
    for(int i = 0; i < MAX_MVARS; i++){
        if(!mvar_table[i].in_use) continue;
        MVar *mv = &mvar_table[i];

        mvar_lock(&mv->lock);

        queue_remove_pid(mv->wq, &mv->wq_head, &mv->wq_tail, &mv->wq_count, MVAR_Q_SIZE, pid);
        queue_remove_pid(mv->rq, &mv->rq_head, &mv->rq_tail, &mv->rq_count, MVAR_Q_SIZE, pid);

        uint64_t wake_pid = 0;

        /* Si tras la limpieza hay un desbalance, despertar al menos
           un proceso bloqueado que ahora puede avanzar, con handoff. */
        if(mv->state == MVAR_EMPTY && mv->wq_count > 0){
            /* Un escritor puede escribir ahora. */
            mvar_q_entry e = wq_pop(mv);
            mv->value = e.value;
            mv->state = MVAR_FULL;
            wake_pid = e.pid;
            /* El escritor despierta con 0 (guardado por el ISR). */
        } else if(mv->state == MVAR_FULL && mv->rq_count > 0){
            /* Un lector puede leer ahora. */
            mvar_q_entry e = rq_pop(mv);
            PCB *r = process_get(e.pid);
            if(r != NULL){
                r->rsp[14] = (uint64_t)(unsigned char)mv->value;
            }
            mv->state = MVAR_EMPTY;
            wake_pid = e.pid;
        }

        mvar_unlock(&mv->lock);

        if(wake_pid != 0){
            process_unblock(wake_pid);
        }
    }
}
