#include "fd.h"
#include "defs.h"
#include "process.h"
#include "keyboardDriver.h"
#include "videoDriver.h"
#include "scheduler.h"
#include "pipe.h"

#define MAX_FDS (MAX_PROCESSES * 2 + 2)

static FD fd_table[MAX_FDS];

static int fd_alloc(FDType type, void *data){
    for(uint64_t i = 0; i < MAX_FDS; i++){
        if(fd_table[i].type == FD_NONE){
            fd_table[i].type = type;
            fd_table[i].data = data;
            fd_table[i].refcount = 0;
            return (int)i;
        }
    }
    return -1;
}

void fd_init(void){
    for(uint64_t i = 0; i < MAX_FDS; i++){
        fd_table[i].type = FD_NONE;
        fd_table[i].refcount = 0;
        fd_table[i].data = NULL;
    }

    fd_table[FD_STDIN].type = FD_TERMINAL;
    fd_table[FD_STDOUT].type = FD_TERMINAL;
}

FD *fd_get(uint64_t fd){
    if(fd >= MAX_FDS){
        return NULL;
    }
    if(fd_table[fd].type == FD_NONE){
        return NULL;
    }
    return &fd_table[fd];
}

void fd_incref(uint64_t fd){
    FD *d = fd_get(fd);
    if(d == NULL){
        return;
    }
    d->refcount++;
    if(d->type == FD_PIPE_READ){
        pipe_open_read((Pipe *)d->data);
    } else if(d->type == FD_PIPE_WRITE){
        pipe_open_write((Pipe *)d->data);
    }
}

void fd_decref(uint64_t fd){
    FD *d = fd_get(fd);
    if(d == NULL){
        return;
    }
    if(d->refcount > 0){
        d->refcount--;
    }
    if(d->type == FD_PIPE_READ){
        pipe_close_read((Pipe *)d->data);
    } else if(d->type == FD_PIPE_WRITE){
        pipe_close_write((Pipe *)d->data);
    }
    if(d->refcount == 0 && d->type != FD_TERMINAL){
        d->type = FD_NONE;
        d->data = NULL;
    }
}

uint64_t fd_read(FD *d, char *buf, uint64_t count, struct PCB *cur){
    if(d == NULL || buf == NULL || count == 0){
        return 0;
    }

    switch(d->type){
        case FD_TERMINAL: {
            if(cur == NULL || !cur->foreground){
                return 0;
            }
            uint64_t n = readKeyBuff(buf, count);
            if(n == 0){
                kbd_set_waiting(cur);
                cur->state = PROCESS_BLOCKED;
                force_switch = 1;
            }
            return n;
        }
        case FD_PIPE_READ:
            return pipe_read((Pipe *)d->data, buf, count);
        default:
            return 0;
    }
}

uint64_t fd_write(FD *d, const char *buf, uint64_t count, struct PCB *cur){
    (void)cur;
    if(d == NULL || buf == NULL || count == 0){
        return 0;
    }

    switch(d->type){
        case FD_TERMINAL: {
            uint32_t color = 0xFFFFFF;
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
