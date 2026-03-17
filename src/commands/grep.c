#include "terminal.h"
#include "ramdisk.h"
#include "string.h"

void cmd_grep(const char *args) {
    if (!args || !*args) {
        term_puts("Usage: grep <pattern> [file]\n");
        return;
    }
    
    char buf[1024];
    const char *pattern = args;
    const char *filename = strchr(args, ' ');
    
    if (!filename) {
        term_puts("Usage: grep <pattern> [file]\n");
        return;
    }
    
    while (*filename == ' ') filename++;
    
    fs_node_t file = fs_resolve(filename);
    if (file == 0 || !fs_is_file(file)) {
        term_puts("grep: file not found\n");
        return;
    }
    
    int bytes = fs_read(file, buf, sizeof(buf) - 1);
    if (bytes > 0) {
        buf[bytes] = '\0';
        const char *p = buf;
        int line_num = 1;
        
        while (*p) {
            if (strncmp(p, pattern, strlen(pattern)) == 0) {
                itoa(line_num, buf, 10);
                term_puts(buf);
                term_puts(": Match found\n");
            }
            if (*p == '\n') {
                line_num++;
            }
            p++;
        }
    }
}
