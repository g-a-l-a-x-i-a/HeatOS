#include "terminal.h"
#include "network.h"
#include "string.h"

static const char *skip_spaces(const char *s) {
    while (s && *s == ' ') s++;
    return s;
}

static int parse_port(const char *s) {
    int port = 0;
    if (!s || !*s) return 23;
    while (*s) {
        if (*s < '0' || *s > '9') return -1;
        port = port * 10 + (*s - '0');
        if (port > 65535) return -1;
        s++;
    }
    if (port == 0) return -1;
    return port;
}

static void print_ipv4(const uint8_t ip[4]) {
    char nbuf[16];
    itoa(ip[0], nbuf, 10); term_puts(nbuf); term_puts(".");
    itoa(ip[1], nbuf, 10); term_puts(nbuf); term_puts(".");
    itoa(ip[2], nbuf, 10); term_puts(nbuf); term_puts(".");
    itoa(ip[3], nbuf, 10); term_puts(nbuf);
}

void cmd_telnet(const char *args) {
    char host[128];
    char port_buf[16];
    size_t i = 0;
    size_t j = 0;
    int port;
    uint8_t ip[4];

    args = skip_spaces(args);
    if (!args || !*args) {
        term_puts("Usage: telnet <host> [port]\n");
        return;
    }

    while (*args && *args != ' ' && i + 1 < sizeof(host)) {
        host[i++] = *args++;
    }
    host[i] = '\0';

    args = skip_spaces(args);
    while (*args && j + 1 < sizeof(port_buf)) {
        port_buf[j++] = *args++;
    }
    port_buf[j] = '\0';

    port = parse_port(port_buf);
    if (port < 0) {
        term_puts("telnet: invalid port\n");
        return;
    }

    if (!network_parse_ipv4(host, ip)) {
        net_dns_result_t dns;
        if (!network_dns_resolve_a(host, 800000, ip, &dns)) {
            term_puts("telnet: DNS resolution failed\n");
            return;
        }
    }

    term_puts("telnet preflight\n");
    term_puts("  remote: ");
    print_ipv4(ip);
    term_puts(":");
    char nbuf[16];
    itoa(port, nbuf, 10);
    term_puts(nbuf);
    term_puts("\n");

    net_ping_result_t ping;
    if (network_ping_ipv4(ip, 600000, &ping) && ping.success) {
        term_puts("  reachability: ok\n");
    } else {
        term_puts("  reachability: timeout\n");
    }

    term_puts("telnet: interactive TCP is not implemented in this kernel build.\n");
}
