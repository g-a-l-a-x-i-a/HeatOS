#include "terminal.h"

void cmd_netstat(const char *args) {
    (void)args;
    term_puts("Proto  Local       Remote      State\n");
    term_puts("ICMP   10.0.2.15   *           waiting\n");
    term_puts("(Socket tracking not available)\n");
}
