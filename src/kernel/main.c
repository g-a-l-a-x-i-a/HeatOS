/*=============================================================================
 * HeatOS Kernel Main
 * 32-bit Protected Mode entry point, called from entry.asm
 *===========================================================================*/
#include "vga.h"
#include "keyboard.h"
#include "terminal.h"

void kernel_main(void) {
    vga_init();
    keyboard_init();
    terminal_run();     /* never returns */

    for (;;)
        __asm__ volatile("hlt");
}
