#include "vga.h"
#include "keyboard.h"
#include "terminal.h"
#include "types.h"

void gdt_init(void); void idt_init(void);
void pmm_init(uint32_t);
void scheduler_init(void);
void fat32_init(void);
void net_stack_init(void);
void web_stack_init(void);
void kernel_main(void) {
    gdt_init();
    idt_init();
    pmm_init(0x4000000); // 64MB memory
    scheduler_init();
    fat32_init();
    net_stack_init();
    web_stack_init();
    vga_init();
    keyboard_init();
    terminal_run();
    for (;;)
        __asm__ volatile("hlt");
}
