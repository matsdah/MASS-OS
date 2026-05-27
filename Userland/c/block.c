#include "include/userlib.h"
#include "include/shell.h"
#include "include/test_util.h"

int64_t block_cmd(int argc, char *argv[]){
    if(argc != 1){
        shellPrintString("uso: block <pid>\n");
        return -1;
    }

    int64_t pid = satoi(argv[0]);
    if(pid < 0){
        shellPrintString("PID invalido\n");
        return -1;
    }

    int64_t rc = sys_block((uint64_t)pid);
    if(rc < 0){
        shellPrintString("No se puede modificar un proceso bloqueado por syscall interna.\n");
        return -1;
    }

    shellPrintString("Estado de ");
    char buf[16];
    num_to_str((uint64_t)pid, buf, 10);
    shellPrintString(buf);
    shellPrintString(" alternado.\n");

    return 0;
}
