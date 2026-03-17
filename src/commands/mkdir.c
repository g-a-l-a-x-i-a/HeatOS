#include "terminal.h"
#include "ramdisk.h"

void cmd_mkdir(const char *args) {
    if (args) fs_mkdir_child(fs_cwd_get(), args);
    else term_puts("Usage: mkdir <name>\n");
}
