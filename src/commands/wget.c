#include "terminal.h"
#include "ramdisk.h"
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

static void append_text(char *dst, size_t cap, size_t *pos, const char *src) {
    if (!dst || !pos || !src || cap == 0) return;

    while (*src && (*pos + 1) < cap) {
        dst[(*pos)++] = *src++;
    }
    dst[*pos] = '\0';
}

void cmd_wget(const char *args) {
    char target[160];
    char outfile[RAMDISK_NAME_LEN];
    char host[128];
    char path[128];
    char report[256];
    size_t rpos = 0;
    uint8_t ip[4];
    fs_node_t out_node;
    size_t i = 0;
    size_t j = 0;
    bool reachable = false;

    args = skip_spaces(args);
    if (!args || !*args) {
        term_puts("Usage: wget <url|host> [outfile]\n");
        return;
    }

    while (*args && *args != ' ' && i + 1 < sizeof(target)) {
        target[i++] = *args++;
    }
    target[i] = '\0';

    args = skip_spaces(args);
    if (args && *args) {
        while (*args && j + 1 < sizeof(outfile)) {
            outfile[j++] = *args++;
        }
        outfile[j] = '\0';
    } else {
        strncpy(outfile, "wget.txt", sizeof(outfile) - 1);
        outfile[sizeof(outfile) - 1] = '\0';
    }

    extract_host_path(target, host, sizeof(host), path, sizeof(path));
    if (!host[0]) {
        term_puts("wget: invalid URL/host\n");
        return;
    }

    if (!network_parse_ipv4(host, ip)) {
        net_dns_result_t dns;
        if (!network_dns_resolve_a(host, 800000, ip, &dns)) {
            term_puts("wget: DNS resolution failed\n");
            return;
        }
    }

    net_ping_result_t ping;
    if (network_ping_ipv4(ip, 600000, &ping) && ping.success) {
        reachable = true;
    }

    if (!fs_create_file(fs_cwd_get(), outfile)) {
        if (!fs_resolve_checked(outfile, &out_node) || !fs_is_file(out_node)) {
            term_puts("wget: cannot create output file\n");
            return;
        }
    } else {
        if (!fs_resolve_checked(outfile, &out_node) || !fs_is_file(out_node)) {
            term_puts("wget: output file resolution failed\n");
            return;
        }
    }

    report[0] = '\0';
    append_text(report, sizeof(report), &rpos, "WGET REPORT\n");
    append_text(report, sizeof(report), &rpos, "target: ");
    append_text(report, sizeof(report), &rpos, target);
    append_text(report, sizeof(report), &rpos, "\nhost: ");
    append_text(report, sizeof(report), &rpos, host);
    append_text(report, sizeof(report), &rpos, "\npath: ");
    append_text(report, sizeof(report), &rpos, path);
    append_text(report, sizeof(report), &rpos, "\nip: ");

    char nbuf[16];
    itoa(ip[0], nbuf, 10); append_text(report, sizeof(report), &rpos, nbuf); append_text(report, sizeof(report), &rpos, ".");
    itoa(ip[1], nbuf, 10); append_text(report, sizeof(report), &rpos, nbuf); append_text(report, sizeof(report), &rpos, ".");
    itoa(ip[2], nbuf, 10); append_text(report, sizeof(report), &rpos, nbuf); append_text(report, sizeof(report), &rpos, ".");
    itoa(ip[3], nbuf, 10); append_text(report, sizeof(report), &rpos, nbuf);

    append_text(report, sizeof(report), &rpos, "\nreachable: ");
    append_text(report, sizeof(report), &rpos, reachable ? "yes" : "no");
    append_text(report, sizeof(report), &rpos, "\nhttp-transfer: not implemented\n");

    if (!fs_write(out_node, report, (int)strlen(report))) {
        term_puts("wget: failed to write report\n");
        return;
    }

    term_puts("Saved report to ");
    term_puts(outfile);
    term_puts(" (host ");
    print_ipv4(ip);
    term_puts(")\n");
}
