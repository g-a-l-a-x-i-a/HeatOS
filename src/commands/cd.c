#include "terminal.h"
#include "ramdisk.h"

void cmd_cd(const char *args) {
    fs_node_t target;

    if (!args || !*args) {
        fs_cwd_set(0);
        return;
    }

    if (!fs_resolve_checked(args, &target)) {
        term_puts("cd: directory not found: ");
        term_puts(args);
        term_puts("\n");
        return;
    }

    if (!fs_is_dir(target)) {
        term_puts("cd: not a directory: ");
        term_puts(args);
        term_puts("\n");
        return;
    }

    fs_cwd_set(target);
}
