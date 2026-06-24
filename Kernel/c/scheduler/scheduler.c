#include "scheduler.h"
#include "process.h"
#include "time.h"
#include "memoryManager.h"

// Leida desde interrupts.asm para decidir si hacer context switch voluntario
volatile uint64_t force_switch = 0;

/* Aging: cada AGING_INTERVAL ticks de espera, la prioridad efectiva sube +1.
 * MAX_AGING_BONUS acota el bono para que un proceso prio-1 no exceda eff=5.
 * Timer a 100 Hz => 1 tick = 10ms => AGING_INTERVAL=50 == 500ms por +1. */
#define AGING_INTERVAL  50
#define MAX_AGING_BONUS 4

/* Lista de procesos listos para ejecutarse. */
static PCB* run_queue[MAX_PROCESSES];

static int queue_size = 0;
static int queue_idx  = 0;

/* Proceso idle: corre hlt cuando no hay ningun otro proceso READY. NO vive en la
   run_queue ni participa del aging/seleccion normal; solo se devuelve como fallback
   en scheduler_next_ready. Asi nunca le roba CPU a procesos reales. */
static PCB* idle_pcb = NULL;

void scheduler_set_idle(PCB* p){
    idle_pcb = p;
}

/* Bono de aging: +1 por cada AGING_INTERVAL ticks esperando, hasta el tope. */
static int aging_bonus(const PCB *p){
    int b = (int)(p->wait_ticks / AGING_INTERVAL);
    if(b > MAX_AGING_BONUS){
        b = MAX_AGING_BONUS;
    }
    return b;
}

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
    /* El idle nunca entra a la run_queue: es solo fallback (incluso si un nice/add
       lo intentara reinsertar). */
    if(p == idle_pcb) return;

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
** a foreground. La prioridad efectiva incluye el bono de aging, de modo
** que un proceso de baja prioridad que lleva mucho tiempo esperando
** eventualmente compita con los de mayor prioridad (anti-starvation).
** Dos pasadas: primero busca READY foreground con eff == highest_eff,
** si no encuentra ninguno busca cualquier READY con eff == highest_eff.
*/
PCB* scheduler_next_ready(void){
    if(queue_size == 0){
        return idle_pcb;   /* nadie en la cola: idle (o NULL si aun no se seteo) */
    }

    /* Encontrar la prioridad efectiva mas alta entre procesos READY.
       eff = priority + (foreground ? 1 : 0) + aging_bonus. */
    int highest_eff = -1;
    for(int i = 0; i < queue_size; i++){
        if(run_queue[i]->state == PROCESS_READY){
            int eff = run_queue[i]->priority
                    + (run_queue[i]->foreground ? 1 : 0)
                    + aging_bonus(run_queue[i]);
            if(eff > highest_eff){
                highest_eff = eff;
            }
        }
    }
    if(highest_eff < 0) return idle_pcb;   /* nadie READY: correr idle */

    /* Pasada 1: preferir foreground con prioridad efectiva == highest_eff */
    for(int i = 0; i < queue_size; i++){
        queue_idx = (queue_idx + 1) % queue_size;
        PCB* c = run_queue[queue_idx];
        if(c->state == PROCESS_READY && c->foreground &&
           c->priority + 1 + aging_bonus(c) == highest_eff){
            return c;
        }
    }

    /* Pasada 2: cualquier READY con prioridad efectiva == highest_eff */
    for(int i = 0; i < queue_size; i++){
        queue_idx = (queue_idx + 1) % queue_size;
        PCB* c = run_queue[queue_idx];
        if(c->state == PROCESS_READY){
            int eff = c->priority + (c->foreground ? 1 : 0) + aging_bonus(c);
            if(eff == highest_eff){
                return c;
            }
        }
    }

    return idle_pcb;   /* fallback: nadie READY -> idle */
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

    /* Aging: incrementar la espera de todos los READY salvo el que corre.
       Se hace antes de demote al current para no auto-incrementarlo. */
    for(int i = 0; i < queue_size; i++){
        PCB* p = run_queue[i];
        if(p != cur && p->state == PROCESS_READY){
            p->wait_ticks++;
        }
    }

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

    /* Va a correr: resetear su aging. */
    next->wait_ticks = 0;
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

    /* Va a correr: resetear su aging. */
    next->wait_ticks = 0;
    next->state = PROCESS_RUNNING;
    process_set_current(next);

    return next->rsp;
}
