#include "include/syscall.h"
#include "include/test_util.h"
#include "include/userlib.h"
#include "include/shell.h"

#define SEM_ID               "sem"
#define TOTAL_PAIR_PROCESSES 2

static int64_t global;

static void slowInc(int64_t *p, int64_t inc) {
    int64_t aux = *p;
    if (GetUniform(100) < 30)
        my_yield();
    aux += inc;
    *p = aux;
}

void my_process_inc(int argc, char *argv[]) {
    uint64_t n;
    int8_t   inc;
    int8_t   use_sem;

    if (argc != 3)
        return;

    if ((n = (uint64_t)satoi(argv[0])) == 0)
        return;
    if ((inc = (int8_t)satoi(argv[1])) == 0)
        return;
    if ((use_sem = (int8_t)satoi(argv[2])) < 0)
        return;

    if (use_sem) {
        if (!my_sem_open(SEM_ID, 1)) {
            printf("test_sync: ERROR abriendo semaforo\n");
            return;
        }
    }

    uint64_t i;
    for (i = 0; i < n; i++) {
        if (use_sem)
            my_sem_wait(SEM_ID);
        slowInc(&global, inc);
        if (use_sem)
            my_sem_post(SEM_ID);
    }

    if (use_sem)
        my_sem_close(SEM_ID);
}

static int64_t test_sync_internal(uint64_t argc, char *argv[]) {
    uint64_t pids[2 * TOTAL_PAIR_PROCESSES];

    if (argc != 2)
        return -1;

    char *argvDec[] = {argv[0], "-1", argv[1], 0};
    char *argvInc[] = {argv[0], "1",  argv[1], 0};

    global = 0;

    uint64_t i;
    for (i = 0; i < TOTAL_PAIR_PROCESSES; i++) {
        pids[i]                        = (uint64_t)my_create_process("my_process_inc", 3, argvDec);
        pids[i + TOTAL_PAIR_PROCESSES] = (uint64_t)my_create_process("my_process_inc", 3, argvInc);
    }

    for (i = 0; i < TOTAL_PAIR_PROCESSES; i++) {
        my_wait((int64_t)pids[i]);
        my_wait((int64_t)pids[i + TOTAL_PAIR_PROCESSES]);
    }

    printf("Valor final: %d\n", (int)global);

    return 0;
}

/* Wrapper de shell: test_sync <n> <use_sem> */
void test_sync_cmd(void) {
    const char *args = cmd_args();
    if (!args) {
        shellPrintString("uso: test_sync <n> <use_sem>\n");
        return;
    }

    char buf[64];
    uint64_t i = 0;
    while (args[i] && i < 63) { buf[i] = args[i]; i++; }
    buf[i] = '\0';

    char *p = buf;
    while (*p && *p != ' ') p++;
    if (*p == ' ') {
        *p = '\0';
        p++;
        while (*p == ' ') p++;
    } else {
        shellPrintString("uso: test_sync <n> <use_sem>\n");
        return;
    }

    char *argv[2] = {buf, p};
    test_sync_internal(2, argv);
}

/* Entry point para ser spawneado como proceso hijo */
int64_t test_sync_entry(int argc, char *argv[]) {
    return test_sync_internal((uint64_t)argc, argv);
}
