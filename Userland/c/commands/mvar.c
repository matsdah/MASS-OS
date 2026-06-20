#include <stdint.h>
#include <stddef.h>
#include "../include/shell.h"
#include "../include/userlib.h"
#include "../include/syscall.h"
#include "../include/test_util.h"

/* Colores de lectores (ciclicos). */
static const uint32_t reader_colors[] = {
    0xCCCCCC, /* 0: gris claro */
    0xFF0000, /* 1: rojo */
    0x00FF00, /* 2: verde */
    0x0000FF, /* 3: azul */
    0xFFFF00, /* 4: amarillo */
    0x00FFFF, /* 5: cyan */
    0xFF00FF, /* 6: magenta */
    0xFFA500, /* 7: naranja */
    0xFFC0CB, /* 8: rosa */
    0x8B4513, /* 9: marron */
};

#define NUM_COLORS (sizeof(reader_colors)/sizeof(reader_colors[0]))

/* Espera activa aleatoria */
static void random_busy_wait(void){
    uint64_t n = (uint64_t)GetUniform(10000000) + 1000000;
    bussy_wait(n);
}

/* Construye nombre de MVar: base + pid */
static void build_mvar_name(uint64_t pid, char *out){
    const char *base = "mvar_";
    int i = 0;
    while(base[i]){
        out[i] = base[i];
        i++;
    }
    char tmp[20];
    int j = 0;
    if(pid == 0){
        tmp[j++] = '0';
    } else {
        while(pid > 0){
            tmp[j++] = '0' + (pid % 10);
            pid /= 10;
        }
    }
    while(j > 0){
        out[i++] = tmp[--j];
    }
    out[i] = '\0';
}

/* Escritor i: loop infinito */
void mvar_writer(int argc, char *argv[]){
    if(argc < 2){
        sys_exit(1);
    }

    int idx = (int)satoi(argv[0]);
    char letter = (char)('A' + idx);
    const char *name = argv[1];

    while(1){
        random_busy_wait();
        int64_t r = sys_mvar_put(name, letter);
        if(r == -1){
            /* MVar destruida o no existe */
            break;
        }
        /* r == 0: put exitoso (o fue bloqueado y despertado via handoff) */
    }
    sys_exit(0);
}

/* Lector i: loop infinito */
void mvar_reader(int argc, char *argv[]){
    if(argc < 2){
        sys_exit(1);
    }

    int idx = (int)satoi(argv[0]);
    uint32_t color = reader_colors[idx % NUM_COLORS];
    const char *name = argv[1];

    while(1){
        random_busy_wait();
        int64_t r = sys_mvar_take(name);
        if(r == -1){
            /* MVar destruida o no existe */
            break;
        }
        char c = (char)(unsigned char)r;
        sys_write_color(STDOUT, &c, 1, color);
    }
    sys_exit(0);
}

/* mvar <escritores> <lectores>
 * Crea una MVar nativa del kernel y spawnea writers/readers. */
int64_t mvar(int argc, char *argv[]){
    if(argc < 2){
        return -1;
    }

    int writers = (int)satoi(argv[0]);
    int readers = (int)satoi(argv[1]);

    if(writers <= 0 || readers <= 0){
        return -1;
    }

    uint64_t mypid = sys_getpid();
    char name[32];
    build_mvar_name(mypid, name);

    if(sys_mvar_create(name) != 1){
        return -1;
    }

    /* Crear escritores */
    for(int i = 0; i < writers; i++){
        char idx_str[4];
        if(i >= 10){
            idx_str[0] = '0' + ((i / 10) % 10);
            idx_str[1] = '0' + (i % 10);
            idx_str[2] = '\0';
        } else {
            idx_str[0] = '0' + i;
            idx_str[1] = '\0';
        }
        char *wargv[] = {idx_str, name, NULL};
        my_create_process("mvar_writer", 2, wargv);
    }

    /* Crear lectores */
    for(int i = 0; i < readers; i++){
        char idx_str[4];
        if(i >= 10){
            idx_str[0] = '0' + ((i / 10) % 10);
            idx_str[1] = '0' + (i % 10);
            idx_str[2] = '\0';
        } else {
            idx_str[0] = '0' + i;
            idx_str[1] = '\0';
        }
        char *rargv[] = {idx_str, name, NULL};
        my_create_process("mvar_reader", 2, rargv);
    }

    /* Proceso principal termina inmediatamente;
       los hijos quedan corriendo como huérfanos. */
    return 0;
}
