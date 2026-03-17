#include "terminal.h"
#include "scheduler.h"
#include "string.h"

static const char *state_name(uint32_t state) {
    switch (state) {
        case 1: return "running";
        case 0: return "stopped";
        default: return "unknown";
    }
}

void cmd_ps(const char *args) {
    (void)args;

    scheduler_proc_info_t procs[32];
    int count = scheduler_snapshot(procs, 32);

    term_puts("PID  STATE    CURRENT\n");
    for (int i = 0; i < count; i++) {
        char nbuf[16];
        itoa(procs[i].id, nbuf, 10);
        term_puts(nbuf);

        if (procs[i].id < 10) term_puts("    ");
        else if (procs[i].id < 100) term_puts("   ");
        else term_puts("  ");

        term_puts(state_name(procs[i].state));
        if (procs[i].state == 1) term_puts("  ");
        else term_puts(" ");

        term_puts(procs[i].current ? "*" : "-");
        term_puts("\n");
    }
}
