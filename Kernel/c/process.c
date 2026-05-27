#include "process.h"
#include "scheduler.h"
#include "memoryManager.h"
#include "lib.h"
#include "interrupts.h"
#include "fd.h"
#include <stddef.h>

/* Tabla de procesos del Kernel. */
static PCB process_table[MAX_PROCESSES];

static uint64_t next_pid = 0;
static PCB* current_process = NULL;

// ─── helpers ─────────────────────────────────────────────────────────────────

static int str_len(const char *s){

    int n = 0;
    while(s && s[n]){ 
        n++;
    }

    return n;
}

static void str_copy(char *dst, const char *src, int max){
    int i = 0;
    
    while(i < max - 1 && src && src[i]){ 
        dst[i] = src[i];
        i++; 
    }
    
    dst[i] = '\0';
}

static void process_release_fds(PCB *p){
    if(p == NULL){
        return;
    }
    if(p->fd[0] >= 0){
        fd_decref((uint64_t)p->fd[0]);
    }
    if(p->fd[1] >= 0){
        fd_decref((uint64_t)p->fd[1]);
    }
    p->fd[0] = -1;
    p->fd[1] = -1;
}

// ─── stack frame inicial ──────────────────────────────────────────────────────
//
// Layout del stack tras una interrupcion en long mode. La CPU pushea SIEMPRE
// 5 qwords (SS, RSP, RFLAGS, CS, RIP), incluso sin cambio de privilegio:
//
//   [rsp+19*8] SS              <- empujado por CPU (parte alta del frame)
//   [rsp+18*8] RSP
//   [rsp+17*8] RFLAGS
//   [rsp+16*8] CS
//   [rsp+15*8] RIP
//   [rsp+14*8] RAX  (primero en pushState)
//   [rsp+13*8] RBX
//   ...
//   [rsp+ 0*8] R15  (ultimo en pushState, primer pop)
//
// Para que popState+iretq arranquen un proceso nuevo, construimos exactamente
// este layout desde la cima del stack hacia abajo.


/* 
** Construye el frame inicial del stack. 
** Prepara el mecanismo para el context switching.
*/
static void process_trampoline(int argc, char **argv){
    PCB* cur = process_current();
    if(cur != NULL && cur->entry != NULL){
        cur->entry(argc, argv);
    }

    /* Si el entry retorna, salir via syscall para forzar cambio de contexto. */
    __asm__ __volatile__(
        "movq $20, %%rax\n"
        "movq $0, %%rdi\n"
        "int $0x80\n"
        :
        :
        : "rax", "rdi"
    );

    while(1){
        _hlt();
    }
}

static uint64_t* build_initial_stack(void *stack_top, ProcessEntry entry, int argc, char **argv){
    uint64_t* sp = (uint64_t *)stack_top;

    /* Hardware frame. En long mode (IA-32e), iretq SIEMPRE pop-ea las 5 qwords
    ** (SS, RSP, RFLAGS, CS, RIP) sin importar si hay cambio de privilegio. */
    *(--sp) = 0;                   /* SS */
    *(--sp) = (uint64_t)stack_top; /* RSP: tope del stack del proceso */
    *(--sp) = 0x202;               /* RFLAGS: Interrupt Flag = 1 */
    *(--sp) = 0x08;                /* CS: segmento de codigo del kernel */
    *(--sp) = (uint64_t)entry;     /* RIP: punto de entrada */
    

    /* Registros General Purpose en orden de pushState */
    *(--sp) = 0;                   /* RAX */
    *(--sp) = 0;                   /* RBX */
    *(--sp) = 0;                   /* RCX */
    *(--sp) = 0;                   /* RDX */
    *(--sp) = 0;                   /* RBP */
    *(--sp) = (uint64_t)argc;      /* RDI → arg1 de la funcion de entrada */
    *(--sp) = (uint64_t)argv;      /* RSI → arg2 de la funcion de entrada */
    *(--sp) = 0;                   /* R8 */
    *(--sp) = 0;                   /* R9 */
    *(--sp) = 0;                   /* R10 */
    *(--sp) = 0;                   /* R11 */
    *(--sp) = 0;                   /* R12 */
    *(--sp) = 0;                   /* R13 */
    *(--sp) = 0;                   /* R14 */
    *(--sp) = 0;                   /* R15 (ultimo en pushState → primer pop) */

    return sp;   /* PCB.rsp apunta aqui*/ 
}

