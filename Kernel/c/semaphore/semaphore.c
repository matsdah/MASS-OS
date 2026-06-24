#include "semaphore.h"
#include "process.h"
#include "scheduler.h"
#include "lib.h"
#include <stddef.h>

/* Maxima cantidad de semáforos. */
#define MAX_SEMS 32

/* Maxima longitud del nombre de un semáforo. */
#define SEM_NAME_LEN 32

#define LOCKED   1u
#define UNLOCKED 0u

typedef struct {
    char name[SEM_NAME_LEN];            /* Nombre del semáforo. */
    int64_t value;                      /* Valor del semáforo. */
    uint64_t wait_queue[MAX_PROCESSES]; /* PIDs bloqueados esperando este sem. */
    int wait_head;          /* Índice del primer proceso en la cola de espera. */
    int wait_tail;          /* Índice del último proceso en la cola de espera. */
    int wait_count;         /* Cantidad de procesos en la cola de espera. */
    int open_count;         /* Cantidad de procesos que tienen el semáforo abierto. */
    volatile uint64_t lock; /* Spinlock (test-and-set via xchg). */
} Semaphore;

/* Tabla global de semáforos. */
static Semaphore sem_table[MAX_SEMS];

/* Helpers. */

/* Compara dos strings de nombres de semaforos. */
static int sem_str_eq(const char *a, const char *b){
    if(!a || !b){
        return 0;
    } 

    while(*a && (*a == *b)){ 
        a++; 
        b++; 
    }

    return ((unsigned char)*a == (unsigned char)*b);
}

/* Copia una string de nombre de semáforo. */
static void sem_str_copy(char *dst, const char *src, int max) {
    int i = 0;
    
    while (i < max - 1 && src && src[i]){
        dst[i] = src[i]; i++;
    }
    
    dst[i] = '\0';
}

/* Busca un semáforo por nombre. */
static Semaphore *find_sem(const char *name){
    int i;
    
    for (i = 0; i < MAX_SEMS; i++){
        if((sem_table[i].open_count > 0) && (sem_str_eq(sem_table[i].name, name))){
            return &sem_table[i];
        }
    }
        
    return NULL;
}

/* Agrega un proceso a la cola de espera del semáforo. */
static void queue_push(Semaphore* s, uint64_t pid){

    if(s->wait_count >= MAX_PROCESSES){
        return;
    }

    s->wait_queue[s->wait_tail] = pid;

    s->wait_tail = (s->wait_tail + 1) % MAX_PROCESSES;

    s->wait_count++;
}

/* Quita un proceso de la cola de espera del semáforo. */
static uint64_t queue_pop(Semaphore* s){

    uint64_t pid = s->wait_queue[s->wait_head];

    s->wait_head = (s->wait_head + 1) % MAX_PROCESSES;

    s->wait_count--;

    return pid;
}

/* Quita un PID de la cola de espera; retorna 1 si estaba, 0 si no. */
static int queue_remove_pid(Semaphore* s, uint64_t pid){
    if(s->wait_count <= 0){
        return 0;
    }

    uint64_t new_q[MAX_PROCESSES];
    int new_count = 0, found = 0, idx = s->wait_head;

    for(int i = 0; i < s->wait_count; i++){
        if(s->wait_queue[idx] != pid){
            new_q[new_count++] = s->wait_queue[idx];
        } else {
            found = 1;
        }
        idx = (idx + 1) % MAX_PROCESSES;
    }

    if(found){
        for(int i = 0; i < new_count; i++){
            s->wait_queue[i] = new_q[i];
        }
        s->wait_head = 0;
        s->wait_tail = new_count;
        s->wait_count = new_count;
    }

    return found;
}

/* Spinlock: test-and-set via xchg atómico. En single-core con IF=0
   (syscall handler) nunca itera, pero garantiza la atomicidad exigida. */
static void sem_lock(volatile uint64_t *l){
    while(atomic_xchg(l, LOCKED) == LOCKED){
        /* spin */
    }
}

static void sem_unlock(volatile uint64_t *l){
    atomic_xchg(l, UNLOCKED);
}

