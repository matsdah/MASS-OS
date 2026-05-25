#include "pipe.h"
#include "process.h"
#include "scheduler.h"
#include "lib.h"

/* Estructura de un pipe. */
typedef struct Pipe {
    uint8_t buffer[PIPE_BUFFER_SIZE];   /* Buffer circular para datos del pipe */
    uint64_t read_idx;          /* Índice de lectura en el buffer */
    uint64_t write_idx;         /* Índice de escritura en el buffer */
    uint64_t count;             /* Número de bytes en el buffer */
    uint64_t open_read;         /* Número de procesos que tienen el pipe abierto para lectura */
    uint64_t open_write;        /* Número de procesos que tienen el pipe abierto para escritura */
    uint64_t wait_readers[MAX_PROCESSES];   /* Cola de procesos esperando para leer del pipe */
    int wait_r_head;            /* Índice del primer proceso en la cola de lectores */
    int wait_r_tail;            /* Índice del último proceso en la cola de lectores */
    int wait_r_count;           /* Número de procesos en la cola de lectores */
    uint64_t wait_writers[MAX_PROCESSES];   /* Cola de procesos esperando para escribir en el pipe */
    int wait_w_head;            /* Índice del primer proceso en la cola de escritores */
    int wait_w_tail;            /* Índice del último proceso en la cola de escritores */
    int wait_w_count;           /* Número de procesos en la cola de escritores */
    int in_use;                 /* Indica si el pipe está en uso */
    int is_named;               /* Indica si el pipe es nombrado */
    int eof;                    /* Indica si el pipe ha recibido un cierre de escritura (EOF) */
    uint64_t opens;             /* Número de veces que el pipe ha sido abierto */
    char name[PIPE_NAME_LEN];
} Pipe;

static Pipe pipe_table[MAX_PIPES];

/* Agrega un proceso a la cola circular (PID). */
static void queue_push(uint64_t* queue, int* tail, int* count, uint64_t pid){
    if(*count >= MAX_PROCESSES){
        return;
    }

    /* Agregar el proceso a la cola. */
    queue[*tail] = pid;
    *tail = (*tail + 1) % MAX_PROCESSES;
    (*count)++;
}

/* Extrae un proceso de la cola circular (PID). */
static uint64_t queue_pop(uint64_t* queue, int* head, int* count){
    uint64_t pid = queue[*head];
    *head = (*head + 1) % MAX_PROCESSES;
    (*count)--;
    return pid;
}

/* Desbloquea a un lector en espera, si existe. */
static void pipe_wake_reader(Pipe* p){

    if(p->wait_r_count > 0){
        uint64_t pid = queue_pop(p->wait_readers, &p->wait_r_head, &p->wait_r_count);
        process_unblock(pid);
    }
}

/* Desbloquea a un escritor en espera, si existe. */
static void pipe_wake_writer(Pipe* p){

    if(p->wait_w_count > 0){
        uint64_t pid = queue_pop(p->wait_writers, &p->wait_w_head, &p->wait_w_count);
        process_unblock(pid);
    }
}

/* Desbloquea a todos los lectores en espera. */
static void pipe_wake_all_readers(Pipe* p){
    while(p->wait_r_count > 0){
        pipe_wake_reader(p);
    }
}

/* Desbloquea a todos los escritores en espera. */
static void pipe_wake_all_writers(Pipe* p){
    while(p->wait_w_count > 0){
        pipe_wake_writer(p);
    }
}

/* Reinicia el estado interno del pipe (buffer, colas, contadores y nombre). */
static void pipe_reset(Pipe* p){
    p->read_idx = 0;
    p->write_idx = 0;
    p->count = 0;
    p->open_read = 0;
    p->open_write = 0;
    p->wait_r_head = 0;
    p->wait_r_tail = 0;
    p->wait_r_count = 0;
    p->wait_w_head = 0;
    p->wait_w_tail = 0;
    p->wait_w_count = 0;
    p->is_named = 0;
    p->eof = 0;
    p->opens = 0;
    p->name[0] = '\0';
}

/* Libera el pipe si no hay lectores ni escritores abiertos. */
static void pipe_maybe_free(Pipe* p){
    if(p->open_read == 0 && p->open_write == 0){
        pipe_reset(p);
        p->in_use = 0;
    }
}

/* Inicializa la tabla global de pipes. */
void pipe_init(void){
    memset(pipe_table, 0, sizeof(pipe_table));
}

/* Reserva un pipe libre de la tabla. */
Pipe* pipe_alloc(void){
    for(int i = 0; i < MAX_PIPES; i++){
        if(!pipe_table[i].in_use){
            Pipe* p = &pipe_table[i];
            pipe_reset(p);
            p->in_use = 1;
            return p;
        }
    }

    return NULL;
}

/* Abre un pipe nombrado existente o crea uno nuevo con ese nombre. */
Pipe* pipe_open_named(const char* name){
    if(name == NULL){
        return NULL;
    }

    for(int i = 0; i < MAX_PIPES; i++){
        /* Buscar un pipe nombrado que coincida con el nombre dado. */
        if(pipe_table[i].in_use && pipe_table[i].is_named){
            int j = 0;

            while(name[j] && pipe_table[i].name[j] && name[j] == pipe_table[i].name[j]){
                j++;
            }

            if(name[j] == '\0' && pipe_table[i].name[j] == '\0'){
                pipe_table[i].opens++;
                return &pipe_table[i];
            }
        }
    }

    /* Si no lo encontre, crear un nuevo pipe nombrado. */
    Pipe* p = pipe_alloc();
    if(p == NULL){
        return NULL;
    }

    int j;
    for(j = 0; j < PIPE_NAME_LEN - 1 && name[j]; j++){
        p->name[j] = name[j];
    }
    p->name[j] = '\0';
    p->is_named = 1;
    p->opens = 1;
    return p;
}

