#include "terminal.h"
#include "network.h"
#include "string.h"

static const char *skip_spaces(const char *s) {
    while (s && *s == ' ') s++;
    return s;
}

static void print_ipv4(const uint8_t ip[4]) {
    char nbuf[16];
    itoa(ip[0], nbuf, 10); term_puts(nbuf); term_puts(".");
    itoa(ip[1], nbuf, 10); term_puts(nbuf); term_puts(".");
    itoa(ip[2], nbuf, 10); term_puts(nbuf); term_puts(".");
    itoa(ip[3], nbuf, 10); term_puts(nbuf);
}

void cmd_nslookup(const char *args) {
    char host[128];
    size_t i = 0;
    uint8_t ip[4];
    net_dns_result_t result;

    args = skip_spaces(args);
    if (!args || !*args) {
        term_puts("Usage: nslookup <hostname>\n");
        return;
    }

    while (*args && *args != ' ' && i + 1 < sizeof(host)) {
        host[i++] = *args++;
    }
    host[i] = '\0';

    term_puts("Resolving ");
    term_puts(host);
    term_puts("...\n");

    if (network_parse_ipv4(host, ip)) {
        term_puts("Address: ");
        print_ipv4(ip);
        term_puts("\n");
        return;
    }

    if (network_dns_resolve_a(host, 800000, ip, &result)) {
        term_puts("Address: ");
        print_ipv4(ip);
        term_puts("\n");
    } else {
        term_puts("Name resolution failed\n");
    }
}
