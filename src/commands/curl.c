#include "terminal.h"
#include "network.h"
#include "string.h"

static const char *skip_spaces(const char *s) {
    while (s && *s == ' ') s++;
    return s;
}

static void extract_host_path(const char *target, char *host, size_t host_size,
                              char *path, size_t path_size) {
    size_t i = 0;
    size_t j = 0;

    target = skip_spaces(target);
    if (strncmp(target, "http://", 7) == 0) target += 7;
    else if (strncmp(target, "https://", 8) == 0) target += 8;

    while (*target && *target != '/' && *target != ' ' && i + 1 < host_size) {
        host[i++] = *target++;
    }
    host[i] = '\0';

    if (*target == '/') {
        while (*target && *target != ' ' && j + 1 < path_size) {
            path[j++] = *target++;
        }
    }
    if (j == 0) {
        if (path_size > 1) {
            path[0] = '/';
            path[1] = '\0';
        }
    } else {
        path[j] = '\0';
    }
}

static void print_ipv4(const uint8_t ip[4]) {
    char nbuf[16];
    itoa(ip[0], nbuf, 10); term_puts(nbuf); term_puts(".");
    itoa(ip[1], nbuf, 10); term_puts(nbuf); term_puts(".");
    itoa(ip[2], nbuf, 10); term_puts(nbuf); term_puts(".");
    itoa(ip[3], nbuf, 10); term_puts(nbuf);
}

void cmd_curl(const char *args) {
    char host[128];
    char path[128];
    uint8_t ip[4];

    args = skip_spaces(args);
    if (!args || !*args) {
        term_puts("Usage: curl <url>\n");
        return;
    }

    extract_host_path(args, host, sizeof(host), path, sizeof(path));
    if (!host[0]) {
        term_puts("curl: invalid URL/host\n");
        return;
    }

    if (!network_parse_ipv4(host, ip)) {
        net_dns_result_t dns;
        if (!network_dns_resolve_a(host, 800000, ip, &dns)) {
            term_puts("curl: DNS resolution failed\n");
            return;
        }
    }

    term_puts("curl preflight\n");
    term_puts("  host: "); term_puts(host); term_puts("\n");
    term_puts("  path: "); term_puts(path); term_puts("\n");
    term_puts("  ip:   "); print_ipv4(ip); term_puts("\n");

    net_ping_result_t ping;
    if (network_ping_ipv4(ip, 600000, &ping) && ping.success) {
        term_puts("  reachability: ok\n");
    } else {
        term_puts("  reachability: timeout\n");
    }

    term_puts("curl: HTTP transfer is not implemented in this kernel build.\n");
}
