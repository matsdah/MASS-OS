#include "../include/userlib.h"
#include "../include/shell.h"
#include "../include/test_util.h"

int64_t nice_cmd(int argc, char *argv[]){
    if(argc != 2){
        shellPrintString("uso: nice <pid> <prioridad>\n");
        return -1;
    }

    int64_t pid = satoi(argv[0]);
    int64_t prio = satoi(argv[1]);

    if(pid < 0){
        shellPrintString("PID invalido\n");
        return -1;
    }
    if(prio < 1 || prio > 5){
        shellPrintString("Prioridad fuera de rango (1-5)\n");
        return -1;
    }

    sys_nice((uint64_t)pid, (uint64_t)prio);

    shellPrintString("Prioridad de ");
    char buf[16];
    num_to_str((uint64_t)pid, buf, 10);
    shellPrintString(buf);
    shellPrintString(" cambiada a ");
    num_to_str((uint64_t)prio, buf, 10);
    shellPrintString(buf);
    shellPrintString("\n");

    return 0;
}
