#include "fd.h"
#include "defs.h"
#include "process.h"
#include "keyboardDriver.h"
#include "videoDriver.h"
#include "scheduler.h"
#include "pipe.h"
#include "naiveConsole.h"

/* Codigo de retorno para indicar que la lectura bloqueo al proceso. */
#define READ_BLOCKED ((uint64_t)-1)

/* Numero maximo de descriptores de archivo globales.
** Peor caso en donde todos los procesos tienen 2 pipes abiertos. */
#define MAX_FDS (MAX_PROCESSES * 2 + 2)

/* Tabla global de FDs. Cada entrada puede apuntar a terminal o pipe. */
static FD fd_table[MAX_FDS];

/* Busca un slot libre en la tabla y asigna tipo y data. */
static int fd_alloc(FDType type, void* data){
    for(uint64_t i = 0; i < MAX_FDS; i++){
        if(fd_table[i].type == FD_NONE){
            fd_table[i].type = type;
            fd_table[i].data = data;
            fd_table[i].refcount = 0;
            return (int)i;
        }
    }
    
    return -1;  /* No hay slots libres. */
}

/* Inicializa la tabla de FDs y reserva stdin/stdout como terminal. */
void fd_init(void){
    for(uint64_t i = 0; i < MAX_FDS; i++){
        fd_table[i].type = FD_NONE;
        fd_table[i].refcount = 0;
        fd_table[i].data = NULL;
    }

    /* Reservar stdin y stdout como terminales. */
    fd_table[FD_STDIN].type = FD_TERMINAL;
    fd_table[FD_STDOUT].type = FD_TERMINAL;
}

/* Devuelve el FD si existe y es valido; NULL si no. */
FD* fd_get(uint64_t fd){
    if(fd >= MAX_FDS){
        return NULL;
    }

    if(fd_table[fd].type == FD_NONE){
        return NULL;
    }

    return &fd_table[fd];
}

/* Incrementa refcount y notifica a la implementacion del pipe. */
void fd_incref(uint64_t fd){

    FD* d = fd_get(fd);

    if(d == NULL){
        return;
    }

    /* Incrementar el contador de referencias. */
    d->refcount++;

    if(d->type == FD_PIPE_READ){
        pipe_open_read((Pipe *)d->data);
    } else{
        if(d->type == FD_PIPE_WRITE){
            pipe_open_write((Pipe *)d->data);
        }
    }
}

/* Decrementa refcount, notifica al pipe y libera slot si corresponde. */
void fd_decref(uint64_t fd){
    FD* d = fd_get(fd);
    
    if(d == NULL){
        return;
    }
    
    if(d->refcount > 0){
        d->refcount--;
        if(d->type == FD_PIPE_READ){
            pipe_close_read((Pipe *)d->data);
        } else if(d->type == FD_PIPE_WRITE){
            pipe_close_write((Pipe *)d->data);
        }
    }

    /* Liberar slot si no hay referencias y no es un terminal. */
    if((d->refcount == 0) && (d->type != FD_TERMINAL)){
        d->type = FD_NONE;
        d->data = NULL;
    }
}

/*
 * Lee desde un FD.
 * Terminal: solo si el proceso es foreground; si no hay input, bloquea.
 * Pipe: delega en pipe_read.
 */
uint64_t fd_read(FD* d, char* buf, uint64_t count, struct PCB* cur){

    if(d == NULL || buf == NULL || count == 0){
        return 0;
    }

    switch(d->type){
        case FD_TERMINAL: {
            if(cur == NULL || !cur->foreground){
                return 0;
            }

            /* Modo cooked: entregar lineas completas */
            if(tty_get_mode() == TTY_COOKED){
                if(tty_eof && tty_line_len == 0){
                    tty_eof = 0;
                    return 0;
                }
                if(tty_line_ready || tty_eof){
                    uint64_t to_copy = (uint64_t)tty_line_len;
                    if(to_copy > count){
                        to_copy = count;
                    }
                    for(uint64_t i = 0; i < to_copy; i++){
                        buf[i] = tty_line[i];
                    }
                    if(to_copy == (uint64_t)tty_line_len){
                        tty_line_len = 0;
                        tty_line_ready = 0;
                        tty_eof = 0;
                    } else {
                        for(int i = 0; i < tty_line_len - (int)to_copy; i++){
                            tty_line[i] = tty_line[i + (int)to_copy];
                        }
                        tty_line_len -= (int)to_copy;
                    }
                    return to_copy;
                }
                /* No hay linea lista: bloquear */
                kbd_set_waiting(cur);
                cur->state = PROCESS_BLOCKED;
                force_switch = 1;
                return READ_BLOCKED;
            }

            /* Modo raw: leer del buffer de teclado */
            uint64_t n = readKeyBuff(buf, count);

            /* EOF: byte 0x04 enviado por Ctrl+D */
            if(n > 0 && buf[0] == 0x04){
                return 0;
            }

            /* Lectura bloqueante. */
            if(n == 0){
                kbd_set_waiting(cur);
                cur->state = PROCESS_BLOCKED;
                force_switch = 1;
                return READ_BLOCKED;
            }

            return n;
        }

        case FD_PIPE_READ:
            return pipe_read((Pipe *)d->data, buf, count);
        default:
            return 0;
    }
}

/*
 * Escribe en un FD.
 * Terminal: imprime cada caracter.
 * Pipe: delega en pipe_write.
 */
uint64_t fd_write(FD* d, const char* buf, uint64_t count, struct PCB* cur, uint32_t color){
    (void)cur;

    if(d == NULL || buf == NULL || count == 0){
        return 0;
    }

    switch(d->type){
        case FD_TERMINAL: {
            for(uint64_t i = 0; i < count; i++){
                videoPutChar((uint8_t)buf[i], color);
            }

            return count;
        }

        case FD_PIPE_WRITE:
            return pipe_write((Pipe *)d->data, buf, count);
        default:
            return 0;
    }
}

/*
 * Crea un FD asociado a un pipe ya existente.
 * write_end=1 crea FD de escritura; 0 crea FD de lectura.
 */
int fd_create_pipe(struct Pipe *p, int write_end){
    if(p == NULL){
        return -1;
    }

    FDType type = write_end ? FD_PIPE_WRITE : FD_PIPE_READ;
    int fd = fd_alloc(type, p);
    
    if(fd < 0){
        return -1;
    }

    fd_incref((uint64_t)fd);
    return fd;
}
