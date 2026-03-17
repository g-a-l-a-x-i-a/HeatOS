#include "terminal.h"
#include "io.h"

void cmd_reboot(const char *args) {
    (void)args;
    term_puts("Rebooting...\n");
    
    for (volatile int i = 0; i < 1000000; i++);
    
    outb(0xCF9, 0x02);
    outb(0xCF9, 0x06);
    
    __asm__ volatile("hlt");
}
