#include "../include/syscall.h"
#include "../include/test_util.h"
#include "../include/userlib.h"
#include "../include/shell.h"

/* Número total de procesos a crear en la prueba. */
#define TOTAL_PROCESSES 3

/* Prioridades para los procesos. */
#define LOWEST  1
#define MEDIUM  3
#define HIGHEST 5

static int64_t prio[TOTAL_PROCESSES] = {LOWEST, MEDIUM, HIGHEST};

static uint64_t max_value = 0;

/* Obtiene la prioridad actual del proceso consultando la tabla de procesos. */
static uint8_t get_current_priority(void){
    static ProcessInfo buf[MAX_PROCESSES];
    uint64_t count = sys_ps(buf, MAX_PROCESSES);
    uint64_t pid = sys_getpid();
    for(uint64_t i = 0; i < count; i++){
        if(buf[i].pid == pid){
            return buf[i].priority;
        }
    }
    return 0;
}

/* Función para incrementar un valor desde 0 hasta max_value. */
void zero_to_max(int argc, char *argv[]){
    (void)argc; 
    (void)argv;
    uint64_t value = 0;

    while(value++ != max_value){
        ;
    }

    uint8_t pr = get_current_priority();
    printf("PROCESS %d DONE! PRIO: %d\n", (int)my_getpid(), (int)pr);
}

static int64_t test_prio_internal(uint64_t argc, char *argv[]){
    int64_t pids[TOTAL_PROCESSES];
    char* ztm_argv[] = {0};
    uint64_t i;

    if(argc != 1){
        shellPrintString("uso: test_prio <max_value>\n");
        return -1;
    }

    /* Validar que argv[0] sea un numero positivo. */
    if(!is_valid_uint(argv[0])){
        shellPrintString("test_prio: max_value debe ser un numero positivo\n");
        return -1;
    }

    if((max_value = (uint64_t)satoi(argv[0])) == 0){
        shellPrintString("test_prio: max_value debe ser mayor a 0\n");
        return -1;
    }

    /* Crear procesos con la misma prioridad. */
    printf("SAME PRIORITY...\n");

    for(i = 0; i < TOTAL_PROCESSES; i++){
        pids[i] = my_create_process("zero_to_max", 0, ztm_argv);
    }

    for(i = 0; i < TOTAL_PROCESSES; i++){
        my_wait(pids[i]);
    }

    /* Cambiar la prioridad de los procesos. */
    printf("SAME PRIORITY, THEN CHANGE IT...\n");

    for(i = 0; i < TOTAL_PROCESSES; i++){

        pids[i] = my_create_process("zero_to_max", 0, ztm_argv);
        my_nice((uint64_t)pids[i], (uint64_t)prio[i]);
        printf("  PROCESS %d NEW PRIORITY: %d\n", (int)pids[i], (int)prio[i]);
    }

    for(i = 0; i < TOTAL_PROCESSES; i++){
        my_wait(pids[i]);
    }

    printf("SAME PRIORITY, THEN CHANGE IT WHILE BLOCKED...\n");

    for(i = 0; i < TOTAL_PROCESSES; i++){
        pids[i] = my_create_process("zero_to_max", 0, ztm_argv);
        my_block((uint64_t)pids[i]);
        my_nice((uint64_t)pids[i], (uint64_t)prio[i]);
        printf("  PROCESS %d NEW PRIORITY: %d\n", (int)pids[i], (int)prio[i]);
    }

    for(i = 0; i < TOTAL_PROCESSES; i++){
        my_unblock((uint64_t)pids[i]);
    }

    for(i = 0; i < TOTAL_PROCESSES; i++){
        my_wait(pids[i]);
    }

    return 0;
}
/* Wrapper de shell: test_prio <max_value> */
void test_prio_cmd(void) {
    const char* args = cmd_args();
    if(!args){
        shellPrintString("uso: test_prio <max_value>\n");
        return;
    }

    /* El argumento es <max_value>. Si es vacio o no numerico, rechazar. */
    if(args[0] == '\0'){
        shellPrintString("uso: test_prio <max_value>\n");
        return;
    }

    char *p = (char *)args;
    for(; *p; p++){
        if(*p < '0' || *p > '9'){
            shellPrintString("test_prio: max_value debe ser un numero positivo\n");
            return;
        }
    }

    char* argv[1] = {(char *)args};
    test_prio_internal(1, argv);
}

/* Entry point para ser spawneado como proceso hijo */
int64_t test_prio_entry(int argc, char *argv[]) {
    return test_prio_internal((uint64_t)argc, argv);
}
