#include "terminal.h"

void cmd_telnet(const char *args) {
    if (!args || !*args) {
        term_puts("Usage: telnet <host> [port]\n");
        return;
    }
    
    term_puts("telnet: TCP client not fully implemented\n");
    term_puts("Requires complete TCP connection management\n");
}
