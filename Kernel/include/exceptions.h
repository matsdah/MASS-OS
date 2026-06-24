#ifndef EXCEPTIONS_H
#define EXCEPTIONS_H

#define ZERO_EXCEPTION_ID 0
#define INVALID_OPCODE_ID 6

void exceptionDispatcher(int exception);
void exceptionHandler(char * msg);
void zeroDivision(void);
void invalidOpcode(void);
typedef void (*Exception)(void);

#endif