#include "../include/userlib.h"
#include "../include/shell.h"
#include "../include/test_util.h"

int64_t kill_cmd(int argc, char *argv[]){
    if(argc != 1){
        shellPrintString("uso: kill <pid>\n");
        return -1;
    }

    int64_t pid = satoi(argv[0]);
    if(pid < 0){
        shellPrintString("PID invalido\n");
        return -1;
    }

    sys_kill((uint64_t)pid);

    shellPrintString("Proceso ");
    char buf[16];
    num_to_str((uint64_t)pid, buf, 10);
    shellPrintString(buf);
    shellPrintString(" terminado.\n");

    return 0;
}
