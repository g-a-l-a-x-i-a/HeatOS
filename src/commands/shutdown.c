#include "terminal.h"
#include "io.h"

void cmd_shutdown(const char *args) {
    (void)args;
    term_puts("Shutting down...\n");
    
    for (volatile int i = 0; i < 1000000; i++);
    
    outb(0xCF9, 0x00);
    outb(0xCF9, 0x0E);
    
    __asm__ volatile("hlt");
}