// ─── API publica ──────────────────────────────────────────────────────────────

/* Inicializa la tabla de procesos. */
void process_init(void){
    memset(process_table, 0, sizeof(process_table));
    next_pid = 0;
    current_process = NULL;
}

/* Devuelve el proceso actual. */
PCB* process_current(void){
    return current_process;
}

/* Establece el proceso actual. */
void process_set_current(PCB* p){
    current_process = p;
}

/* Devuelve el proceso con el PID especificado. */
PCB* process_get(uint64_t pid){

    if(pid == 0){
        return NULL;
    }

    for(int i = 0; i < MAX_PROCESSES; i++){
        if(process_table[i].pid == pid && process_table[i].state != PROCESS_FREE){
            return &process_table[i];
        }
    }

    return NULL;
}

/* Crea un nuevo proceso. */
int process_create(const char *name, ProcessEntry entry, int argc, char **argv, uint8_t fg){

    /* Buscar slot libre. */ 
    int slot = -1;
    for(int i = 0; i < MAX_PROCESSES && slot == -1; i++){
        if(process_table[i].state == PROCESS_FREE){
            slot = i;
        }
    }
    
    if(slot < 0){
        return -1;
    }

    PCB* p = &process_table[slot];

    /* Asignar stack (kernel-internal: no se contabiliza en alloc_count del usuario)
    ** para diferenciarlo del stack del usuario. */
    p->stack_base = (uint64_t *)mm_malloc_kernel(STACK_SIZE);
    if(p->stack_base == NULL){
        return -1;
    }

    /* Copiar argv y sus strings a memoria kernel persistente.
    ** argv original puede apuntar al stack del padre (shell), que se
    ** sobreescribe cuando processLine retorna. */
    char **argv_copy = NULL;
    if(argv != NULL && argc > 0){
        /* Calcular tamaño total: array de punteros + strings */
        uint64_t total_size = sizeof(char *) * (uint64_t)(argc + 1);
        for(int i = 0; i < argc; i++){
            if(argv[i] != NULL){
                total_size += (uint64_t)str_len(argv[i]) + 1;
            }
        }
        
        argv_copy = (char **)mm_malloc_kernel(total_size);
        if(argv_copy == NULL){
            mm_free(p->stack_base);
            return -1;
        }
        
        /* Copiar strings al final del bloque, luego armar punteros */
        uint8_t *str_pool = (uint8_t *)argv_copy + sizeof(char *) * (uint64_t)(argc + 1);
        for(int i = 0; i < argc; i++){
            if(argv[i] != NULL){
                int len = str_len(argv[i]) + 1;
                memcpy(str_pool, argv[i], (uint64_t)len);
                argv_copy[i] = (char *)str_pool;
                str_pool += (uint64_t)len;
            } else {
                argv_copy[i] = NULL;
            }
        }
        argv_copy[argc] = NULL;
    }

    /* Construir frame inicial. */ 
    void* stack_top = (void*)((uint8_t *)p->stack_base + STACK_SIZE);
    p->rsp = build_initial_stack(stack_top, process_trampoline, argc, (char **)argv_copy);

    /* Rellenar PCB */ 
    p->pid = ++next_pid;
    p->priority = DEFAULT_PRIORITY;
    p->remaining_quanta = DEFAULT_PRIORITY;
    p->state = (fg & 2) ? PROCESS_BLOCKED : PROCESS_READY;
    p->foreground = fg;     /* flag para indicar si el proceso es de primer plano. */
    p->fd[0] = FD_STDIN;
    p->fd[1] = FD_STDOUT;
    fd_incref((uint64_t)p->fd[0]);
    fd_incref((uint64_t)p->fd[1]);
    p->parent_pid = ((current_process != NULL) ? (current_process->pid) : 0);
    p->waiting_for = 0;     /* Indica si se esta esperando a otro proceso. Sirve para que el scheduler sepa cuando desbloquearlo. */
    p->argc = argc;         
    p->argv = argv_copy;    /* Memoria kernel (se libera en process_exit/kill) */
    p->retval = 0;
    p->entry = entry;

    str_copy(p->name, name, MAX_NAME_LEN);
    
    if(str_len(p->name) == 0){
        p->name[0] = '?';
        p->name[1] = '\0';
    }

    scheduler_add(p);       /* Agregar el proceso a la tabla de procesos listos para ejecutar. */
    return (int)p->pid; 
}

