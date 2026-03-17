#include "commands.h"
#include "terminal.h"
#include "ramdisk.h"
#include "string.h"

static const char *skip_spaces(const char *s) {
    while (s && *s == ' ') s++;
    return s;
}

static bool split_two_args(const char *args, char *a, size_t asz, char *b, size_t bsz) {
    size_t i = 0;
    size_t j = 0;

    args = skip_spaces(args);
    if (!args || !*args) return false;

    while (*args && *args != ' ' && i + 1 < asz) a[i++] = *args++;
    a[i] = '\0';

    args = skip_spaces(args);
    while (*args && j + 1 < bsz) b[j++] = *args++;
    b[j] = '\0';

    return a[0] != '\0' && b[0] != '\0';
}

static bool resolve_allow_root(const char *path, fs_node_t *out) {
    if (!out || !path) return false;
    return fs_resolve_checked(path, out);
}

void cmd_mv(const char *args) {
    char src[256];
    char dst[256];
    fs_node_t src_node;
    fs_node_t dst_node;

    if (!split_two_args(args, src, sizeof(src), dst, sizeof(dst))) {
        term_puts("Usage: mv <source> <dest>\n");
        return;
    }

    if (!resolve_allow_root(src, &src_node) || !fs_is_file(src_node)) {
        term_puts("mv: source not found\n");
        return;
    }

    if (resolve_allow_root(dst, &dst_node) && src_node == dst_node) {
        term_puts("mv: source and destination are identical\n");
        return;
    }

    if (!cmd_copy_file_path(src, dst, NULL)) {
        return;
    }

    if (!fs_delete(src_node)) {
        term_puts("mv: move copied, but source deletion failed\n");
        return;
    }

    term_puts("OK\n");
}
