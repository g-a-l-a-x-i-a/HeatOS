#include "terminal.h"
#include "scheduler.h"

static const char *skip_spaces(const char *s) {
    while (s && *s == ' ') s++;
    return s;
}

static bool parse_pid(const char *s, uint32_t *out_pid) {
    uint32_t v = 0;

    s = skip_spaces(s);
    if (!s || !*s) return false;

    while (*s) {
        if (*s < '0' || *s > '9') return false;
        v = v * 10u + (uint32_t)(*s - '0');
        s++;
    }

    *out_pid = v;
    return true;
}

void cmd_kill(const char *args) {
    uint32_t pid;

    if (!parse_pid(args, &pid)) {
        term_puts("Usage: kill <pid>\n");
        return;
    }

    if (pid == 0) {
        term_puts("kill: cannot kill kernel process\n");
        return;
    }

    if (scheduler_kill(pid)) {
        term_puts("OK\n");
    } else {
        term_puts("kill: pid not found\n");
    }
}
