// API userland: wrappers de syscalls y utilitarios minimos.
// sys_read(fd, buf, count): retorna READ_BLOCKED (-1) si bloquea, 0 si EOF, >0 si leyo bytes.
// getchar() bloquea hasta que haya una tecla disponible.
// Redraw buffer registra salida para re-render al cambiar tamaño de fuente.
#ifndef USERLIB_H
#define USERLIB_H

#include <stdint.h>
#include <memoryManager.h>

#define STDOUT 1
#define STDERR 2
#define REGSBUFF 500
#define REDRAW_BUFF 4096
#define KB 1024
#define NOTE_C4  262
#define NOTE_CS4 277
#define NOTE_D4  294
#define NOTE_DS4 311
#define NOTE_E4  330
#define NOTE_F4  349
#define NOTE_FS4 370
#define NOTE_G4  392
#define NOTE_GS4 415
#define NOTE_A4  440
#define NOTE_AS4 466
#define NOTE_B4  494
#define NOTE_C5  523
#define NOTE_CS5 554
#define NOTE_D5  587
#define NOTE_DS5 622
#define NOTE_E5  659
#define NOTE_F5  698
#define EIGHTH 50
#define QUARTER 100
#define HALF 200

#define MAX_PROCESSES 64
#define MAX_NAME_LEN  32

// Vista userland de un proceso (misma que kernel ProcessInfo)
typedef struct {
    uint64_t pid;
    char     name[MAX_NAME_LEN];
    uint8_t  priority;
    uint8_t  state;
    uint8_t  foreground;
    uint64_t rsp;
} ProcessInfo;

typedef struct{
    char character;
    uint64_t fd;
}RedrawStruct;

void redraw_reset(void);
void redraw_append_char(char c, uint64_t fd);

// ─── Syscalls de IO ───────────────────────────────────────────────────────────
uint64_t sys_write(uint64_t fd, const char * buff, uint64_t count);
uint64_t sys_read(uint64_t fd, char * buff, uint64_t count);
uint64_t sys_registers(char * buff);
void sys_time(uint8_t * buff);
void sys_date(uint8_t * buff);
void sys_increase_fontsize(void);
void sys_decrease_fontsize(void);
void sys_beep(uint32_t freq, uint64_t time);
uint64_t sys_ticks(void);
void sys_clear(void);
void sys_speaker_start(uint32_t freq);
void sys_speaker_off(void);
uint64_t sys_screen_width(void);
uint64_t sys_screen_height(void);
void sys_putpixel(uint32_t color, uint64_t x, uint64_t y);
void sys_fill_rect(uint64_t x, uint64_t y, uint64_t w, uint64_t h, uint32_t color);

// ─── Syscalls de memoria ──────────────────────────────────────────────────────
void *sys_malloc(uint64_t size);
void sys_free(void *ptr);
void sys_mem_status(MemStatus *status);

// ─── Syscalls de procesos ─────────────────────────────────────────────────────
int64_t  sys_create_process(const char *name, void *entry,
                            int argc, char **argv, uint8_t fg);
void     sys_exit(int retval);
uint64_t sys_getpid(void);
uint64_t sys_ps(ProcessInfo *buffer, uint64_t max_count);
void     sys_kill(uint64_t pid);
void     sys_nice(uint64_t pid, uint64_t new_priority);
int64_t  sys_block(uint64_t pid);
void     sys_unblock(uint64_t pid);
void     sys_yield(void);
int64_t  sys_waitpid(uint64_t pid);
int64_t  sys_sem_open(const char *name, uint64_t initial_value);
int64_t  sys_sem_wait(const char *name);
int64_t  sys_sem_post(const char *name);
int64_t  sys_sem_close(const char *name);
int64_t  sys_pipe(int fds[2]);
int64_t  sys_dup2(uint64_t old_fd, uint64_t new_fd);
int64_t  sys_pipe_open(const char *name, int fds[2]);
int64_t  sys_close(uint64_t fd);
int64_t  sys_pipe_setup(uint64_t pid, uint64_t stdio_fd, uint64_t target);
int64_t  sys_tty_mode(int mode);

// ─── Utilitarios ─────────────────────────────────────────────────────────────
uint64_t putchar(char c);
char getchar(void);
void processLine(char * buff, uint32_t * history_len);
uint64_t num_to_str(uint64_t value, char * dest, int base);
void gen_invalid_opcode(void);

// ─── Argumentos del comando actual ────────────────────────────────────────────
// Devuelve el resto de la linea (despues del primer espacio) o NULL si no hay
// argumentos. Lo setea processLine antes de invocar al comando. Usado por
// test_proc y test_prio.
const char *cmd_args(void);

// ─── Comandos de shell ────────────────────────────────────────────────────────
void clear(void);
void registers(void);
void divideByZero(void);
void printTime(void);
void printDate(void);
void playBeep(void);
void invOp(void);
uint8_t adjustHour(uint8_t hour, int offset);
void printTimeAndDate(uint8_t* buff, char separator);
void shellIncreaseFontSize(void);
void shellDecreaseFontSize(void);
void redrawFont(void);
void ps(void);

#endif
