#ifndef BLOCK_H
#define BLOCK_H

#include <stdint.h>

typedef struct BlockDevice {
  const char *name;
  uint64_t block_size;
  uint64_t block_count;
  int (*read)(uint64_t lba, uint32_t count, void *out);
} BlockDevice;

int block_init(void);
const BlockDevice *block_get(void);
int block_read(uint64_t lba, uint32_t count, void *out);

#endif
