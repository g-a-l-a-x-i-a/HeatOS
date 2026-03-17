#include "terminal.h"
#include "ramdisk.h"

static void tree_recursive(fs_node_t node, int depth) {
    fs_node_t iter;
    if (!fs_list_begin(node, &iter)) return;
    
    fs_node_t child;
    while (fs_list_next(&iter, &child)) {
        for (int i = 0; i < depth; i++) term_puts("  ");
        term_puts("\xE3\x94\x9c ");
        term_puts(fs_get_name(child));
        if (fs_is_dir(child)) {
            term_puts("/\n");
            tree_recursive(child, depth + 1);
        } else {
            term_puts("\n");
        }
    }
}

void cmd_tree(const char *args) {
    (void)args;
    term_puts("./\n");
    tree_recursive(fs_cwd_get(), 1);
}
