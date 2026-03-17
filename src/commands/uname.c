#include "terminal.h"

void cmd_uname(const char *args) {
    (void)args;
    term_puts("HeatOS 1.0 (32-bit Protected Mode)\n");
}