/* Índice del semáforo en sem_table (para el bitmap held_sems del PCB). */
static int sem_index(Semaphore *s){
    return (int)(s - sem_table);
}

/* API pública */

/* Inicializa la tabla de semáforos. */
void sem_init(void){
    int i, j;
    
    for(i = 0; i < MAX_SEMS; i++){
        sem_table[i].open_count = 0;
        sem_table[i].value = 0;
        sem_table[i].wait_head = 0;
        sem_table[i].wait_tail = 0;
        sem_table[i].wait_count = 0;
        sem_table[i].name[0] = '\0';
        sem_table[i].lock = UNLOCKED;
        
        for (j = 0; j < MAX_PROCESSES; j++){
            sem_table[i].wait_queue[j] = 0;
        }
    }
} 

/* Abre (o crea) un semáforo por nombre. Retorna 1 en éxito, 0 si tabla llena. */
int64_t sem_open(const char* name, uint64_t initial_value){

    if(!name){
        return 0;
    }

    /* ¿Ya existe? */
    Semaphore* s = find_sem(name);

    if(s){
        s->open_count++;
    } else {
        /* Buscar slot libre -> creacion de semaforo. Un slot con open_count==0 esta
           libre (find_sem solo encuentra los que tienen open_count>0). */
        int i;
        for(i = 0; i < MAX_SEMS; i++){
            if(sem_table[i].open_count == 0){
                s = &sem_table[i];
                break;
            }
        }

        if(!s){
            /* Tabla de semaforos llena. */
            return 0;
        }

        /* Inicializar el nuevo semáforo encontrado. */
        sem_str_copy(s->name, name, SEM_NAME_LEN);
        s->value = (int64_t)initial_value;
        s->open_count = 1;
        s->wait_head = 0;
        s->wait_tail = 0;
        s->wait_count = 0;
        s->lock = UNLOCKED;
    }

    /* Registrar al proceso como opener: el bit permite que sem_cleanup_for_process
       devuelva esta referencia (decrementar open_count) si el proceso muere sin cerrar. */
    PCB* cur = process_current();
    if(cur != NULL){
        cur->opened_sems |= (1u << sem_index(s));
    }

    return 1;
}

/* Decrementa el semáforo; bloquea el proceso actual si value < 0.
 * Sección crítica (value + cola) protegida por spinlock con xchg.
 * El unlock va SIEMPRE antes de process_block: si no, el proceso
 * descheduleado nunca liberaría el lock. */
int64_t sem_wait(const char *name){
    if(!name){
        return -1;
    }

    Semaphore* s = find_sem(name);
    if(!s){
        return -1;
    }

    int idx = sem_index(s);
    PCB* cur = process_current();

    sem_lock(&s->lock);
    s->value--;

    if(s->value < 0){
        /* Sin recurso: a la cola y bloquear. */
        if(cur == NULL){
            sem_unlock(&s->lock);
            return -1;
        }

        queue_push(s, cur->pid);
        sem_unlock(&s->lock);

        /* process_block setea BLOCKED + force_switch; el context switch
           real ocurre en iretq, ya con el lock liberado. */
        process_block(cur->pid);
    } else {
        /* Recurso adquirido: marcar tenencia en el PCB. */
        if(cur != NULL){
            cur->held_sems |= (1u << idx);
        }
        sem_unlock(&s->lock);
    }

    return 0;
}

/* Incrementa el semáforo; despierta un proceso en espera si value <= 0.
 * Sección crítica protegida por spinlock. El recurso se transfiere al
 * proceso despertado (se le setea su bit de held_sems). */
int64_t sem_post(const char *name){
    if (!name){
        return -1;
    }

    Semaphore* s = find_sem(name);
    if(!s){
        return -1;
    }

    int idx = sem_index(s);
    PCB* cur = process_current();

    sem_lock(&s->lock);

    /* El que hace post libera su tenencia (no-op si no la tenía). */
    if(cur != NULL){
        cur->held_sems &= ~(1u << idx);
    }

    s->value++;

    if((s->value <= 0) && (s->wait_count > 0)){
        /* Sacar un proceso de la cola de espera y transferirle el recurso. */
        uint64_t pid = queue_pop(s);
        PCB* woken = process_get(pid);
        if(woken != NULL){
            woken->held_sems |= (1u << idx);
        }
        sem_unlock(&s->lock);

        /* Desbloquear tras liberar el lock. */
        process_unblock(pid);
    } else {
        sem_unlock(&s->lock);
    }

    return 0;
}

