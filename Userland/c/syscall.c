#include "include/syscall.h"
#include "include/userlib.h"
#include "include/test_util.h"
#include <stdint.h>

/* ── Registro de funciones por nombre ───────────────────────────────────── */

typedef struct {
    const char *name;
    void       *fn;
} ProcEntry;

static int str_eq(const char *a, const char *b) {
    if (!a || !b) return 0;
    while (*a && (*a == *b)) { a++; b++; }
    return (unsigned char)*a == (unsigned char)*b;
}

static ProcEntry registry[] = {
    {"endless_loop",       (void *)endless_loop},
    {"endless_loop_print", (void *)endless_loop_print},
    {"zero_to_max",        (void *)zero_to_max},
    {"my_process_inc",     (void *)my_process_inc},
    {"test_mm",            (void *)test_mm_entry},
    {"test_processes",     (void *)test_processes_entry},
    {"test_prio",          (void *)test_prio_entry},
    {"test_sync",          (void *)test_sync_entry},
    {0, 0}
};

/* ── Wrappers my_* ───────────────────────────────────────────────────────── */

int64_t my_getpid(void) {
    return (int64_t)sys_getpid();
}

int64_t my_create_process(char *name, uint64_t argc, char *argv[]) {
    return my_create_process_fg(name, argc, argv, 0);
}

int64_t my_create_process_fg(char *name, uint64_t argc, char *argv[], uint8_t fg) {
    int i;
    for (i = 0; registry[i].name; i++) {
        if (str_eq(registry[i].name, name))
            return sys_create_process(name, registry[i].fn, (int)argc, argv, fg);
    }
    return -1;
}

int64_t my_nice(uint64_t pid, uint64_t newPrio) {
    sys_nice(pid, newPrio);
    return 0;
}

int64_t my_kill(uint64_t pid) {
    sys_kill(pid);
    return 0;
}

int64_t my_block(uint64_t pid) {
    sys_block(pid);
    return 0;
}

int64_t my_unblock(uint64_t pid) {
    sys_unblock(pid);
    return 0;
}

int64_t my_yield(void) {
    sys_yield();
    return 0;
}

int64_t my_wait(int64_t pid) {
    return sys_waitpid((uint64_t)pid);
}

int64_t my_sem_open(char *name, uint64_t initialValue) {
    return sys_sem_open(name, initialValue);
}

int64_t my_sem_wait(char *name) {
    return sys_sem_wait(name);
}

int64_t my_sem_post(char *name) {
    return sys_sem_post(name);
}

int64_t my_sem_close(char *name) {
    return sys_sem_close(name);
}
