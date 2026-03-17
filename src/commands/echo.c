#include "terminal.h"

void cmd_echo(const char *args) {
    // Echo actually uses args
    term_puts("echo: print text\n");
    if (args && *args) {
        term_puts(args);
        term_puts("\n");
    }
}
