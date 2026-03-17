#include "terminal.h"
#include "network.h"
#include "string.h"

static void print_ipv4(const uint8_t ip[4]) {
    char nbuf[16];
    itoa(ip[0], nbuf, 10); term_puts(nbuf); term_puts(".");
    itoa(ip[1], nbuf, 10); term_puts(nbuf); term_puts(".");
    itoa(ip[2], nbuf, 10); term_puts(nbuf); term_puts(".");
    itoa(ip[3], nbuf, 10); term_puts(nbuf);
}

void cmd_netstat(const char *args) {
    (void)args;

    net_info_t info;
    network_get_info(&info);

    term_puts("Network status\n");
    term_puts("  Adapter: ");
    term_puts(info.present ? "present\n" : "missing\n");

    if (info.present) {
        term_puts("  Link: ");
        term_puts(info.ready ? "ready\n" : "not ready\n");
        term_puts("  Local IP: ");
        print_ipv4(info.ip);
        term_puts("\n");
    }

    term_puts("\nActive sockets\n");
    term_puts("  unavailable (TCP/UDP socket table API not exposed yet)\n");
}
