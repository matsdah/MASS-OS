#include "syscallDispatcher.h"
#include "videoDriver.h"
#include "keyboardDriver.h"
#include "lib.h"
#include "defs.h"
#include "time.h"
#include "sound.h"
#include "memoryManager.h"
#include "process.h"
#include "scheduler.h"
#include "semaphore.h"
#include "fd.h"
#include "pipe.h"

// ─── Syscalls de memoria (16-18) ──────────────────────────────────────────────

uint64_t sys_malloc(uint64_t size) {
    return (uint64_t)mm_malloc(size);
}

void sys_free(uint64_t ptr) {
    mm_free((void *)ptr);
}

void sys_mem_status(uint64_t statusPtr) {
    mm_status((MemStatus *)statusPtr);
}

// ─── Syscalls de video/teclado ────────────────────────────────────────────────

void sys_increase_fontsize(void){
    increaseFontSize();
}
void sys_decrease_fontsize(void){
    decreaseFontSize();
}

void sys_clear(void){
    clearScreen(0x000000);
}

uint64_t sys_write(uint64_t fd, const char * buff, uint64_t count){
    PCB *cur = process_current();
    if(cur == NULL || buff == NULL || count == 0){
        return 0;
    }

    int real_fd;
    if(fd <= FD_STDOUT){
        real_fd = cur->fd[fd];
        if(real_fd < 0){
            return 0;
        }
    } else {
        real_fd = (int)fd;
    }

    FD *d = fd_get((uint64_t)real_fd);
    return fd_write(d, buff, count, cur);
}

uint64_t sys_read(uint64_t fd, char * buff, uint64_t count){
    PCB *cur = process_current();
    if(cur == NULL || buff == NULL || count == 0){
        return 0;
    }

    int real_fd;
    if(fd <= FD_STDIN){
        real_fd = cur->fd[fd];
        if(real_fd < 0){
            return 0;
        }
    } else {
        real_fd = (int)fd;
    }

    FD *d = fd_get((uint64_t)real_fd);
    return fd_read(d, buff, count, cur);
}

void sys_date(uint8_t * buff){
    date(buff);
}

void sys_time(uint8_t * buff){
    time(buff);
}

uint64_t sys_registers(char * buff){
    return copyRegistersBuffer(buff);
}

void sys_beep(uint32_t freq, uint64_t ticks){
    beep(freq, ticks);
}

uint64_t sys_ticks(void){
    return deltaTicks();
}

void sys_speaker_start(uint32_t freq){
    startSpeaker(freq);
}

void sys_speaker_off(void){
    turnOff();
}

uint64_t sys_screen_width(void){
    return (uint64_t)getScreenWidth();
}

uint64_t sys_screen_height(void){
    return (uint64_t)getScreenHeight();
}

void sys_putpixel(uint32_t color, uint64_t x, uint64_t y){
    putPixel(color, x, y);
}

void sys_fill_rect(uint64_t x, uint64_t y, uint64_t w, uint64_t h, uint32_t color){
    fillRect(x, y, w, h, color);
}

// ─── Syscalls de procesos (19-28) ─────────────────────────────────────────────

int64_t sys_create_process(uint64_t name, uint64_t entry,
                           uint64_t argc, uint64_t argv, uint64_t fg) {
    return (int64_t)process_create((const char *)name,
                                   (ProcessEntry)entry,
                                   (int)argc,
                                   (char **)argv,
                                   (uint8_t)fg);
}

void sys_exit(uint64_t retval) {
    process_exit((int)retval);
}

uint64_t sys_getpid(void) {
    PCB *cur = process_current();
    return (cur != NULL) ? cur->pid : 0;
}

uint64_t sys_ps(uint64_t buffer, uint64_t max_count) {
    return process_ps((ProcessInfo *)buffer, max_count);
}

void sys_kill(uint64_t pid) {
    process_kill(pid);
}

void sys_nice(uint64_t pid, uint64_t new_priority) {
    process_nice(pid, (uint8_t)new_priority);
}

int64_t sys_block(uint64_t pid) {
    PCB* p = process_get(pid);
    if(p == NULL || p->state == PROCESS_FREE || p->state == PROCESS_ZOMBIE){
        return -1;
    }
    if(p->state == PROCESS_BLOCKED && p->waiting_for != 0){
        return -1;
    }
    process_block(pid);
    return 0;
}

