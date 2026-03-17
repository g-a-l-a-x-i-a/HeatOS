#ifndef FAT32_H
#define FAT32_H

#include "types.h"

void fat32_init(void);
int fat32_read_file(const char* filename, void* buffer, uint32_t* size);

#endif
