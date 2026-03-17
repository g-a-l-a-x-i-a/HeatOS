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

static int parse_count(const char *s, int fallback) {
    int value = 0;
    if (!s || !*s) return fallback;
    while (*s) {
        if (*s < '0' || *s > '9') return fallback;
        value = value * 10 + (*s - '0');
        s++;
    }
    if (value <= 0) return fallback;
    return value;
}

void cmd_ping(const char *args) {
    char host[128];
    char count_buf[16];
    size_t i = 0;
    size_t j = 0;
    uint8_t ip[4];
    int count;
    int success = 0;

    args = skip_spaces(args);
    if (!args || !*args) {
        term_puts("Usage: ping <ip|host> [count]\n");
        return;
    }

    while (*args && *args != ' ' && i + 1 < sizeof(host)) {
        host[i++] = *args++;
    }
    host[i] = '\0';

    args = skip_spaces(args);
    while (*args && j + 1 < sizeof(count_buf)) {
        count_buf[j++] = *args++;
    }
    count_buf[j] = '\0';
    count = parse_count(count_buf, 4);
    if (count > 10) count = 10;

    if (!network_parse_ipv4(host, ip)) {
        net_dns_result_t dns;
        if (!network_dns_resolve_a(host, 800000, ip, &dns)) {
            term_puts("ping: cannot resolve host\n");
            return;
        }
    }

    term_puts("Pinging ");
    print_ipv4(ip);
    term_puts(" with 32 bytes of data:\n");

    for (int seq = 0; seq < count; seq++) {
        net_ping_result_t res;
        char nbuf[16];

        if (network_ping_ipv4(ip, 600000, &res) && res.success) {
            term_puts("reply from ");
            print_ipv4(ip);
            term_puts(": bytes=");
            itoa(res.bytes, nbuf, 10); term_puts(nbuf);
            term_puts(" ttl=");
            itoa(res.ttl, nbuf, 10); term_puts(nbuf);
            term_puts(" time=");
            itoa(res.time_ms, nbuf, 10); term_puts(nbuf);
            term_puts("ms\n");
            success++;
        } else {
            term_puts("request timed out\n");
        }
    }

    char nbuf[16];
    term_puts("Ping statistics: sent=");
    itoa(count, nbuf, 10); term_puts(nbuf);
    term_puts(" received=");
    itoa(success, nbuf, 10); term_puts(nbuf);
    term_puts(" lost=");
    itoa(count - success, nbuf, 10); term_puts(nbuf);
    term_puts("\n");
}
