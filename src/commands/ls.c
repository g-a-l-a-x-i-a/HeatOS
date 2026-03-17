#include "terminal.h"
#include "ramdisk.h"

void cmd_ls(const char *args) {
    (void)args;
    fs_node_t iter;
    if (fs_list_begin(fs_cwd_get(), &iter)) {
        fs_node_t child;
        while (fs_list_next(&iter, &child)) {
            term_puts(fs_get_name(child));
            if (fs_is_dir(child)) term_puts("/");
            term_puts("  ");
        }
        term_puts("\n");
    }
}