/* Limpia todos los semáforos asociados a un proceso que muere.
 * Itera TODOS los semáforos abiertos (un proceso puede tener varios):
 *   - Si estaba en cola de espera: lo saca y compensa el value-- de sem_wait.
 *   - Si tenía el recurso (bit held): lo libera con un post y despierta
 *     a un posible waiter transfiriéndole la tenencia.
 *   - Si ninguna de las dos: no toca value (evita post espurio). */
void sem_cleanup_for_process(uint64_t pid){
    PCB* p = process_get(pid);
    if(p == NULL){
        return;
    }

    for(int i = 0; i < MAX_SEMS; i++){
        Semaphore* s = &sem_table[i];
        if(s->open_count <= 0){
            continue;
        }

        sem_lock(&s->lock);

        /* 1) ¿Estaba en la cola de espera de este semáforo? */
        int was_queued = queue_remove_pid(s, pid);

        /* 1.5) Si el proceso tenía el semáforo abierto: devolver su referencia de
               open_count (evita la fuga cuando un proceso muere sin sem_close).
               Independiente de queued/held. open_count==0 deja el slot reutilizable. */
        if((p->opened_sems >> i) & 1u){
            p->opened_sems &= ~(1u << i);
            if(s->open_count > 0){
                s->open_count--;
            }
        }

        /* 2) queued y held son MUTUAMENTE EXCLUYENTES: un proceso encolado está
              ESPERANDO el recurso, no lo tiene. Tratarlos por separado (dos value++)
              sobre-compensa cuando held_sems tiene un bit stale — caso productor/
              consumidor (MVar): se hace wait sobre un sem y post sobre OTRO, así que
              sem_post nunca limpia el bit del sem en el que se espera. Resultado:
              value queda +1 y se rompe el invariante del buffer. */
        if(was_queued){
            /* Esperando: compensar el único value-- del sem_wait bloqueante y
               descartar cualquier bit held stale SIN un segundo value++. */
            s->value++;
            p->held_sems &= ~(1u << i);
        } else if((p->held_sems >> i) & 1u){
            /* Tenía el recurso (no estaba esperando): liberarlo y despertar a un
               waiter transfiriéndole la tenencia. */
            p->held_sems &= ~(1u << i);
            s->value++;
            if((s->value <= 0) && (s->wait_count > 0)){
                uint64_t wp = queue_pop(s);
                PCB* woken = process_get(wp);
                if(woken != NULL){
                    woken->held_sems |= (1u << i);
                }
                sem_unlock(&s->lock);
                process_unblock(wp);
                continue;  /* ya se liberó el lock */
            }
        }

        sem_unlock(&s->lock);
    }

    /* Por seguridad: todos los bits ya se limpiaron arriba. */
    p->held_sems = 0;
    p->opened_sems = 0;
}

/* Decrementa el contador de usuarios; libera la entrada si llega a 0.
 * Limpia el bit de held del PCB para que el cleanup no procese un slot
 * que ya se cerró (y podría reutilizarse con otro nombre). */
int64_t sem_close(const char *name){
    if(!name){
        return -1;
    }

    Semaphore* s = find_sem(name);

    if(!s){
        return -1;
    }

    /* Limpiar bits de tenencia y apertura del caller (slot puede reutilizarse). */
    int idx = sem_index(s);
    PCB* cur = process_current();
    if(cur != NULL){
        cur->held_sems &= ~(1u << idx);
        cur->opened_sems &= ~(1u << idx);
    }

    /* Decrementar el contador de usuarios. */
    s->open_count--;

    /* Liberar la entrada si el contador de usuariosllega a 0. */
    if(s->open_count <= 0){
        s->open_count = 0;
        s->name[0] = '\0';
        s->wait_head = 0;
        s->wait_tail = 0;
        s->wait_count = 0;
    }

    return 0;
}
