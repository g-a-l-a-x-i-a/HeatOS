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

void cmd_traceroute(const char *args) {
    char host[128];
    size_t i = 0;
    uint8_t ip[4];

    args = skip_spaces(args);
    if (!args || !*args) {
        term_puts("Usage: traceroute <ip|host>\n");
        return;
    }

    while (*args && *args != ' ' && i + 1 < sizeof(host)) {
        host[i++] = *args++;
    }
    host[i] = '\0';

    if (!network_parse_ipv4(host, ip)) {
        net_dns_result_t dns;
        if (!network_dns_resolve_a(host, 800000, ip, &dns)) {
            term_puts("traceroute: cannot resolve host\n");
            return;
        }
    }

    term_puts("traceroute to ");
    term_puts(host);
    term_puts(" (");
    print_ipv4(ip);
    term_puts(")\n");

    term_puts(" 1  ");
    print_ipv4(ip);
    term_puts("  ");

    int got = 0;
    for (int probe = 0; probe < 3; probe++) {
        net_ping_result_t res;
        char nbuf[16];
        if (network_ping_ipv4(ip, 600000, &res) && res.success) {
            itoa(res.time_ms, nbuf, 10);
            term_puts(nbuf);
            term_puts("ms ");
            got++;
        } else {
            term_puts("* ");
        }
    }
    term_puts("\n");

    if (got == 0) {
        term_puts("Destination unreachable\n");
    }

    term_puts("Note: hop-by-hop TTL probing is not available in this network stack yet.\n");
}
