#include "mm.h"

static uint8_t* pmm_bitmap;
static uint32_t pmm_max_blocks;
static uint32_t pmm_used_blocks;

static void pmm_bitmap_set(uint32_t bit) {
    pmm_bitmap[bit / 8] |= (1 << (bit % 8));
}
static void pmm_bitmap_clear(uint32_t bit) {
    pmm_bitmap[bit / 8] &= ~(1 << (bit % 8));
}
static bool pmm_bitmap_test(uint32_t bit) {
    return pmm_bitmap[bit / 8] & (1 << (bit % 8));
}

void pmm_init(uint32_t mem_size) {
    pmm_max_blocks = mem_size / PAGE_SIZE;
    pmm_used_blocks = pmm_max_blocks; // Mark all used initially
    
    // We'll put the bitmap right after the kernel
    pmm_bitmap = (uint8_t*)0x100000; // Simplification for bare metal
    
    for (uint32_t i = 0; i < pmm_max_blocks / 8; i++) {
        pmm_bitmap[i] = 0; // mark all free
    }
}

void* pmm_alloc_page(void) {
    for (uint32_t i = 0; i < pmm_max_blocks; i++) {
        if (!pmm_bitmap_test(i)) {
            pmm_bitmap_set(i);
            pmm_used_blocks++;
            return (void*)(i * PAGE_SIZE);
        }
    }
    return 0;
}

void pmm_free_page(void* p) {
    uint32_t block = (uint32_t)p / PAGE_SIZE;
    pmm_bitmap_clear(block);
    pmm_used_blocks--;
}

// Simple bump allocator for heap (no free)
static uint32_t heap_curr = HEAP_START;

void* kmalloc(uint32_t size) {
    uint32_t ret = heap_curr;
    heap_curr += size;
    return (void*)ret;
}

void kfree(void* p) {
    (void)p;
    // Unimplemented bump allocator free
}
