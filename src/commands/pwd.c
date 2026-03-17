#include "terminal.h"
#include "ramdisk.h"

void cmd_pwd(const char *args) {
    (void)args;
    char path[256];
    fs_build_path(fs_cwd_get(), path, sizeof(path));
    term_puts(path);
    term_puts("\n");
}
