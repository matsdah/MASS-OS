#include "scheduler.h"
#include "process.h"
#include "time.h"
#include "memoryManager.h"

// Leida desde interrupts.asm para decidir si hacer context switch voluntario
volatile uint64_t force_switch = 0;

/* Lista de procesos listos para ejecutarse. */
static PCB* run_queue[MAX_PROCESSES];

static int queue_size = 0;
static int queue_idx  = 0;

/* Inicializa la cola de ejecución. */
void scheduler_init(void){
    queue_size = 0;         /* Tamaño de la cola. */
    queue_idx = 0;          /* Índice del proceso actual en la cola (round-robin). */
}

/* Arranque: lanza el primer proceso sin retornar. */
void scheduler_start(void){
    /* Elegir el primer proceso READY en Round-Robin. */
    PCB* first = scheduler_next_ready();

    if(first == NULL){
        /* Error. */
        return;
    }

    first->state = PROCESS_RUNNING;
    process_set_current(first);

    /* Definida en interrupts.asm; no retorna. */
    scheduler_start_asm(first->rsp);
}

/* Agrega un proceso a la cola de ejecución ordenada por prioridad
   descendente. Mantiene Round Robin intra-nivel: dentro de la misma
   prioridad, el orden de llegada se preserva. */
void scheduler_add(PCB* p){
    if(queue_size >= MAX_PROCESSES) return;

    /* Insertar en orden descendente por prioridad. */
    int insert_pos = queue_size;
    for(int i = 0; i < queue_size; i++){
        if(run_queue[i]->priority < p->priority){
            insert_pos = i;
            /* Desplazar elementos a la derecha. */
            for(int j = queue_size; j > i; j--){
                run_queue[j] = run_queue[j-1];
            }
            break;
        }
    }
    run_queue[insert_pos] = p;
    queue_size++;

    /* Ajustar queue_idx si insertamos antes o en la posición actual. */
    if(insert_pos <= queue_idx && queue_size > 1){
        queue_idx++;
    }
}

/* Elimina un proceso de la cola de ejecución desplazando elementos
   para mantener el orden por prioridad. */
void scheduler_remove(PCB* p){
    for(int i = 0; i < queue_size; i++){
        if(p == run_queue[i]){
            /* Desplazar todo a la izquierda para mantener orden. */
            for(int j = i; j < queue_size - 1; j++){
                run_queue[j] = run_queue[j+1];
            }
            queue_size--;
            if((queue_idx >= queue_size) && (queue_size > 0)){
                queue_idx = queue_size - 1;
            } else if(i < queue_idx) {
                queue_idx--;
            }
            return;
        }
    }
}

/* 
** Elegir el siguiente proceso READY usando round-robin con preferencia
** a foreground. Dos pasadas: primero busca READY con foreground=1, si no
** encuentra ninguno busca cualquier READY (incluye background).
*/
PCB* scheduler_next_ready(void){
    if(queue_size == 0){
        return NULL;
    }

    /* Pasada 1: preferir foreground */
    for(int i = 0; i < queue_size; i++){
        queue_idx = (queue_idx + 1) % queue_size;
        PCB* c = run_queue[queue_idx];
        if(c->state == PROCESS_READY && c->foreground){
            return c;
        }
    }

    /* Pasada 2: fallback a cualquier READY */
    for(int i = 0; i < queue_size; i++){
        queue_idx = (queue_idx + 1) % queue_size;
        PCB* c = run_queue[queue_idx];
        if(c->state == PROCESS_READY){
            return c;
        }
    }

    return NULL;
}

