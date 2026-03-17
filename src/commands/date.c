#include "terminal.h"

void cmd_date(const char *args) {
    (void)args;
    term_puts("RTC not yet initialized\n");
    term_puts("System uptime: Unknown\n");
    term_puts("(RTC driver implementation in progress)\n");
}
