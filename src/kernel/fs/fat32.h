#ifndef FAT32_H
#define FAT32_H

#include <stdint.h>

int fat32_mount(void);
void fat32_print_info(void);
int fat32_list_root(void);
int fat32_read_file(const char *name, void *out, uint32_t max_bytes, uint32_t *out_size);

#endif