/* 
** Llamada desde _irq00Handler (timer tick) 
** Round-Robin con prioridades: el proceso actual sigue corriendo mientras le
** queden quantum (priority quantum consecutivas por turno). Cuando se le agotan,
** pasa a READY y se elige el siguiente de la cola con round-robin.
*/
uint64_t* scheduler_tick(uint64_t* current_rsp){

    /* Actualizar contador de ticks. */
    timer_handler();

    PCB* cur = process_current();

    if(cur != NULL){
        /* Si hay un proceso corriendo... */
        cur->rsp = current_rsp;

        if(cur->state == PROCESS_RUNNING){
            if(cur->remaining_quanta > 0){
                cur->remaining_quanta--;
            }

            if(cur->remaining_quanta > 0){
                /* Todavia tiene quanta -> sigue corriendo. */
                return current_rsp;
            }

            /* Quantum agotado -> pasa a READY y reinicia el contador. */
            cur->state = PROCESS_READY;
            cur->remaining_quanta = cur->priority;
        }
    }

    /* Elegir el siguiente proceso READY. */
    /* Se llega aca cuando el proceso actual no tenga mas quantum. */
    PCB* next = scheduler_next_ready();

    if(next == NULL){
        /*
        ** Nadie listo: si el actual aun tiene un stack (no esta ZOMBIE/FREE),
        ** seguimos con el; sino quedamos con el RSP actual y la CPU hace hlt
        ** hasta el proximo IRQ.
        */
        if((cur != NULL) && (cur->state == PROCESS_READY || cur->state == PROCESS_RUNNING)){
            cur->state = PROCESS_RUNNING;
            return current_rsp;
        }

        /* Entra en hlt. */
        return current_rsp;
    }

    /* Reap ZOMBIE del proceso anterior si es necesario. */
    if(cur != NULL && cur->state == PROCESS_ZOMBIE){
        scheduler_remove(cur);
        if(cur->stack_base != NULL){
            mm_free(cur->stack_base);
            cur->stack_base = NULL;
        }
        if(cur->argv != NULL){
            mm_free(cur->argv);
            cur->argv = NULL;
        }
        cur->rsp = NULL;
        cur->state = PROCESS_FREE;
        cur->pid = 0;
    }

    next->state = PROCESS_RUNNING;
    process_set_current(next);

    return next->rsp;
}

/*
** Implementación de yield (pausa). Llamada desde _irq128Handler cuando
** force_switch = 1 (sys_yield, sys_exit, sys_block sobre el proceso actual,
** sys_read sin datos disponibles, etc.).
*/
uint64_t *scheduler_yield_impl(uint64_t *current_rsp){
    PCB* cur = process_current();

    if(cur != NULL){
        cur->rsp = current_rsp;

        if(cur->state == PROCESS_RUNNING){
            /* Lo pauso (si no fue ya BLOCKED/ZOMBIE por la syscall). */
            cur->state = PROCESS_READY;
        }

        /* Reiniciar quanta para la proxima vez que sea elegido. */
        cur->remaining_quanta = cur->priority;
    }

    PCB* next = scheduler_next_ready();
    if(next == NULL){
        /* Nadie listo distinto del actual. Si cur sigue listo, retomarlo. */
        if(cur != NULL && cur->state == PROCESS_READY){
            cur->state = PROCESS_RUNNING;
            return current_rsp;
        }
        /* Caso patologico: nadie listo y cur esta BLOCKED/ZOMBIE.
           Devolvemos el rsp actual; la CPU hara hlt hasta el proximo IRQ que
           desbloquee a alguien (idle deberia evitar este caso siempre). */
        return current_rsp;
    }

    /* Si el proceso que cede la CPU fue marcado ZOMBIE, liberarlo ahora que
       ya tenemos a donde saltar. */
    if(cur != NULL && cur->state == PROCESS_ZOMBIE){
        scheduler_remove(cur);
        if(cur->stack_base != NULL){
            mm_free(cur->stack_base);
            cur->stack_base = NULL;
        }
        if(cur->argv != NULL){
            mm_free(cur->argv);
            cur->argv = NULL;
        }
        cur->rsp = NULL;
        cur->state = PROCESS_FREE;
        cur->pid = 0;
    }

    next->state = PROCESS_RUNNING;
    process_set_current(next);

    return next->rsp;
}


