#ifndef MVAR_H
#define MVAR_H

#include <stdint.h>

/* Inicializa la tabla de MVars (llamado desde kernelMain). */
void mvar_init(void);

/* Crea una MVar nombrada. Estado inicial: EMPTY.
 * Retorna 1 en exito, 0 si ya existe o tabla llena. */
int64_t mvar_create(const char *name);

/* Escribe un valor en la MVar. Si esta FULL, bloquea al escritor.
 * Cuando la MVar pasa a EMPTY, el primer escritor en cola despierta
 * y escribe atomizadamente. */
int64_t mvar_put(const char *name, char value);

/* Lee y consume el valor de la MVar. Si esta EMPTY, bloquea al lector.
 * Retorna el valor leido (0..255). Si la MVar no existe, retorna -1. */
int64_t mvar_take(const char *name);

/* Destruye una MVar nombrada. Despierta a todos los procesos bloqueados
 * en ella (retornan -1). Retorna 1 en exito, 0 si no existe. */
int64_t mvar_destroy(const char *name);

/* Limpia referencias de un proceso que muere.
 * Si el proceso esta bloqueado en una cola de MVar, lo remueve.
 * No altera el estado de la MVar (EMPTY/FULL) porque el proceso nunca
 * llego a completar put/take. */
void mvar_cleanup_for_process(uint64_t pid);

#endif