/* Finaliza el proceso actual. */
void process_exit(int retval){
    if(current_process == NULL){
        return;
    }

    current_process->retval = retval;

    /* El proceso finalizo su ejecucion pero no ha sido removido de la tabla de procesos.*/
    current_process->state = PROCESS_ZOMBIE;
    //zerebrozzzz
    process_release_fds(current_process);

    /* Despertar al padre si esta bloqueado en waitpid esperando este proceso. */
    PCB* parent = process_get(current_process->parent_pid);
    if(parent != NULL && parent->state == PROCESS_BLOCKED && parent->waiting_for == current_process->pid){

        /* Escribir el retval directamente en el RAX guardado del padre. */ 
        /* parent->rsp apunta al slot R15; el slot RAX esta 14 qwords mas arriba. */ 
        parent->rsp[14] = (uint64_t)(int64_t)retval;
        parent->waiting_for = 0;
        parent->state = PROCESS_READY;

        /* Reap inmediato: el padre ya recibio el retval, no necesitamos ZOMBIE. */
        current_process->state = PROCESS_FREE;
        current_process->pid = 0;
    }

    /* Liberar stack y argv del proceso que muere. */ 
    mm_free(current_process->stack_base);
    current_process->stack_base = NULL;
    if(current_process->argv != NULL){
        mm_free(current_process->argv);
        current_process->argv = NULL;
    }
    current_process->rsp = NULL;

    force_switch = 1;   /* Ceder CPU en el proximo retorno de syscall. */
}

void process_kill(uint64_t pid){
    PCB* p = process_get(pid);
    if(p == NULL || p->state == PROCESS_FREE || p->state == PROCESS_ZOMBIE){
        return;
    }

    p->retval = -1;

    /* Despertar padre si estaba esperando este proceso. */
    PCB *parent = process_get(p->parent_pid);
    if(parent != NULL && parent->state == PROCESS_BLOCKED && parent->waiting_for == pid){
        /* Escribir el retval directamente en el RAX guardado del padre. 
        ** El valor -1 indica que el proceso fue matado. */ 
        parent->rsp[14] = (uint64_t)(uint32_t)(-1); // ?¿
        parent->waiting_for = 0;
        parent->state = PROCESS_READY;
    }

    if(p == current_process){
        /* Se quiere matar el mismo. */
        current_process->state = PROCESS_ZOMBIE;
        process_release_fds(current_process);
        mm_free(current_process->stack_base);
        current_process->stack_base = NULL;
        if(current_process->argv != NULL){
            mm_free(current_process->argv);
            current_process->argv = NULL;
        }
        current_process->rsp = NULL;
        /* Fuerza el switch para seguir con otro proceso. */
        force_switch = 1;       
    }else{
        /* Matar otro proceso: liberar recursos inmediatamente y removerlo del scheduler. 
        ** No es necesario marcarlo como PROCESS_ZOMBIE porque el proceso actual no esta esperando por el.
        ** Entonces no hay riesgo si hago scheduler_remove y libero el slot del proceso.
        */ 
        scheduler_remove(p);
        process_release_fds(p);
        if(p->stack_base != NULL){
            mm_free(p->stack_base);
            p->stack_base = NULL;
        }
        if(p->argv != NULL){
            mm_free(p->argv);
            p->argv = NULL;
        }
        p->state = PROCESS_FREE;
        p->pid = 0;
    }
}

