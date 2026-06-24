#ifndef PROCESS_H
#define PROCESS_H

#include <stdint.h>
#include <stddef.h>

#define MAX_PROCESSES 64
#define STACK_SIZE (4 * 1024)    // 4 KB por proceso
#define MAX_NAME_LEN 32
#define MIN_PRIORITY 1
#define MAX_PRIORITY 5
#define DEFAULT_PRIORITY 3

typedef enum {
    PROCESS_FREE = 0,
    PROCESS_READY = 1,
    PROCESS_RUNNING = 2,
    PROCESS_BLOCKED = 3,
    PROCESS_ZOMBIE  = 4
} ProcessState;

typedef void (*ProcessEntry)(int argc, char **argv);

/* Tabla de procesos para Kernel. */
typedef struct PCB{
    uint64_t pid;               /* PID del proceso */
    char name[MAX_NAME_LEN];    /* Nombre del proceso. */
    ProcessEntry entry;         /* Punto de entrada real del proceso. */
    uint8_t priority;           /* Prioridad del proceso (1-5). */
    uint8_t remaining_quanta;   /* Cuanta restante en el quantum actual. */
    uint32_t wait_ticks;        /* Ticks acumulados esperando CPU (aging). */
    ProcessState state;         /* Estado del proceso. */
    uint64_t *rsp;              /* RSP guardado (valido cuando no esta RUNNING) */
    uint64_t *stack_base;       /* base del stack mm_malloc (para liberar) */
    uint8_t foreground;         /* Indica si el proceso es foreground (1) o background (0). */
    int fd[2];                  /* fd[0]=stdin, fd[1]=stdout */
    uint64_t parent_pid;        /* PID del proceso padre (0 si no tiene padre) */
    uint64_t waiting_for;       /* PID esperado en waitpid (0 = no espera) */   
    int argc;                   /* Cantidad de argumentos (para pasar a la función entry) */
    char **argv;                /* Argumentos (para pasar a la función entry) */
    int retval;                 /* Valor de retorno del proceso (para que el padre lo lea en waitpid) */
    uint32_t held_sems;         /* Bitmap: bit i = proceso tiene recurso de sem_table[i] */
    uint32_t opened_sems;       /* Bitmap: bit i = proceso tiene abierto sem_table[i] (open_count) */
} PCB;

/* Vista userland de un proceso diferenciado de Kernel. */
typedef struct{
    uint64_t pid;
    char name[MAX_NAME_LEN];
    uint8_t priority;
    uint8_t state;
    uint8_t foreground;
    uint64_t rsp;
} ProcessInfo;

void process_init(void);
int process_create(const char *name, ProcessEntry entry, int argc, char **argv, uint8_t fg);
void process_exit(int retval);
PCB* process_get(uint64_t pid);
PCB* process_current(void);
void process_set_current(PCB *p);
void process_kill(uint64_t pid);
void process_kill_foreground(void);
void process_block(uint64_t pid);
void process_unblock(uint64_t pid);
void process_nice(uint64_t pid, uint8_t new_priority);
int process_waitpid(uint64_t pid);
uint64_t process_ps(ProcessInfo *buf, uint64_t max);
void process_pipe_setup(uint64_t pid, int stdio_fd, int target);

#endif
