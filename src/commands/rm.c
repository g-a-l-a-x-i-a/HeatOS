#include "terminal.h"
#include "ramdisk.h"

void cmd_rm(const char *args) {
    if (args) {
        fs_node_t target = fs_resolve(args);
        if (target != 0) fs_delete(target);
        else term_puts("Not found\n");
    } else term_puts("Usage: rm <name>\n");
}
