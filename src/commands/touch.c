#include "terminal.h"
#include "ramdisk.h"

void cmd_touch(const char *args) {
    if (!args || !*args) {
        term_puts("Usage: touch <name>\n");
        return;
    }

    if (fs_create_file(fs_cwd_get(), args)) {
        term_puts("OK\n");
    } else {
        term_puts("touch: failed (exists/invalid/full)\n");
    }
}