/* Alterna el estado del proceso entre BLOCKED y READY.
   No permite desbloquear un proceso que esta bloqueado internamente
   por una syscall (ej. waitpid), para no romper la sincronizacion. */
void process_block(uint64_t pid){
    PCB* p = process_get(pid);

    if(p == NULL || p->state == PROCESS_FREE || p->state == PROCESS_ZOMBIE){
        return;
    }

    if(p->state == PROCESS_BLOCKED && p->waiting_for != 0){
        /* Proceso bloqueado en waitpid: ignorar toggle externo. */
        return;
    }

    if(p->state == PROCESS_BLOCKED){
        p->state = PROCESS_READY;
    } else {
        p->state = PROCESS_BLOCKED;
        if(p == current_process){
            force_switch = 1;
        }
    }
}

/* Desbloquea el proceso con el PID especificado. */
void process_unblock(uint64_t pid){
    PCB* p = process_get(pid);

    if(p == NULL || p->state != PROCESS_BLOCKED){
        return;
    }

    p->state = PROCESS_READY;
}

/* Cambia la prioridad del proceso con el PID especificado. */
void process_nice(uint64_t pid, uint8_t new_priority){
    PCB* p = process_get(pid);

    if(p == NULL){
        return;
    }
    
    if(new_priority < MIN_PRIORITY){
        new_priority = MIN_PRIORITY;
    }
    
    if(new_priority > MAX_PRIORITY){
        new_priority = MAX_PRIORITY;
    }
    
    p->priority = new_priority;

    /* Actualizar remaining_quanta incondicionalmente: si se sube la prioridad el
       proceso recibe un quantum completo en el proximo tick; si se baja, se corta
       el quantum actual para que no supere la nueva prioridad. */
    p->remaining_quanta = new_priority;
}

/* Espera a que un proceso hijo termine. */
int process_waitpid(uint64_t pid){
    PCB* child = process_get(pid);
    if(child == NULL){
        return -1;
    }

    if(child->state == PROCESS_ZOMBIE){
        /* Proceso hijo ya termino. */

        int retval = child->retval;

        /* Reclama el proceso de la tabla de procesos. */
        child->state = PROCESS_FREE;
        child->pid = 0;

        return retval;
    }

    if(current_process != NULL){
        /* Hijo todavia vivo: bloquear y esperar */
        current_process->state = PROCESS_BLOCKED;
        current_process->waiting_for = pid;
    }

    force_switch = 1;
    // El retval real sera escrito por process_exit en rsp[14].
    return 0;
}

/* Obtiene información sobre los procesos en ejecución. */
uint64_t process_ps(ProcessInfo *buf, uint64_t max){
    uint64_t count = 0;

    for(int i = 0; i < MAX_PROCESSES && count < max; i++){
        PCB* p = &process_table[i];

        if(p->state != PROCESS_FREE){
            buf[count].pid = p->pid;
            buf[count].priority = p->priority;
            buf[count].state = (uint8_t)p->state;
            buf[count].foreground = p->foreground;
            buf[count].rsp = (uint64_t)p->rsp;
            str_copy(buf[count].name, p->name, MAX_NAME_LEN);
            count++;
        }
    }

    return count;
}

/* Redirige stdin/stdout de un proceso hijo antes de que arranque.
   Lo usa la shell para conectar comandos con pipes. */
void process_pipe_setup(uint64_t pid, int stdio_fd, int target){
    PCB *child = process_get(pid);
    if(child == NULL || target > 1){
        return;
    }

    if(child->fd[target] >= 0){
        fd_decref((uint64_t)child->fd[target]);
    }

    child->fd[target] = stdio_fd;
    fd_incref((uint64_t)stdio_fd);
}
