#include "terminal.h"
#include "ramdisk.h"
#include "string.h"

void cmd_cp(const char *args) {
    if (!args || !*args) {
        term_puts("Usage: cp <source> <dest>\n");
        return;
    }
    
    char buf[512];
    char src_buf[256];
    const char *src = args;
    const char *dest = strchr(args, ' ');
    
    if (!dest) {
        term_puts("Usage: cp <source> <dest>\n");
        return;
    }
    
    int src_len = (dest - src < 255) ? (dest - src) : 255;
    strncpy(src_buf, src, src_len);
    src_buf[src_len] = '\0';
    
    while (*dest == ' ') dest++;
    
    fs_node_t src_node = fs_resolve(src_buf);
    if (src_node == 0 || !fs_is_file(src_node)) {
        term_puts("cp: source not found or not a file\n");
        return;
    }
    
    fs_node_t dest_parent = fs_cwd_get();
    if (!fs_create_file(dest_parent, dest)) {
        term_puts("cp: failed to create destination\n");
        return;
    }
    
    int bytes = fs_read(src_node, buf, sizeof(buf));
    if (bytes > 0) {
        fs_node_t dest_node = fs_resolve(dest);
        if (dest_node != 0) {
            fs_write(dest_node, buf, bytes);
            term_puts("Copied ");
            char nbuf[16];
            itoa(bytes, nbuf, 10);
            term_puts(nbuf);
            term_puts(" bytes\n");
        }
    } else {
        term_puts("OK (empty file)\n");
    }
}
