#include <stdint.h>
#include "../include/shell.h"
#include "../include/userlib.h"
#include "../include/test_util.h"

#define CAT_BUF_SIZE 128

int64_t cat(int argc, char *argv[]) {
    (void)argc;
    (void)argv;

    char buf[CAT_BUF_SIZE];

    while (1) {
        uint64_t n = sys_read(STDIN, buf, CAT_BUF_SIZE);
        if (n == 0) {
            break;
        }
        if (n == (uint64_t)-1) {
            continue;
        }
        sys_write(STDOUT, buf, n);
    }

    return 0;
}
