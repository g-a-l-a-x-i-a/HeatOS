#include "terminal.h"
#include "vga.h"
#include "keyboard.h"
#include "string.h"

static int cursor_x = 0;
static int cursor_y = 0;

void term_putc(char c, uint8_t attr) {
    if (c == '\n') {
        cursor_x = 0;
        cursor_y++;
    } else if (c == '\b') {
        if (cursor_x > 0) {
            cursor_x--;
            vga_putchar_at(cursor_y, cursor_x, ' ', attr);
        }
    } else {
        vga_putchar_at(cursor_y, cursor_x, c, attr);
        cursor_x++;
        if (cursor_x >= VGA_WIDTH) {
            cursor_x = 0;
            cursor_y++;
        }
    }
    if (cursor_y >= VGA_HEIGHT) {
        vga_scroll_up(0, VGA_HEIGHT - 1, attr);
        cursor_y = VGA_HEIGHT - 1;
    }
    vga_set_cursor(cursor_y, cursor_x);
}

void term_puts(const char *s) {
    while (*s) {
        term_putc(*s++, 0x07);
    }
}

void terminal_run(void) {
    vga_clear(0x07);
    term_puts("Welcome to HeatOS!\n");
    term_puts("The whole kernel have been rewritten for stability.\n> ");
    
    char cmdbuf[256];
    int cmdlen = 0;
    
    while (1) {
        int key = keyboard_poll();
        if (key) {
            if (key == KEY_ENTER || key == '\n') {
                term_putc('\n', 0x07);
                cmdbuf[cmdlen] = '\0';
                
                if (cmdlen > 0) {
                    if (strcmp(cmdbuf, "help") == 0) {
                        term_puts("Available commands:\n  help - Show this message\n  clear - Clear screen\n");
                    } else if (strcmp(cmdbuf, "clear") == 0) {
                        vga_clear(0x07);
                        cursor_x = 0; cursor_y = 0;
                        vga_set_cursor(cursor_y, cursor_x);
                    } else {
                        term_puts("Unknown command.\n");
                    }
                }
                cmdlen = 0;
                term_puts("> ");
            } else if (key == KEY_BACKSPACE || key == '\b') {
                if (cmdlen > 0) {
                    cmdlen--;
                    term_putc('\b', 0x07);
                }
            } else if (key >= 32 && key <= 126 && cmdlen < 255) {
                cmdbuf[cmdlen++] = key;
                term_putc((char)key, 0x07);
            }
        }
    }
}
