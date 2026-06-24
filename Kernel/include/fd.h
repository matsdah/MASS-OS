#ifndef FD_H
#define FD_H

#include <stdint.h>

#define FD_STDIN  0
#define FD_STDOUT 1

typedef enum {
    FD_NONE = 0,
    FD_TERMINAL = 1,
    FD_PIPE_READ = 2,
    FD_PIPE_WRITE = 3
} FDType;

typedef struct FD {
    FDType type;
    uint64_t refcount;
    void *data;
} FD;

struct PCB;
struct Pipe;

void fd_init(void);
FD *fd_get(uint64_t fd);
void fd_incref(uint64_t fd);
void fd_decref(uint64_t fd);
uint64_t fd_read(FD *d, char *buf, uint64_t count, struct PCB *cur);
uint64_t fd_write(FD *d, const char *buf, uint64_t count, struct PCB *cur, uint32_t color);
int fd_create_pipe(struct Pipe *p, int write_end);

#endif
