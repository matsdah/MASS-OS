#include <stdint.h>
#include <stddef.h>
#include "../include/shell.h"
#include "../include/userlib.h"
#include "../include/syscall.h"
#include "../include/test_util.h"

/* ─── MVar de espacio de usuario sobre semáforos del kernel ──────────────────
 *
 * Una MVar es un buffer de tamaño 1. Se implementa con dos semáforos:
 *   sem_empty (inicial 1): cuenta slots vacíos. Escritores esperan aquí.
 *   sem_full  (inicial 0): cuenta valores disponibles. Lectores esperan aquí.
 *
 * put: sem_wait(empty) → escribe value → sem_post(full)
 * take: sem_wait(full) → lee value → sem_post(empty)
 *
 * La tabla de MVars es una global compartida (no hay paging en este kernel,
 * todos los procesos ven el mismo address space). El valor se protege con
 * el protocolo de semáforos: sólo un proceso accede a la vez. */

#define MAX_USER_MVARS 16
#define UMVAR_NAME_LEN 28   /* dejar margen para sufijo _e/__f (< 32 de SEM_NAME_LEN) */

typedef struct {
    char name[UMVAR_NAME_LEN];
    char value;
    int in_use;
} user_mvar_t;

static user_mvar_t umvar_table[MAX_USER_MVARS];

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

/* Helpers de strings. */
static int umvar_str_eq(const char *a, const char *b){
    if(!a || !b) return 0;
    while(*a && (*a == *b)){ a++; b++; }
    return ((unsigned char)*a == (unsigned char)*b);
}

static void umvar_str_copy(char *dst, const char *src, int max){
    int i = 0;
    while(i < max - 1 && src && src[i]){
        dst[i] = src[i]; i++;
    }
    dst[i] = '\0';
}

/* Construye nombres de semáforos: "<mvarname>e" y "<mvarname>f".
 * Usa 'e'/'f' (sin guión) para ahorrar 1 char y mantenerse < 32. */
static void build_sem_names(const char *mvar_name, char *empty, char *full){
    int i = 0;
    while(mvar_name[i] && i < UMVAR_NAME_LEN - 1){
        empty[i] = mvar_name[i];
        full[i]  = mvar_name[i];
        i++;
    }
    empty[i] = 'e'; empty[i + 1] = '\0';
    full[i]  = 'f'; full[i + 1]  = '\0';
}

/* Busca una MVar por nombre. */
static user_mvar_t *find_umvar(const char *name){
    for(int i = 0; i < MAX_USER_MVARS; i++){
        if(umvar_table[i].in_use && umvar_str_eq(umvar_table[i].name, name)){
            return &umvar_table[i];
        }
    }
    return NULL;
}

/* ─── API pública de MVar (espacio de usuario) ─────────────────────────────── */

/* Abre los dos semáforos de una MVar tomando una referencia propia del proceso
 * llamador. Los valores iniciales (empty=1, full=0) son ignorados si el semáforo
 * ya existe, así que el orden de apertura entre participantes es irrelevante.
 * Cada writer/reader llama a esto al arrancar para ser dueño de su referencia:
 * así los semáforos viven mientras haya ≥1 participante y el kernel libera la
 * referencia de un participante muerto (bitmap opened_sems). Retorna 1 si ok. */
static int64_t user_mvar_open_sems(const char *name){
    char sem_empty[UMVAR_NAME_LEN + 2];
    char sem_full[UMVAR_NAME_LEN + 2];
    build_sem_names(name, sem_empty, sem_full);

    if(!my_sem_open(sem_empty, 1)) return 0;   /* 1 slot vacío */
    if(!my_sem_open(sem_full, 0)){ my_sem_close(sem_empty); return 0; }  /* 0 valores */
    return 1;
}

/* Cierra los dos semáforos de la MVar (libera la referencia del participante). */
static void user_mvar_close_sems(const char *name){
    char sem_empty[UMVAR_NAME_LEN + 2];
    char sem_full[UMVAR_NAME_LEN + 2];
    build_sem_names(name, sem_empty, sem_full);

    my_sem_close(sem_empty);
    my_sem_close(sem_full);
}

