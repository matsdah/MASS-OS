#include "include/syscall.h"
#include "include/test_util.h"
#include "include/userlib.h"
#include "include/shell.h"

#define TOTAL_PROCESSES 3

#define LOWEST  1
#define MEDIUM  3
#define HIGHEST 5

static int64_t prio[TOTAL_PROCESSES] = {LOWEST, MEDIUM, HIGHEST};

static uint64_t max_value = 0;

void zero_to_max(int argc, char *argv[]) {
    (void)argc; (void)argv;
    uint64_t value = 0;
    while (value++ != max_value)
        ;
    printf("PROCESS %d DONE!\n", (int)my_getpid());
}

static int64_t test_prio_internal(uint64_t argc, char *argv[]) {
    int64_t pids[TOTAL_PROCESSES];
    char   *ztm_argv[] = {0};
    uint64_t i;

    if (argc != 1)
        return -1;

    if ((max_value = (uint64_t)satoi(argv[0])) == 0)
        return -1;

    printf("SAME PRIORITY...\n");

    for (i = 0; i < TOTAL_PROCESSES; i++)
        pids[i] = my_create_process("zero_to_max", 0, ztm_argv);

    for (i = 0; i < TOTAL_PROCESSES; i++)
        my_wait(pids[i]);

    printf("SAME PRIORITY, THEN CHANGE IT...\n");

    for (i = 0; i < TOTAL_PROCESSES; i++) {
        pids[i] = my_create_process("zero_to_max", 0, ztm_argv);
        my_nice((uint64_t)pids[i], (uint64_t)prio[i]);
        printf("  PROCESS %d NEW PRIORITY: %d\n", (int)pids[i], (int)prio[i]);
    }

    for (i = 0; i < TOTAL_PROCESSES; i++)
        my_wait(pids[i]);

    printf("SAME PRIORITY, THEN CHANGE IT WHILE BLOCKED...\n");

    for (i = 0; i < TOTAL_PROCESSES; i++) {
        pids[i] = my_create_process("zero_to_max", 0, ztm_argv);
        my_block((uint64_t)pids[i]);
        my_nice((uint64_t)pids[i], (uint64_t)prio[i]);
        printf("  PROCESS %d NEW PRIORITY: %d\n", (int)pids[i], (int)prio[i]);
    }

    for (i = 0; i < TOTAL_PROCESSES; i++)
        my_unblock((uint64_t)pids[i]);

    for (i = 0; i < TOTAL_PROCESSES; i++)
        my_wait(pids[i]);

    return 0;
}

/* Wrapper de shell: test_prio <max_value> */
void test_prio_cmd(void) {
    const char *args = cmd_args();
    if (!args) {
        shellPrintString("uso: test_prio <max_value>\n");
        return;
    }
    char *argv[1] = {(char *)args};
    test_prio_internal(1, argv);
}

/* Entry point para ser spawneado como proceso hijo */
int64_t test_prio_entry(int argc, char *argv[]) {
    return test_prio_internal((uint64_t)argc, argv);
}
