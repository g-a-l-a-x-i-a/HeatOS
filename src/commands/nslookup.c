#include "terminal.h"
#include "network.h"
#include "string.h"

void cmd_nslookup(const char *args) {
    if (!args || !*args) {
        term_puts("Usage: nslookup <hostname>\n");
        return;
    }
    
    term_puts("Resolving ");
    term_puts(args);
    term_puts("...\n");
    
    uint8_t ip[4];
    net_dns_result_t result;
    
    if (network_dns_resolve_a(args, 500000, ip, &result)) {
        term_puts("Address: ");
        char buf[12];
        itoa(ip[0], buf, 10);
        term_puts(buf);
        term_puts(".");
        itoa(ip[1], buf, 10);
        term_puts(buf);
        term_puts(".");
        itoa(ip[2], buf, 10);
        term_puts(buf);
        term_puts(".");
        itoa(ip[3], buf, 10);
        term_puts(buf);
        term_puts("\n");
    } else {
        term_puts("Name resolution failed\n");
    }
}
