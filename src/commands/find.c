#include "terminal.h"
#include "ramdisk.h"
#include "string.h"

static void find_recursive(fs_node_t node, const char *pattern) {
    fs_node_t iter;
    if (!fs_list_begin(node, &iter)) return;
    
    fs_node_t child;
    while (fs_list_next(&iter, &child)) {
        const char *name = fs_get_name(child);
        if (strlen(pattern) == 0 || strncmp(name, pattern, strlen(pattern)) == 0) {
            char path[256];
            fs_build_path(child, path, sizeof(path));
            term_puts(path);
            term_puts("\n");
        }
        if (fs_is_dir(child)) {
            find_recursive(child, pattern);
        }
    }
}

void cmd_find(const char *args) {
    const char *pattern = args ? args : "";
    find_recursive(fs_cwd_get(), pattern);
}
