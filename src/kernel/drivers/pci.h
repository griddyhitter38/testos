#ifndef PCI_H
#define PCI_H

#include <stdint.h>

uint32_t pci_read32(uint8_t bus, uint8_t dev, uint8_t func, uint8_t offset);

typedef struct {
  uint8_t bus;
  uint8_t dev;
  uint8_t func;
  uint64_t bar0;
} PciXhciDevice;

int pci_find_xhci(PciXhciDevice *out_dev);

#endif
