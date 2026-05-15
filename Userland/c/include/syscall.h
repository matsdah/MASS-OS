#ifndef SYSCALL_H
#define SYSCALL_H

#include <stdint.h>
#include "userlib.h"

/* Compatibilidad con test files del repo que usan malloc/free/memset */
#define malloc(n)   sys_malloc(n)
#define free(p)     sys_free(p)

/* ── Interfaz my_* (espejo del repo de la cátedra) ──────────────────────── */

int64_t my_getpid(void);
int64_t my_create_process(char *name, uint64_t argc, char *argv[]);
int64_t my_create_process_fg(char *name, uint64_t argc, char *argv[], uint8_t fg);
int64_t my_nice(uint64_t pid, uint64_t newPrio);
int64_t my_kill(uint64_t pid);
int64_t my_block(uint64_t pid);
int64_t my_unblock(uint64_t pid);
int64_t my_yield(void);
int64_t my_wait(int64_t pid);

int64_t my_sem_open(char *name, uint64_t initialValue);
int64_t my_sem_wait(char *name);
int64_t my_sem_post(char *name);
int64_t my_sem_close(char *name);

#endif
