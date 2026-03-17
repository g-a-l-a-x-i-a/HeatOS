#include "scheduler.h"
#include "mm.h"

static process_t* current_process = 0;
static process_t* process_list = 0;
static uint32_t next_pid = 1;

void scheduler_init(void) {
    // Current kernel execution becomes PID 0
    process_t* kernel_proc = (process_t*)kmalloc(sizeof(process_t));
    kernel_proc->id = 0;
    kernel_proc->state = 1;
    kernel_proc->next = kernel_proc;
    
    current_process = kernel_proc;
    process_list = kernel_proc;
}

int create_process(void (*entry_point)()) {
    process_t* new_proc = (process_t*)kmalloc(sizeof(process_t));
    new_proc->id = next_pid++;
    new_proc->state = 1;
    
    // Allocate stack
    void* stack = pmm_alloc_page();
    uint32_t* stack_top = (uint32_t*)((uint32_t)stack + 4096);
    
    // Fake context for first switch
    *(--stack_top) = (uint32_t)entry_point; // EIP
    *(--stack_top) = 0; // EBP
    *(--stack_top) = 0; // ESP
    *(--stack_top) = 0; // EBX
    *(--stack_top) = 0; // ESI
    *(--stack_top) = 0; // EDI
    
    new_proc->esp = (uint32_t)stack_top;
    
    // Insert into ring
    new_proc->next = process_list->next;
    process_list->next = new_proc;
    
    return new_proc->id;
}

void yield(void) {
    if (!current_process || current_process == current_process->next) return;
    
    // Minimal round-robin queue tick
    current_process = current_process->next;
    
    // In a real OS we'd invoke the context switch ASM routine here
}