void sys_unblock(uint64_t pid) {
    process_unblock(pid);
}

void sys_yield(void) {
    PCB *cur = process_current();
    if (cur != NULL) {
        cur->state = PROCESS_READY;
        force_switch = 1;
    }
}

int64_t sys_waitpid(uint64_t pid) {
    return (int64_t)process_waitpid(pid);
}

int64_t sys_sem_open(const char *name, uint64_t initial_value) {
    return sem_open(name, initial_value);
}

int64_t sys_sem_wait(const char *name) {
    return sem_wait(name);
}

int64_t sys_sem_post(const char *name) {
    return sem_post(name);
}

int64_t sys_sem_close(const char *name) {
    return sem_close(name);
}

/* Crea un par de FDs (lectura + escritura) para un pipe y los guarda en fds[].
   El pipe ya debe estar reservado via pipe_open. */
static int64_t pipe_create_fds(Pipe *p, int *fds){
    int read_fd = fd_create_pipe(p, 0);
    if(read_fd < 0){
        return -1;
    }

    int write_fd = fd_create_pipe(p, 1);
    if(write_fd < 0){
        fd_decref((uint64_t)read_fd);
        return -1;
    }

    fds[0] = read_fd;
    fds[1] = write_fd;
    return 0;
}

int64_t sys_pipe(uint64_t fd_array){
    if(fd_array == 0){
        return -1;
    }

    Pipe *p = pipe_open(NULL);
    if(p == NULL){
        return -1;
    }

    return pipe_create_fds(p, (int *)fd_array);
}

int64_t sys_dup2(uint64_t old_fd, uint64_t new_fd){
    if(new_fd > 1){
        return -1;
    }

    FD *d = fd_get(old_fd);
    if(d == NULL){
        return -1;
    }

    PCB *cur = process_current();
    if(cur == NULL){
        return -1;
    }

    if(cur->fd[new_fd] == (int)old_fd){
        return (int64_t)old_fd;
    }

    if(cur->fd[new_fd] >= 0){
        fd_decref((uint64_t)cur->fd[new_fd]);
    }

    cur->fd[new_fd] = (int)old_fd;
    fd_incref(old_fd);
    return (int64_t)old_fd;
}

int64_t sys_close(uint64_t fd){
    fd_decref(fd);
    return 0;
}

int64_t sys_pipe_open(uint64_t name, uint64_t fd_array){
    if(name == 0 || fd_array == 0){
        return -1;
    }

    Pipe *p = pipe_open((const char *)name);
    if(p == NULL){
        return -1;
    }

    return pipe_create_fds(p, (int *)fd_array);
}

int64_t sys_pipe_setup(uint64_t pid, uint64_t stdio_fd, uint64_t target){
    process_pipe_setup(pid, (int)stdio_fd, (int)target);
    return 0;
}

/* Tabla de syscalls */
void * syscalls[CANT_SYS] = {
    &sys_registers,         // 0
    &sys_time,              // 1
    &sys_date,              // 2
    &sys_read,              // 3
    &sys_write,             // 4
    &sys_increase_fontsize, // 5
    &sys_decrease_fontsize, // 6
    &sys_beep,              // 7
    &sys_ticks,             // 8
    &sys_clear,             // 9
    &sys_speaker_start,     // 10
    &sys_speaker_off,       // 11
    &sys_screen_width,      // 12
    &sys_screen_height,     // 13
    &sys_putpixel,          // 14
    &sys_fill_rect,         // 15
    &sys_malloc,            // 16
    &sys_free,              // 17
    &sys_mem_status,        // 18
    &sys_create_process,    // 19
    &sys_exit,              // 20
    &sys_getpid,            // 21
    &sys_ps,                // 22
    &sys_kill,              // 23
    &sys_nice,              // 24
    &sys_block,             // 25
    &sys_unblock,           // 26
    &sys_yield,             // 27
    &sys_waitpid,           // 28
    &sys_sem_open,          // 29
    &sys_sem_wait,          // 30
    &sys_sem_post,          // 31
    &sys_sem_close,         // 32
    &sys_pipe,              // 33
    &sys_dup2,              // 34
    &sys_close,             // 35
    &sys_pipe_open,         // 36
    &sys_pipe_setup,        // 37
};
