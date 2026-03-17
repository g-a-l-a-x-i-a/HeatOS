#include "terminal.h"
#include "catnake.h"

void cmd_tetris(const char *args) {
    (void)args;
    term_puts("Tetris mode uses Catnake engine in this build.\n");
    catnake_run();
    term_reset_screen();
}
