#include "terminal.h"
#include "ramdisk.h"

void cmd_rm(const char *args) {
    fs_node_t target;

    if (!args || !*args) {
        term_puts("Usage: rm <name>\n");
        return;
    }

    if (!fs_resolve_checked(args, &target)) {
        term_puts("rm: not found\n");
        return;
    }

    if (target == 0) {
        term_puts("rm: cannot remove root\n");
        return;
    }

    if (target == fs_cwd_get()) {
        term_puts("rm: cannot remove current directory\n");
        return;
    }

    if (fs_delete(target)) {
        term_puts("OK\n");
    } else {
        term_puts("rm: failed (non-empty dir or invalid)\n");
    }
}
