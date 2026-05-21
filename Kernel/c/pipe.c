#include "pipe.h"
#include "process.h"
#include "scheduler.h"
#include "lib.h"

typedef struct Pipe {
    uint8_t buffer[PIPE_BUFFER_SIZE];
    uint64_t read_idx;
    uint64_t write_idx;
    uint64_t count;
    uint64_t open_read;
    uint64_t open_write;
    uint64_t wait_readers[MAX_PROCESSES];
    int wait_r_head;
    int wait_r_tail;
    int wait_r_count;
    uint64_t wait_writers[MAX_PROCESSES];
    int wait_w_head;
    int wait_w_tail;
    int wait_w_count;
    int in_use;
} Pipe;

static Pipe pipe_table[MAX_PIPES];

static void queue_push(uint64_t *queue, int *tail, int *count, uint64_t pid){
    if(*count >= MAX_PROCESSES){
        return;
    }
    queue[*tail] = pid;
    *tail = (*tail + 1) % MAX_PROCESSES;
    (*count)++;
}

static uint64_t queue_pop(uint64_t *queue, int *head, int *count){
    uint64_t pid = queue[*head];
    *head = (*head + 1) % MAX_PROCESSES;
    (*count)--;
    return pid;
}

static void pipe_wake_reader(Pipe *p){
    if(p->wait_r_count > 0){
        uint64_t pid = queue_pop(p->wait_readers, &p->wait_r_head, &p->wait_r_count);
        process_unblock(pid);
    }
}

static void pipe_wake_writer(Pipe *p){
    if(p->wait_w_count > 0){
        uint64_t pid = queue_pop(p->wait_writers, &p->wait_w_head, &p->wait_w_count);
        process_unblock(pid);
    }
}

static void pipe_wake_all_readers(Pipe *p){
    while(p->wait_r_count > 0){
        pipe_wake_reader(p);
    }
}

static void pipe_wake_all_writers(Pipe *p){
    while(p->wait_w_count > 0){
        pipe_wake_writer(p);
    }
}

static void pipe_reset(Pipe *p){
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
}

static void pipe_maybe_free(Pipe *p){
    if(p->open_read == 0 && p->open_write == 0){
        pipe_reset(p);
        p->in_use = 0;
    }
}

void pipe_init(void){
    memset(pipe_table, 0, sizeof(pipe_table));
}

Pipe *pipe_alloc(void){
    for(int i = 0; i < MAX_PIPES; i++){
        if(!pipe_table[i].in_use){
            Pipe *p = &pipe_table[i];
            pipe_reset(p);
            p->in_use = 1;
            return p;
        }
    }
    return NULL;
}

void pipe_open_read(Pipe *p){
    if(p == NULL){
        return;
    }
    p->open_read++;
}

void pipe_open_write(Pipe *p){
    if(p == NULL){
        return;
    }
    p->open_write++;
}

void pipe_close_read(Pipe *p){
    if(p == NULL){
        return;
    }
    if(p->open_read > 0){
        p->open_read--;
    }
    if(p->open_read == 0){
        pipe_wake_all_writers(p);
    }
    pipe_maybe_free(p);
}

void pipe_close_write(Pipe *p){
    if(p == NULL){
        return;
    }
    if(p->open_write > 0){
        p->open_write--;
    }
    if(p->open_write == 0){
        pipe_wake_all_readers(p);
    }
    pipe_maybe_free(p);
}

uint64_t pipe_read(Pipe *p, char *buf, uint64_t count){
    if(p == NULL || buf == NULL || count == 0){
        return 0;
    }

    if(p->count == 0){
        if(p->open_write == 0){
            return 0; /* EOF */
        }
        PCB *cur = process_current();
        if(cur != NULL){
            queue_push(p->wait_readers, &p->wait_r_tail, &p->wait_r_count, cur->pid);
            process_block(cur->pid);
        }
        return 0;
    }

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

    if(p->wait_w_count > 0 && p->count < PIPE_BUFFER_SIZE){
        pipe_wake_writer(p);
    }

    return n;
}

uint64_t pipe_write(Pipe *p, const char *buf, uint64_t count){
    if(p == NULL || buf == NULL || count == 0){
        return 0;
    }

    if(p->open_read == 0){
        return 0;
    }

    if(p->count >= PIPE_BUFFER_SIZE){
        PCB *cur = process_current();
        if(cur != NULL){
            queue_push(p->wait_writers, &p->wait_w_tail, &p->wait_w_count, cur->pid);
            process_block(cur->pid);
        }
        return 0;
    }

    uint64_t n = 0;
    uint64_t space = PIPE_BUFFER_SIZE - p->count;
    uint64_t to_write = (count < space) ? count : space;

    while(n < to_write){
        p->buffer[p->write_idx] = (uint8_t)buf[n++];
        p->write_idx = (p->write_idx + 1) % PIPE_BUFFER_SIZE;
        p->count++;
    }

    if(p->wait_r_count > 0 && p->count > 0){
        pipe_wake_reader(p);
    }

    return n;
}
