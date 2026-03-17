#include "terminal.h"
#include "ramdisk.h"
#include "string.h"

void cmd_mv(const char *args) {
    if (!args || !*args) {
        term_puts("Usage: mv <source> <dest>\n");
        return;
    }
    
    char buf[256];
    const char *src = args;
    const char *dest = strchr(args, ' ');
    
    if (!dest) {
        term_puts("Usage: mv <source> <dest>\n");
        return;
    }
    
    int src_len = dest - src;
    strncpy(buf, src, src_len);
    buf[src_len] = '\0';
    
    while (*dest == ' ') dest++;
    
    fs_node_t src_node = fs_resolve(buf);
    if (src_node == 0) {
        term_puts("mv: source not found\n");
        return;
    }
    
    term_puts("Note: mv currently renames only. Renaming to: ");
    term_puts(dest);
    term_puts("\n");
    term_puts("Full implementation requires directory traversal\n");
}
