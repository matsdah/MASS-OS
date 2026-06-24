#include "../include/userlib.h"
#include "../include/shell.h"
#include "../include/memoryManager.h"
#include "../include/test_util.h"

int64_t mem(int argc, char *argv[]) {
    (void)argc;
    (void)argv;

    MemStatus st;
    sys_mem_status(&st);

    char buf[32];

    shellPrintString("Total:       ");
    num_to_str(st.total, buf, 10);
    shellPrintString(buf);
    shellPrintString(" bytes\n");

    shellPrintString("Free:        ");
    num_to_str(st.free, buf, 10);
    shellPrintString(buf);
    shellPrintString(" bytes\n");

    shellPrintString("Used (user): ");
    num_to_str(st.used, buf, 10);
    shellPrintString(buf);
    shellPrintString(" bytes\n");

    shellPrintString("Used (kern): ");
    num_to_str(st.used_kernel, buf, 10);
    shellPrintString(buf);
    shellPrintString(" bytes\n");

    shellPrintString("Alloc count: ");
    num_to_str(st.alloc_count, buf, 10);
    shellPrintString(buf);
    shellPrintString("\n");

    return 0;
}
