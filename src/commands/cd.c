#include "terminal.h"
#include "ramdisk.h"

void cmd_cd(const char *args) {
    if (!args || !*args) {
        // cd without args goes to root
        fs_cwd_set(fs_resolve("/"));
        term_puts("Changed to root\n");
        return;
    }
    
    fs_node_t target = fs_resolve(args);
    if (target == 0) {
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
    term_puts("OK\n");
}