/* Registra la entrada de tabla de una MVar (NO abre semáforos: de eso se encargan
 * los participantes, ver user_mvar_open_sems). Retorna 1 en exito, 0 si ya existe. */
int64_t user_mvar_create(const char *name){
    if(!name) return 0;

    /* ¿Ya existe? */
    if(find_umvar(name) != NULL) return 0;

    /* Buscar slot libre. */
    int slot = -1;
    for(int i = 0; i < MAX_USER_MVARS; i++){
        if(!umvar_table[i].in_use){
            slot = i;
            break;
        }
    }

    /* Tabla llena: reclamar un slot rotativo. Cada corrida de `mvar` deja su
       entrada (el padre la crea y termina), pero sus participantes ya murieron y el
       kernel liberó sus semáforos, así que reusar un slot viejo es seguro. Cerramos
       sus semáforos por las dudas (no-op si ya no existen). */
    if(slot < 0){
        static int reclaim = 0;
        slot = reclaim;
        reclaim = (reclaim + 1) % MAX_USER_MVARS;
        user_mvar_close_sems(umvar_table[slot].name);
    }

    user_mvar_t *mv = &umvar_table[slot];
    umvar_str_copy(mv->name, name, UMVAR_NAME_LEN);
    mv->value = 0;
    mv->in_use = 1;
    return 1;
}

/* Escribe un valor en la MVar. Bloquea si está FULL.
 * Retorna 0 en exito, -1 si la MVar no existe. */
int64_t user_mvar_put(const char *name, char value){
    user_mvar_t *mv = find_umvar(name);
    if(mv == NULL) return -1;

    char sem_empty[UMVAR_NAME_LEN + 2];
    char sem_full[UMVAR_NAME_LEN + 2];
    build_sem_names(name, sem_empty, sem_full);

    /* Esperar slot vacío, escribir, señalar valor disponible. */
    my_sem_wait(sem_empty);
    mv->value = value;
    my_sem_post(sem_full);
    return 0;
}

/* Lee y consume el valor de la MVar. Bloquea si está EMPTY.
 * Retorna el valor leido (0..255), -1 si la MVar no existe. */
int64_t user_mvar_take(const char *name){
    user_mvar_t *mv = find_umvar(name);
    if(mv == NULL) return -1;

    char sem_empty[UMVAR_NAME_LEN + 2];
    char sem_full[UMVAR_NAME_LEN + 2];
    build_sem_names(name, sem_empty, sem_full);

    /* Esperar valor disponible, leer, señalar slot vacío. */
    my_sem_wait(sem_full);
    char val = mv->value;
    my_sem_post(sem_empty);
    return (int64_t)(unsigned char)val;
}

/* Destruye una MVar. Cierra sus semáforos. */
void user_mvar_destroy(const char *name){
    user_mvar_t *mv = find_umvar(name);
    if(mv == NULL) return;

    char sem_empty[UMVAR_NAME_LEN + 2];
    char sem_full[UMVAR_NAME_LEN + 2];
    build_sem_names(name, sem_empty, sem_full);

    my_sem_close(sem_empty);
    my_sem_close(sem_full);
    mv->in_use = 0;
    mv->name[0] = '\0';
}

/* ─── Comandos de shell ────────────────────────────────────────────────────── */

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

    /* Tomar referencia propia de los semaforos (lifetime atado a este proceso). */
    if(!user_mvar_open_sems(name)){
        sys_exit(1);
    }

    while(1){
        random_busy_wait();
        int64_t r = user_mvar_put(name, letter);
        if(r == -1){
            /* MVar destruida o no existe */
            break;
        }
    }

    user_mvar_close_sems(name);
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

    /* Tomar referencia propia de los semaforos (lifetime atado a este proceso). */
    if(!user_mvar_open_sems(name)){
        sys_exit(1);
    }

    while(1){
        random_busy_wait();
        int64_t r = user_mvar_take(name);
        if(r == -1){
            /* MVar destruida o no existe */
            break;
        }
        char c = (char)(unsigned char)r;
        sys_write_color(STDOUT, &c, 1, color);
    }

    user_mvar_close_sems(name);
    sys_exit(0);
}

/* mvar <escritores> <lectores>
 * Crea una MVar de userland y spawnea writers/readers. */
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

    if(user_mvar_create(name) != 1){
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
