#include "include/test_util.h"
#include "include/userlib.h"
#include "include/shell.h"
#include <stdarg.h>
#include <stdint.h>

/* Generador de números pseudo-aleatorios. */
static uint32_t m_z = 362436069;
static uint32_t m_w = 521288629;

uint32_t GetUint(void){
    m_z = 36969 * (m_z & 65535) + (m_z >> 16);
    m_w = 18000 * (m_w & 65535) + (m_w >> 16);
    return (m_z << 16) + m_w;
}

/* Genera un número entero pseudo-aleatorio uniformemente distribuido en [0, max) */
uint32_t GetUniform(uint32_t max){
    uint32_t u = GetUint();
    return (uint32_t)((u + 1.0) * 2.328306435454494e-10 * max);
}

/* Verificacion de memoria. */
uint8_t memcheck(void *start, uint8_t value, uint32_t size){
    uint8_t* p = (uint8_t *)start;
    uint32_t i;

    for(i = 0; i < size; i++, p++)
        if(*p != value){
            return 0;
        }

    return 1;
}

/* Conversión de string a entero con signo. */
int64_t satoi(char *str){
    uint64_t i = 0;
    int64_t res = 0;
    int8_t sign = 1;

    if(!str){
        return 0;
    }

    if(str[i] == '-'){
        i++;
        sign = -1;
    }

    for(; str[i] != '\0'; ++i){
        if(str[i] < '0' || str[i] > '9'){
            return 0;
        }

        res = res * 10 + str[i] - '0';
    }

    return res * sign;
}

/* Busy-wait y procesos dummy. */
void bussy_wait(uint64_t n){
    uint64_t i;

    for(i = 0; i < n; i++){
        ;
    }
}

void endless_loop(int argc, char *argv[]){
    (void)argc; 
    (void)argv;
    
    while(1){
        ;
    }
}

void endless_loop_print(int argc, char *argv[]){
    uint64_t wait = (argc > 0 && argv && argv[0]) ? (uint64_t)satoi(argv[0]) : 1000000000;
    int64_t pid = (int64_t)sys_getpid();

    while(1){
        printf("%d ", (int)pid);
        bussy_wait(wait);
    }
}

/* printf mínimo. */
static int print_uint(uint64_t v){
    char buf[21];
    int pos = 0;

    if(v == 0){
        putchar('0');
        return 1;
    }

    while(v){
        buf[pos++] = (char)('0' + v % 10);
        v /= 10;
    }

    int digits = pos;
    while(pos-- > 0){
        putchar(buf[pos]);
    }
    return digits;
}

static int print_int(int64_t v){
    if(v < 0){
        putchar('-');
        return 1 + print_uint((uint64_t)(-v));
    }
    return print_uint((uint64_t)v);
}

static int print_str(const char *s){
    if(!s){
        shellPrintString("(null)");
        return 6;
    }

    int n = 0;
    while(*s){
        putchar(*s++);
        n++;
    }
    return n;
}

int printf(const char *fmt, ...){
    va_list args;
    va_start(args, fmt);

    int count = 0;
    while(*fmt){
        if(*fmt != '%'){
            putchar(*fmt++);
            count++;
            continue;
        }
        fmt++;
        switch (*fmt) {
            case 'd':
                count += print_int((int64_t)va_arg(args, int));
                break;
            case 'u':
                count += print_uint((uint64_t)va_arg(args, unsigned int));
                break;
            case 's':
                count += print_str(va_arg(args, const char *));
                break;
            case 'c':
                putchar((char)va_arg(args, int));
                count++;
                break;
            case '%':
                putchar('%');
                count++;
                break;
            default:
                putchar('%');
                putchar(*fmt);
                count += 2;
                break;
        }
        fmt++;
    }

    va_end(args);
    return count;
}
