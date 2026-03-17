#include "terminal.h"

void cmd_echo(const char *args) {
    if (args && *args) {
        term_puts(args);
    }
    term_puts("\n");
}
