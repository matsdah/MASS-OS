#include <stdint.h>
#include "../include/shell.h"
#include "../include/userlib.h"
#include "../include/test_util.h"

#define FILTER_BUF_SIZE 128

static int is_vowel(char c) {
    return c == 'a' || c == 'e' || c == 'i' || c == 'o' || c == 'u' ||
           c == 'A' || c == 'E' || c == 'I' || c == 'O' || c == 'U';
}

int64_t filter(int argc, char *argv[]) {
    (void)argc;
    (void)argv;

    char buf[FILTER_BUF_SIZE];

    while (1) {
        uint64_t n = sys_read(STDIN, buf, FILTER_BUF_SIZE);
        if (n == 0) {
            break;
        }
        if (n == (uint64_t)-1) {
            continue;
        }

        uint64_t out_len = 0;
        for (uint64_t i = 0; i < n; i++) {
            if (!is_vowel(buf[i])) {
                buf[out_len++] = buf[i];
            }
        }

        if (out_len > 0) {
            sys_write(STDOUT, buf, out_len);
        }
    }

    return 0;
}
