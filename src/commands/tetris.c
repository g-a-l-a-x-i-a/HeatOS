#include "terminal.h"

void cmd_tetris(const char *args) {
    (void)args;
    term_puts("      TETRIS      \n");
    term_puts("\n");
    term_puts("  ####\n");
    term_puts("  ####\n");
    term_puts("\n");
    term_puts("Full Tetris game requires real-time rendering\n");
    term_puts("(Use 'catnake' for playable game)\n");
}
