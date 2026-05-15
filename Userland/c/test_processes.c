#include "include/syscall.h"
#include "include/test_util.h"
#include "include/userlib.h"
#include "include/shell.h"

enum State { RUNNING, BLOCKED, KILLED };

typedef struct P_rq {
    int32_t    pid;
    enum State state;
} p_rq;

static int64_t test_processes_internal(uint64_t argc, char *argv[]) {
    uint8_t  rq;
    uint8_t  alive = 0;
    uint8_t  action;
    uint64_t max_processes;
    char    *argvAux[] = {0};

    if (argc != 1)
        return -1;

    if ((max_processes = (uint64_t)satoi(argv[0])) <= 0)
        return -1;

    p_rq p_rqs[max_processes];

    while (1) {
        for (rq = 0; rq < max_processes; rq++) {
            p_rqs[rq].pid = (int32_t)my_create_process("endless_loop", 0, argvAux);

            if (p_rqs[rq].pid == -1) {
                printf("test_processes: ERROR creando proceso\n");
                return -1;
            } else {
                p_rqs[rq].state = RUNNING;
                alive++;
            }
        }

        while (alive > 0) {
            for (rq = 0; rq < max_processes; rq++) {
                action = (uint8_t)(GetUniform(100) % 2);

                switch (action) {
                    case 0:
                        if (p_rqs[rq].state == RUNNING || p_rqs[rq].state == BLOCKED) {
                            if (my_kill((uint64_t)p_rqs[rq].pid) == -1) {
                                printf("test_processes: ERROR matando proceso\n");
                                return -1;
                            }
                            p_rqs[rq].state = KILLED;
                            my_wait(p_rqs[rq].pid);
                            alive--;
                        }
                        break;

                    case 1:
                        if (p_rqs[rq].state == RUNNING) {
                            if (my_block((uint64_t)p_rqs[rq].pid) == -1) {
                                printf("test_processes: ERROR bloqueando proceso\n");
                                return -1;
                            }
                            p_rqs[rq].state = BLOCKED;
                        }
                        break;
                }
            }

            for (rq = 0; rq < max_processes; rq++) {
                if (p_rqs[rq].state == BLOCKED && GetUniform(100) % 2) {
                    if (my_unblock((uint64_t)p_rqs[rq].pid) == -1) {
                        printf("test_processes: ERROR desbloqueando proceso\n");
                        return -1;
                    }
                    p_rqs[rq].state = RUNNING;
                }
            }
        }
    }
}

/* Wrapper de shell: test_processes <max_processes> */
void test_processes_cmd(void) {
    const char *args = cmd_args();
    if (!args) {
        shellPrintString("uso: test_processes <max_processes>\n");
        return;
    }
    char *argv[1] = {(char *)args};
    test_processes_internal(1, argv);
}

/* Entry point para ser spawneado como proceso hijo */
int64_t test_processes_entry(int argc, char *argv[]) {
    return test_processes_internal((uint64_t)argc, argv);
}
