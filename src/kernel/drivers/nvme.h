#ifndef NVME_H
#define NVME_H

#include "drivers/block.h"

int nvme_init(BlockDevice *out_dev);
void nvme_print_info(void);

#endif
