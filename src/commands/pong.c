#include "terminal.h"
#include "catnake.h"

void cmd_pong(const char *args) {
    (void)args;
    term_puts("Pong mode uses Catnake engine in this build.\n");
    catnake_run();
    term_reset_screen();
}
