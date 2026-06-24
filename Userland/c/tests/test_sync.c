#include "../include/syscall.h"
#include "../include/test_util.h"
#include "../include/userlib.h"
#include "../include/shell.h"

#define SEM_ID "sem"
#define TOTAL_PAIR_PROCESSES 2

static int64_t global;

/* Incrementa el valor apuntado por p en inc, con una demora aleatoria.
 * La demora provoca cambios de contexto para aumentar la probabilidad de
 * condiciones de carrera cuando no se usan semáforos. */
static void slowInc(int64_t *p, int64_t inc){
    int64_t aux = *p;

    if(GetUniform(100) < 30){
        // Cedemos CPU de forma no determinística para intercalar ejecuciones.
        my_yield();
    }

    aux += inc;
    *p = aux;
}

/* Función ejecutada por cada proceso hijo (incrementa o decrementa).
 * Args esperados:
 *  argv[0] = n (cantidad de iteraciones)
 *  argv[1] = inc (+1 o -1)
 *  argv[2] = use_sem (0/1 para habilitar semáforos) */
int64_t my_process_inc(int argc, char *argv[]){
    uint64_t n;
    int8_t inc;
    int8_t use_sem;

    /* Validación de aridad: sin parámetros correctos no hay test. */
    if(argc != 3){
        return -1;
    }

    /* n inválido: sin iteraciones no tiene sentido correr. */
    if((n = (uint64_t)satoi(argv[0])) == 0){
        return -1;
    }

    /* inc inválido (0): no modifica el valor global. */
    if((inc = (int8_t)satoi(argv[1])) == 0){
        return -1;
    }

    /* use_sem inválido: esperamos 0 o 1. */
    if((use_sem = (int8_t)satoi(argv[2])) < 0){
        return -1;
    }

    if(use_sem){
        // Semáforo compartido por nombre entre procesos no relacionados.
        if(!my_sem_open(SEM_ID, 1)){
            printf("test_sync: ERROR abriendo semaforo.\n");
            return -1;
        }
    }

    uint64_t i;

    /* Trata de forzar una race condition intercalando ejecuciones. */
    for(i = 0; i < n; i++){
        if(use_sem){
            /* Entrada a sección crítica: bloquea si el semáforo está en 0. */
            my_sem_wait(SEM_ID);
        }

        /* Actualización sobre el global. */
        slowInc(&global, inc);

        if(use_sem){
            /* Salida de sección crítica: libera a un proceso en espera. */
            my_sem_post(SEM_ID);
        }
    }

    if(use_sem){
        /* Cierra el semáforo; decrementa el contador de usuarios. */
        my_sem_close(SEM_ID);
    }

    return 0;
}

/* Función interna para ejecutar la prueba de sincronización.
 * Crea procesos incrementadores y decrementadores y espera su finalización. */
static int64_t test_sync_internal(uint64_t argc, char *argv[]){

    /* Array para almacenar los PIDs de los procesos creados. */
    uint64_t pids[2 * TOTAL_PAIR_PROCESSES];

    if(argc != 2){
        shellPrintString("uso: test_sync <n> <use_sem>\n");
        return -1;
    }

    /* n: cantidad de iteraciones, debe ser un entero positivo. */
    if(!is_valid_uint(argv[0]) || satoi(argv[0]) == 0){
        shellPrintString("test_sync: n debe ser un numero positivo\n");
        return -1;
    }

    /* use_sem: bandera 0 (sin semaforo) o 1 (con semaforo). */
    if(!is_valid_uint(argv[1]) || satoi(argv[1]) > 1){
        shellPrintString("test_sync: use_sem debe ser 0 o 1\n");
        return -1;
    }

    // argv[0] = n; argv[1] = use_sem (0/1)
    char *argvDec[] = {argv[0], "-1", argv[1], 0};
    char *argvInc[] = {argv[0], "1",  argv[1], 0};

    // Estado compartido entre procesos (intencionalmente global).
    global = 0;

    uint64_t i;

    for(i = 0; i < TOTAL_PAIR_PROCESSES; i++){
        // Crea procesos decrementarores e incrementarores en pares.
        pids[i] = (uint64_t)my_create_process("my_process_inc", 3, argvDec);
        pids[i + TOTAL_PAIR_PROCESSES] = (uint64_t)my_create_process("my_process_inc", 3, argvInc);
    }

    for (i = 0; i < TOTAL_PAIR_PROCESSES; i++) {
        // Espera a que terminen ambos procesos de cada par.
        my_wait((int64_t)pids[i]);
        my_wait((int64_t)pids[i + TOTAL_PAIR_PROCESSES]);
    }

    /* Con semáforos el valor final debe ser 0; sin ellos el valor suele variar. */
    printf("Valor final: %d\n", (int)global);

    return 0;
}

/* Wrapper de shell: test_sync <n> <use_sem>
 * Parsea argumentos y ejecuta el test en el mismo proceso. */
void test_sync_cmd(void){
    const char* args = cmd_args();

    if(!args){
        shellPrintString("uso: test_sync <n> <use_sem>\n");
        return;
    }

    char buf[64];
    uint64_t i = 0;

    while(args[i] && i < 63){ 
        buf[i] = args[i]; i++;
    }

    buf[i] = '\0';

    char *p = buf;

    while(*p && *p != ' '){
        p++;
    }

    if(*p == ' '){
        *p = '\0';
        p++;
        while(*p == ' ') p++;
    }else{
        shellPrintString("uso: test_sync <n> <use_sem>\n");
        return;
    }

    /* Reutilizamos el buffer tokenizado como argv. */
    char* argv[2] = {buf, p};
    test_sync_internal(2, argv);
}

/* Entry point para ser spawneado como proceso hijo, no como built-in. */
int64_t test_sync_entry(int argc, char *argv[]) {
    return test_sync_internal((uint64_t)argc, argv);
}
