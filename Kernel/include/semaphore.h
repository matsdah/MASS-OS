#ifndef SEMAPHORE_H
#define SEMAPHORE_H

#include <stdint.h>

/* Inicializa la tabla global de semáforos. */ 
void sem_init(void);

/* Abre o crea un semáforo identificado por nombre (con valor inicial) */
int64_t sem_open(const char *name, uint64_t initial_value);

/* Decrementa el semáforo; bloquea al proceso si no hay recursos. */
int64_t sem_wait(const char *name);

/* Incrementa el semáforo; despierta un proceso en espera si corresponde. */
int64_t sem_post(const char *name);

/* Cierra un semáforo por nombre y libera la entrada si no quedan usuarios. */
int64_t sem_close(const char *name);

/* Limpia semaforos asociados a un proceso que muere (desde process_kill). */
void sem_cleanup_for_process(uint64_t pid);

#endif