/* Marca que un proceso abrio el extremo de lectura. */
void pipe_open_read(Pipe* p){
    if(p == NULL){
        return;
    }

    p->open_read++;

    /* Si es nombrado y habia escritores esperando, despertarlos. */
    if(p->is_named && p->wait_w_count > 0 && p->open_read > 0){
        pipe_wake_all_writers(p);
    }
}

/* Marca que un proceso abrio el extremo de escritura. */
void pipe_open_write(Pipe* p){
    if(p == NULL){
        return;
    }

    p->open_write++;

    if(p->is_named){
        /* Nuevo escritor: limpiar EOF y despertar lectores. */
        p->eof = 0;

        if(p->wait_r_count > 0){
            pipe_wake_all_readers(p);
        }
    }
}

/* Cierra el extremo de lectura y libera el pipe si corresponde. */
void pipe_close_read(Pipe* p){
    if(p == NULL){
        return;
    }

    if(p->open_read > 0){
        p->open_read--;
    }

    if(p->open_read == 0){
        /* Sin lectores: despertar escritores bloqueados. */
        pipe_wake_all_writers(p);
    }

    pipe_maybe_free(p);
}

/* Cierra el extremo de escritura y maneja EOF en pipes nombrados. */
void pipe_close_write(Pipe* p){
    if(p == NULL){
        return;
    }

    if(p->open_write > 0){
        p->open_write--;
    }

    if(p->open_write == 0){
        if(p->is_named && p->opens >= 2){
            /* EOF solo aplica a pipes nombrados con ambos extremos abiertos. */
            p->eof = 1;
        }
        /* Sin escritores: despertar lectores bloqueados. */
        pipe_wake_all_readers(p);
    }

    pipe_maybe_free(p);
}

/*
 * Lee del pipe con bloqueo.
 * Si no hay datos: bloquea al lector (o devuelve EOF si corresponde).
 * Si hay datos: copia hasta 'count' o hasta vaciar el buffer.
 */
uint64_t pipe_read(Pipe* p, char* buf, uint64_t count){
    if(p == NULL || buf == NULL || count == 0){
        return 0;
    }

    /* Si no hay datos en el buffer. */
    if(p->count == 0){
        if(p->is_named){
            if(p->eof){
                return 0;
            }

            /* Lectura bloqueada */
            PCB* cur = process_current();
            if(cur != NULL){
                /* Encolar lector. */
                queue_push(p->wait_readers, &p->wait_r_tail, &p->wait_r_count, cur->pid);
                /* Bloquear lector. */
                process_block(cur->pid);
            }

            return (uint64_t)-1;

        } else {
            /* Pipe anónimo sin datos. */
            if(p->open_write == 0){
                return 0;
            }

            PCB* cur = process_current();
            if(cur != NULL){
                /* Encolar lector. */
                queue_push(p->wait_readers, &p->wait_r_tail, &p->wait_r_count, cur->pid);
                /* Bloquear lector. */
                process_block(cur->pid);
            }

            return (uint64_t)-1;
        }
    }

    /* Copiar datos del buffer. */
    uint64_t n = 0;
    uint64_t to_read = count;
    if(to_read > p->count){
        to_read = p->count;
    }

    while(n < to_read){
        buf[n++] = p->buffer[p->read_idx];
        p->read_idx = (p->read_idx + 1) % PIPE_BUFFER_SIZE;
        p->count--;
    }

    /* Si hay escritores esperando y hay espacio, despertar a uno. */
    if((p->wait_w_count > 0) && (p->count < PIPE_BUFFER_SIZE)){
        pipe_wake_writer(p);
    }

    return n;
}

/*
 * Escribe en el pipe con bloqueo.
 * Si no hay lectores: en pipes nombrados puede bloquear hasta que aparezcan.
 * Si el buffer esta lleno: bloquea al escritor.
 * Si hay espacio: copia hasta 'count' o hasta llenar el buffer.
 */
uint64_t pipe_write(Pipe *p, const char *buf, uint64_t count){
    if(p == NULL || buf == NULL || count == 0){
        return 0;
    }

    if(p->open_read == 0){
        if(p->is_named){
            if(p->eof){
                return 0;
            }

            PCB* cur = process_current();
            if(cur != NULL){
                /* Encolar escritor. */
                queue_push(p->wait_writers, &p->wait_w_tail, &p->wait_w_count, cur->pid);
                /* Bloquear escritor. */
                process_block(cur->pid);
            }

            return (uint64_t)-1;
        }

        return 0;
    }

    /* Si el buffer esta lleno: bloquear escritor. */
    if(p->count >= PIPE_BUFFER_SIZE){
        PCB* cur = process_current();
        if(cur != NULL){
            /* Encolar escritor. */
            queue_push(p->wait_writers, &p->wait_w_tail, &p->wait_w_count, cur->pid);
            /* Bloquear escritor. */
            process_block(cur->pid);
        }

        return (uint64_t)-1;
    }

    /* Copiar datos al buffer. */
    uint64_t n = 0;
    uint64_t space = PIPE_BUFFER_SIZE - p->count;
    uint64_t to_write = (count < space) ? count : space;

    while(n < to_write){
        p->buffer[p->write_idx] = (uint8_t)buf[n++];
        p->write_idx = (p->write_idx + 1) % PIPE_BUFFER_SIZE;
        p->count++;
    }

    /* Si hay lectores esperando y ahora hay datos, despertar a uno. */
    if(p->wait_r_count > 0 && p->count > 0){
        pipe_wake_reader(p);
    }

    return n;
}
