#include "terminal.h"

void cmd_top(const char *args) {
    (void)args;
    term_puts("CPU Usage: 100% (kernel)\n");
    term_puts("Memory: Limited ramdisk\n");
    term_puts("(Live monitoring not available)\n");
}
