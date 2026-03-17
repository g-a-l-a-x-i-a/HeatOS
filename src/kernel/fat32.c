#include "fat32.h"
#include "string.h"
#include "terminal.h"

void fat32_init(void) {
    // Stub definition for FAT32 init parsing BPB
    // terminal_puts("FAT32 initialized.\n");
}

int fat32_read_file(const char* filename, void* buffer, uint32_t* size) {
    // Stub for FAT32 file reading logic
    if (strcmp(filename, "test.txt") == 0) {
        *size = 13;
        memcpy(buffer, "Hello FAT32!\n", 13);
        return 0;
    }
    return -1; // File not found
}
