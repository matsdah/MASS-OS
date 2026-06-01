#ifndef SYSCALLDISPATCHER_H
#define SYSCALLDISPATCHER_H

#include <stdint.h>
#include "defs.h"

extern void * syscalls[CANT_SYS];

// Syscalls 0-18 (existentes)
uint64_t sys_write(uint64_t fd, const char * buff, uint64_t count);
uint64_t sys_read(uint64_t fd, char * buff, uint64_t count);
uint64_t sys_registers(char * buff);
void sys_time(uint8_t * buff);
void sys_date(uint8_t * buff);
void sys_decrease_fontsize(void);
void sys_increase_fontsize(void);
void sys_beep(uint32_t freq, uint64_t time);
uint64_t sys_ticks(void);
void sys_clear(void);
uint64_t sys_screen_width(void);
uint64_t sys_screen_height(void);
void sys_putpixel(uint32_t color, uint64_t x, uint64_t y);
void sys_fill_rect(uint64_t x, uint64_t y, uint64_t w, uint64_t h, uint32_t color);
void sys_speaker_start(uint32_t freq);
void sys_speaker_off(void);
uint64_t sys_malloc(uint64_t size);
void sys_free(uint64_t ptr);
void sys_mem_status(uint64_t statusPtr);

// Syscalls 19-28 (procesos)
int64_t  sys_create_process(uint64_t name, uint64_t entry,
                            uint64_t argc, uint64_t argv, uint64_t fg);
void     sys_exit(uint64_t retval);
uint64_t sys_getpid(void);
uint64_t sys_ps(uint64_t buffer, uint64_t max_count);
void     sys_kill(uint64_t pid);
void     sys_nice(uint64_t pid, uint64_t new_priority);
int64_t  sys_block(uint64_t pid);
void     sys_unblock(uint64_t pid);
void     sys_yield(void);
int64_t  sys_waitpid(uint64_t pid);

// Syscalls 29-32 (semáforos)
int64_t  sys_sem_open(const char *name, uint64_t initial_value);
int64_t  sys_sem_wait(const char *name);
int64_t  sys_sem_post(const char *name);
int64_t  sys_sem_close(const char *name);

// Syscalls 33-35 (pipes)
int64_t  sys_pipe(uint64_t fd_array);
int64_t  sys_dup2(uint64_t old_fd, uint64_t new_fd);
int64_t  sys_close(uint64_t fd);

// Syscall 36 (named pipe)
int64_t  sys_pipe_open(uint64_t name, uint64_t fd_array);

// Syscall 37 (pipe setup para pipes entre comandos)
int64_t  sys_pipe_setup(uint64_t pid, uint64_t stdio_fd, uint64_t target);

// Syscall 38 (modo de terminal)
int64_t  sys_tty_mode(uint64_t mode);

// Syscall 39 (escritura con color)
uint64_t sys_write_color(uint64_t fd, const char *buff, uint64_t count, uint32_t color);

// Syscalls 40-43 (MVar nativas)
int64_t sys_mvar_create(const char *name);
int64_t sys_mvar_put(const char *name, char value);
int64_t sys_mvar_take(const char *name);
int64_t sys_mvar_destroy(const char *name);

#endif
