#include "terminal.h"

void cmd_kill(const char *args) {
    if (!args || !*args) {
        term_puts("Usage: kill <pid>\n");
        return;
    }
    
    term_puts("kill: process API not available\n");
    term_puts("Current system runs single-threaded kernel\n");
}
