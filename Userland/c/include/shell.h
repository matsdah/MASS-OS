#ifndef SHELL_H
#define SHELL_H

#include <stdint.h>

#define CURSOR '_'
#define STDIN  0
#define STDOUT 1
#define TTY_RAW   0
#define TTY_COOKED 1
#define WELCOME "Bienvenido al MASS OS!\n"
#define BUFF_LENGTH 100

typedef void (*Runnable)(void);

typedef struct Command{
     char* name;
     Runnable function;
} Command;

void shellPrintString(char *str);
void shellPutchar(char c, uint64_t fd);
void shellNewline(void);
void shellReadLine(char * buffer, uint64_t max);
void shell_run(void);

#endif