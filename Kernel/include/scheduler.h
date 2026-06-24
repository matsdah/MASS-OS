#ifndef SCHEDULER_H
#define SCHEDULER_H

#include <stdint.h>
#include "process.h"

/* 
** Definido en scheduler.c, seteado a 1 para solicitar un context switch VOLUNTARIO. 
** Leido desde interrupts.asm en _irq128Handler(syscalls) _irq00Handler(timer tick).
*/ 
extern volatile uint64_t force_switch;

/* Inicializa la cola de ejecución. */
void scheduler_init(void);

/* 
** Arranque: lanza el primer proceso sin retornar 
** para que el Scheduler no termine. 
*/ 
void scheduler_start(void);

/* 
** Definida en interrupts.asm: carga rsp del primer proceso 
** y hace popState+iretq.
*/
void scheduler_start_asm(uint64_t* rsp);

/* Agrega un proceso a la cola de ejecución. */
void scheduler_add(PCB* p);

/* Registra el proceso idle: se devuelve como fallback en scheduler_next_ready
   cuando no hay ningun otro proceso READY. No vive en la run_queue. */
void scheduler_set_idle(PCB* p);

/* Elimina un proceso de la cola de ejecución. */
void scheduler_remove(PCB* p);

/* 
** Elegir el siguiente proceso READY usando round-robin. Solo elige procesos
** en estado READY (excluye explicitamente al RUNNING actual; debe ser marcado
** como READY antes de llamar). Si no hay nadie listo, devuelve NULL. 
*/
PCB* scheduler_next_ready(void);

/* 
** Llamada desde _irq00Handler (timer) 
** Round-Robin con prioridades: el proceso actual sigue corriendo mientras le
** queden quantum (priority quantum consecutivas por turno). Cuando se le agotan,
** pasa a READY y se elige el siguiente de la cola con round-robin.
*/
uint64_t* scheduler_tick(uint64_t* current_rsp);

/* 
** Implementación de yield (pausa). Llamada desde _irq128Handler cuando
** force_switch = 1 (sys_yield, sys_exit, sys_block sobre el proceso actual,
** sys_read sin datos disponibles, etc.). 
*/
uint64_t* scheduler_yield_impl(uint64_t* current_rsp);

#endif
