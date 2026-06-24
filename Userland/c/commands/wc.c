#include <stdint.h>
#include "../include/shell.h"
#include "../include/userlib.h"
#include "../include/test_util.h"

#define WC_BUF_SIZE 128

static int is_space(char c) {
    return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f' || c == '\v';
}

int64_t wc(int argc, char *argv[]) {
    (void)argc;
    (void)argv;

    char buf[WC_BUF_SIZE];
    uint64_t lines = 0;
    uint64_t words = 0;
    uint64_t bytes = 0;
    int in_word = 0;

    while (1) {
        uint64_t n = sys_read(STDIN, buf, WC_BUF_SIZE);
        if (n == 0) {
            break;
        }
        if (n == (uint64_t)-1) {
            continue;
        }

        for (uint64_t i = 0; i < n; i++) {
            char c = buf[i];
            bytes++;

            if (c == '\n') {
                lines++;
            }

            if (is_space(c)) {
                in_word = 0;
            } else if (!in_word) {
                in_word = 1;
                words++;
            }
        }
    }

    char num_buf[32];

    shellNewline();
    shellPrintString("Lineas: ");
    num_to_str(lines, num_buf, 10);
    shellPrintString(num_buf);
    shellNewline();
    shellPrintString("Palabras: ");
    num_to_str(words, num_buf, 10);
    shellPrintString(num_buf);
    shellNewline();
    shellPrintString("Bytes: ");
    num_to_str(bytes, num_buf, 10);
    shellPrintString(num_buf);
    shellNewline();

    return 0;
}
