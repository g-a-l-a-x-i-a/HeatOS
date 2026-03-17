#include "kat.h"
#include "ramdisk.h"
#include "terminal.h"
#include "string.h"
#include "vga.h"

#define KAT_BUFFER_SIZE 4096

void kat_run(const char *filename) {
    if (!filename || !filename[0]) {
        term_puts("kat: missing file operand\n");
        return;
    }

    fs_node_t node = fs_resolve(filename);
    if (!node) {
        term_puts("kat: ");
        term_puts(filename);
        term_puts(": No such file or directory\n");
        return;
    }

    if (!fs_is_file(node)) {
        term_puts("kat: ");
        term_puts(filename);
        term_puts(": Is a directory\n");
        return;
    }

    static char buffer[KAT_BUFFER_SIZE];
    int bytes_read = fs_read(node, buffer, KAT_BUFFER_SIZE - 1);
    if (bytes_read < 0) {
        term_puts("kat: ");
        term_puts(filename);
        term_puts(": Error reading file\n");
        return;
    }

    buffer[bytes_read] = '\0';
    term_puts(buffer);
    term_puts("\n");
}
