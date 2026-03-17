#ifndef SCHEDULER_H
#define SCHEDULER_H

#include "types.h"

typedef struct process {
    uint32_t id;
    uint32_t esp;
    uint32_t ebp;
    uint32_t eip;
    uint32_t state;
    struct process* next;
} process_t;

void scheduler_init(void);
int create_process(void (*entry_point)());
void yield(void);

#endif
