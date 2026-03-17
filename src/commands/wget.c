#include "terminal.h"

void cmd_wget(const char *args) {
    (void)args;
    term_puts("Resolving host...\nHTTP/1.1 404 (Network Stack Incomplete)\n");
}
