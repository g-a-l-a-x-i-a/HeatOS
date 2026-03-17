#include "commands.h"
#include "terminal.h"
#include "ramdisk.h"
#include "string.h"

static bool resolve_allow_root(const char *path, fs_node_t *out) {
    if (!out || !path) return false;
    return fs_resolve_checked(path, out);
}

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

static bool split_parent_and_name(const char *path, char *parent, size_t psz,
                                  char *name, size_t nsz) {
    char tmp[256];
    size_t n = 0;
    size_t split = 0;

    path = skip_spaces(path);
    if (!path || !*path) return false;

    while (*path && n + 1 < sizeof(tmp)) {
        tmp[n++] = *path++;
    }
    while (n > 0 && tmp[n - 1] == ' ') n--;
    tmp[n] = '\0';
    if (n == 0) return false;

    for (size_t i = 0; i < n; i++) {
        if (tmp[i] == '/') split = i + 1;
    }

    if (split == 0) {
        if (psz < 2) return false;
        parent[0] = '.';
        parent[1] = '\0';
        strncpy(name, tmp, nsz - 1);
        name[nsz - 1] = '\0';
        return name[0] != '\0';
    }

    if (split == n) return false;

    if (split == 1) {
        if (psz < 2) return false;
        parent[0] = '/';
        parent[1] = '\0';
    } else {
        size_t plen = split - 1;
        if (plen + 1 > psz) return false;
        memcpy(parent, tmp, plen);
        parent[plen] = '\0';
    }

    strncpy(name, &tmp[split], nsz - 1);
    name[nsz - 1] = '\0';
    return name[0] != '\0';
}

bool cmd_copy_file_path(const char *src_path, const char *dst_path, int *bytes_out) {
    static uint8_t copy_buf[RAMDISK_DATA_CAP];
    fs_node_t src_node;
    fs_node_t dst_node;
    fs_node_t dst_parent;
    char parent_path[256];
    char child_name[RAMDISK_NAME_LEN];

    if (!resolve_allow_root(src_path, &src_node) || !fs_is_file(src_node)) {
        term_puts("cp: source not found or not a file\n");
        return false;
    }

    if (resolve_allow_root(dst_path, &dst_node)) {
        if (!fs_is_file(dst_node)) {
            term_puts("cp: destination must be a file path\n");
            return false;
        }
    } else {
        if (!split_parent_and_name(dst_path, parent_path, sizeof(parent_path),
                                   child_name, sizeof(child_name))) {
            term_puts("cp: invalid destination path\n");
            return false;
        }

        if (!resolve_allow_root(parent_path, &dst_parent) || !fs_is_dir(dst_parent)) {
            term_puts("cp: destination parent not found\n");
            return false;
        }

        if (!fs_create_file(dst_parent, child_name)) {
            term_puts("cp: failed to create destination file\n");
            return false;
        }

        if (!resolve_allow_root(dst_path, &dst_node) || !fs_is_file(dst_node)) {
            term_puts("cp: destination resolution failed\n");
            return false;
        }
    }

    int bytes = fs_read(src_node, copy_buf, RAMDISK_DATA_CAP);
    if (bytes < 0) {
        term_puts("cp: read failed\n");
        return false;
    }

    if (!fs_write(dst_node, copy_buf, bytes)) {
        term_puts("cp: write failed\n");
        return false;
    }

    if (bytes_out) *bytes_out = bytes;
    return true;
}

void cmd_cp(const char *args) {
    char src[256];
    char dst[256];
    int bytes = 0;
    char nbuf[16];

    if (!split_two_args(args, src, sizeof(src), dst, sizeof(dst))) {
        term_puts("Usage: cp <source> <dest>\n");
        return;
    }

    if (!cmd_copy_file_path(src, dst, &bytes)) {
        return;
    }

    itoa(bytes, nbuf, 10);
    term_puts("Copied ");
    term_puts(nbuf);
    term_puts(" bytes\n");
}
