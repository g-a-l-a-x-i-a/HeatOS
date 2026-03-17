#include "terminal.h"
#include "ramdisk.h"

void cmd_mkdir(const char *args) {
    if (!args || !*args) {
        term_puts("Usage: mkdir <name>\n");
        return;
    }

    if (fs_mkdir_child(fs_cwd_get(), args)) {
        term_puts("OK\n");
    } else {
        term_puts("mkdir: failed (exists/invalid/full)\n");
    }
}
