#include "terminal.h"
#include "network.h"

void cmd_traceroute(const char *args) {
    if (!args || !*args) {
        term_puts("Usage: traceroute <ip>\n");
        return;
    }
    
    uint8_t ip[4];
    if (!network_parse_ipv4(args, ip)) {
        term_puts("traceroute: invalid IP address\n");
        return;
    }
    
    term_puts("traceroute to ");
    term_puts(args);
    term_puts(":\n");
    term_puts(" 1  ");
    term_puts(args);
    term_puts(" (timeout - simplified trace)\n");
}
