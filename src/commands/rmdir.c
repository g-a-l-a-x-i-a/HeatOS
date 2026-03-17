#include "terminal.h"
#include "ramdisk.h"

void cmd_rmdir(const char *args) {
    if (!args || !*args) {
        term_puts("Usage: rmdir <dirname>\n");
        return;
    }
    
    fs_node_t target = fs_resolve(args);
    if (target == 0) {
        term_puts("rmdir: directory not found\n");
        return;
    }
    
    if (!fs_is_dir(target)) {
        term_puts("rmdir: not a directory\n");
        return;
    }
    
    fs_node_t iter;
    if (fs_list_begin(target, &iter)) {
        fs_node_t child;
        if (fs_list_next(&iter, &child)) {
            term_puts("rmdir: directory not empty\n");
            return;
        }
    }
    
    if (fs_delete(target)) {
        term_puts("OK\n");
    } else {
        term_puts("rmdir: failed to remove\n");
    }
}
