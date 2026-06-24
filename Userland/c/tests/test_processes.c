#include "../include/syscall.h"
#include "../include/test_util.h"
#include "../include/userlib.h"
#include "../include/shell.h"

/* Estados posibles de un proceso. */
enum State { RUNNING, BLOCKED, KILLED };

/* Estructura para representar una solicitud de proceso. */
typedef struct P_rq{
    int32_t pid;
    enum State state;
} p_rq;

static int64_t test_processes_internal(uint64_t argc, char* argv[]){
    uint8_t rq;
    uint8_t alive = 0;
    uint8_t action;
    uint64_t max_processes;
    char* argvAux[] = {0};

    if(argc != 1){
        shellPrintString("uso: test_processes <max_processes>\n");
        return -1;
    }

    if(!is_valid_uint(argv[0])){
        shellPrintString("test_processes: max_processes debe ser un numero positivo\n");
        return -1;
    }

    if((max_processes = (uint64_t)satoi(argv[0])) == 0){
        shellPrintString("test_processes: max_processes debe ser mayor a 0\n");
        return -1;
    }

    /* El kernel solo dispone de MAX_PROCESSES slots y los contadores internos
     * son de 8 bits, asi que se acota max_processes para evitar VLA gigantes y
     * desbordes de iteracion. */
    if(max_processes > MAX_PROCESSES){
        shellPrintString("test_processes: max_processes excede el maximo (64)\n");
        return -1;
    }

    /* Arreglo de solicitudes de proceso. */
    p_rq p_rqs[max_processes];

    while(1){
        for(rq = 0; rq < max_processes; rq++){
            p_rqs[rq].pid = (int32_t)my_create_process("endless_loop", 0, argvAux);

            if(p_rqs[rq].pid == -1){
                printf("test_processes: ERROR creando proceso\n");
                return -1;
            }else{
                p_rqs[rq].state = RUNNING;
                alive++;
            }
        }

        while(alive > 0){
            for(rq = 0; rq < max_processes; rq++){

                /* Generar acción aleatoria */
                action = (uint8_t)(GetUniform(100) % 2);

                switch(action){
                    case 0:
                    /* Matar proceso. */
                        if(p_rqs[rq].state == RUNNING || p_rqs[rq].state == BLOCKED){
                            if(my_kill((uint64_t)p_rqs[rq].pid) == -1){
                                printf("test_processes: ERROR matando proceso\n");
                                return -1;
                            }

                            p_rqs[rq].state = KILLED;
                            my_wait(p_rqs[rq].pid);
                            alive--;
                        }
                        break;

                    case 1:
                    /* Bloquear proceso. */
                        if(p_rqs[rq].state == RUNNING){
                            if(my_block((uint64_t)p_rqs[rq].pid) == -1){
                                printf("test_processes: ERROR bloqueando proceso\n");
                                return -1;
                            }
                            p_rqs[rq].state = BLOCKED;
                        }
                        break;
                }
            }

            /* Desbloquear procesos bloqueados. */
            for(rq = 0; rq < max_processes; rq++){

                /* Generar acción aleatoria */
                if(p_rqs[rq].state == BLOCKED && GetUniform(100) % 2){

                    if(my_unblock((uint64_t)p_rqs[rq].pid) == -1){
                        printf("test_processes: ERROR desbloqueando proceso\n");
                        return -1;
                    }

                    /* Actualizar estado del proceso */
                    p_rqs[rq].state = RUNNING;
                }
            }
        }
    }
}

/* Wrapper de shell: test_processes <max_processes> */
void test_processes_cmd(void) {
    const char* args = cmd_args();
    
    if(!args){
        shellPrintString("uso: test_processes <max_processes>\n");
        return;
    }

    char* argv[1] = {(char* )args};
    test_processes_internal(1, argv);
}

/* Entry point para ser spawneado como proceso hijo */
int64_t test_processes_entry(int argc, char *argv[]){
    return test_processes_internal((uint64_t)argc, argv);
}
