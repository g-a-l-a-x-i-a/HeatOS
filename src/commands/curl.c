#include "terminal.h"

void cmd_curl(const char *args) {
    if (!args || !*args) {
        term_puts("Usage: curl <url>\n");
        return;
    }
    
    term_puts("curl: Network stack incomplete\n");
    term_puts("HTTP/HTTPS client requires full TCP/TLS implementation\n");
}
