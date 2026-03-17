#include "terminal.h"

void cmd_ps(const char *args) {
    (void)args;
    term_puts("PID  NAME     STATE\n");
    term_puts("1    kernel   running\n");
    term_puts("(Scheduler API not fully exposed)\n");
}
