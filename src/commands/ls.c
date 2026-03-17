#include "terminal.h"
#include "ramdisk.h"

void cmd_ls(const char *args) {
    fs_node_t target = fs_cwd_get();

    if (args && *args) {
        if (!fs_resolve_checked(args, &target)) {
            term_puts("ls: path not found\n");
            return;
        }
    }

    if (fs_is_file(target)) {
        term_puts(fs_get_name(target));
        term_puts("\n");
        return;
    }

    fs_node_t iter;
    if (!fs_list_begin(target, &iter)) {
        term_puts("(empty)\n");
        return;
    }

    fs_node_t child;
    while (fs_list_next(&iter, &child)) {
        term_puts(fs_get_name(child));
        if (fs_is_dir(child)) term_puts("/");
        term_puts("  ");
    }

    term_puts("\n");
}
