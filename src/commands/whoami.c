#include "terminal.h"

void cmd_whoami(const char *args) {
    (void)args;
    term_puts("root\n");
}
