#include "terminal.h"
#include "ramdisk.h"
#include "string.h"

static char ascii_lower(char c) {
    if (c >= 'A' && c <= 'Z') return (char)(c - 'A' + 'a');
    return c;
}

static bool contains_nocase(const char *haystack, const char *needle) {
    if (!needle || !*needle) return true;
    if (!haystack) return false;

    for (size_t i = 0; haystack[i]; i++) {
        size_t j = 0;
        while (needle[j] && haystack[i + j] &&
               ascii_lower(haystack[i + j]) == ascii_lower(needle[j])) {
            j++;
        }
        if (!needle[j]) return true;
    }
    return false;
}

static void find_recursive(fs_node_t node, const char *pattern) {
    fs_node_t iter;
    if (!fs_list_begin(node, &iter)) return;

    fs_node_t child;
    while (fs_list_next(&iter, &child)) {
        const char *name = fs_get_name(child);
        if (contains_nocase(name, pattern)) {
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
    while (*pattern == ' ') pattern++;
    find_recursive(fs_cwd_get(), pattern);
}
