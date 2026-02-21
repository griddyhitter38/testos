#ifndef AHCI_H
#define AHCI_H

#include "drivers/block.h"

int ahci_init(BlockDevice *out_dev);
void ahci_print_info(void);

#endif
