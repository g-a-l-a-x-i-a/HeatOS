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

static bool split_pattern_file(const char *args, char *pattern, size_t pattern_size,
                               char *file, size_t file_size) {
    size_t i = 0;
    size_t j = 0;

    if (!args || !*args) return false;

    while (*args == ' ') args++;
    while (*args && *args != ' ' && i + 1 < pattern_size) {
        pattern[i++] = *args++;
    }
    pattern[i] = '\0';

    while (*args == ' ') args++;
    while (*args && j + 1 < file_size) {
        file[j++] = *args++;
    }
    file[j] = '\0';

    return pattern[0] != '\0' && file[0] != '\0';
}

void cmd_grep(const char *args) {
    char pattern[64];
    char file_name[256];
    static char file_buf[RAMDISK_DATA_CAP + 1];

    if (!split_pattern_file(args, pattern, sizeof(pattern), file_name, sizeof(file_name))) {
        term_puts("Usage: grep <pattern> <file>\n");
        return;
    }

    fs_node_t file;
    if (!fs_resolve_checked(file_name, &file) || !fs_is_file(file)) {
        term_puts("grep: file not found\n");
        return;
    }

    int bytes = fs_read(file, file_buf, RAMDISK_DATA_CAP);
    if (bytes > 0) {
        file_buf[bytes] = '\0';

        int line_num = 1;
        int matches = 0;
        char *line_start = file_buf;

        for (int i = 0; i <= bytes; i++) {
            if (file_buf[i] == '\n' || file_buf[i] == '\0') {
                char saved = file_buf[i];
                char nbuf[16];

                file_buf[i] = '\0';
                if (contains_nocase(line_start, pattern)) {
                    itoa(line_num, nbuf, 10);
                    term_puts(nbuf);
                    term_puts(": ");
                    term_puts(line_start);
                    term_puts("\n");
                    matches++;
                }
                file_buf[i] = saved;

                if (saved == '\0') break;
                line_start = &file_buf[i + 1];
                line_num++;
            }
        }

        if (matches == 0) {
            term_puts("No matches\n");
        }
    } else if (bytes == 0) {
        term_puts("No matches\n");
    } else {
        term_puts("grep: read failed\n");
    }
}
