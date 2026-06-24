#include "../include/shell.h"
#include "../include/test_util.h"

int64_t sh_entry(int argc, char *argv[]){
    (void)argc;
    (void)argv;
    shell_run();
    return 0;
}
