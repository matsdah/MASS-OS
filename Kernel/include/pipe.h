#ifndef PIPE_H
#define PIPE_H

#include <stdint.h>

#define PIPE_BUFFER_SIZE 4096
#define MAX_PIPES 32

typedef struct Pipe Pipe;

void pipe_init(void);
Pipe *pipe_alloc(void);
uint64_t pipe_read(Pipe *p, char *buf, uint64_t count);
uint64_t pipe_write(Pipe *p, const char *buf, uint64_t count);
void pipe_open_read(Pipe *p);
void pipe_open_write(Pipe *p);
void pipe_close_read(Pipe *p);
void pipe_close_write(Pipe *p);

#endif
