#include "terminal.h"

void cmd_pong(const char *args) {
    (void)args;
    term_puts("      PONG      \n");
    term_puts("\n");
    term_puts(" P1  BALL  P2\n");
    term_puts(" |    o   |\n");
    term_puts(" |         |\n");
    term_puts("\n");
    term_puts("Full Pong game requires real-time rendering\n");
    term_puts("(Use 'catnake' for playable game)\n");
}
