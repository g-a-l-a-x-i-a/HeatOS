#include "terminal.h"
#include "ramdisk.h"

void cmd_touch(const char *args) {
    if (args) fs_create_file(fs_cwd_get(), args);
    else term_puts("Usage: touch <name>\n");
}
