#include "kat.h"
#include "ramdisk.h"
#include "types.h"
#include "vga.h"

void kat_run(const char *filename, term_hooks_t *hooks) {
    if (!hooks) return;

    if (!filename || !*filename) {
        hooks->putln("Usage: kat <file>");
        return;
    }
    
    fs_node_t node = fs_resolve(filename);
    if (!node || !fs_is_file(node)) {
        hooks->putln("kat: file not found");
        return;
    }

    char buf[512];
    int n = fs_read(node, buf, sizeof(buf) - 1);
    if (n >= 0) {
        buf[n] = '\0';
        hooks->puts(buf);
        if (n > 0 && buf[n-1] != '\n') {
            hooks->putc('\n', VGA_ATTR(VGA_WHITE, VGA_BLACK));
        }
    } else {
        hooks->putln("kat: error reading file");
    }
}
