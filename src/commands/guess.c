#include "terminal.h"
#include "keyboard.h"

void cmd_guess(const char *args) {
    (void)args;
    term_puts("Guess my number (0-9). Press a key:\n");
    int r = (keyboard_poll() % 10) + '0';
    while (1) {
        int k = keyboard_poll();
        if (k >= '0' && k <= '9') {
            if (k == r) { term_puts("You won!\n"); break; }
            else term_puts("Wrong! Try again:\n");
        } else if (k == KEY_ESCAPE) break;
    }
}
