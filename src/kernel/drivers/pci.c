#include "pci.h"

static inline void outl(uint16_t port, uint32_t val) {
  __asm__ __volatile__("outl %0, %1" : : "a"(val), "Nd"(port));
}

static inline uint32_t inl(uint16_t port) {
  uint32_t ret;
  __asm__ __volatile__("inl %1, %0" : "=a"(ret) : "Nd"(port));
  return ret;
}

uint32_t pci_read32(uint8_t bus, uint8_t dev, uint8_t func, uint8_t offset) {
  uint32_t address = (1u << 31) |
                     ((uint32_t)bus << 16) |
                     ((uint32_t)dev << 11) |
                     ((uint32_t)func << 8) |
                     (offset & 0xFC);
  outl(0xCF8, address);
  return inl(0xCFC);
}

int pci_find_xhci(PciXhciDevice *out_dev) {
  if (!out_dev) {
    return 0;
  }
  for (uint16_t bus = 0; bus < 256; ++bus) {
    for (uint8_t dev = 0; dev < 32; ++dev) {
      for (uint8_t func = 0; func < 8; ++func) {
        uint32_t vendor_device = pci_read32(bus, dev, func, 0x00);
        if (vendor_device == 0xFFFFFFFFu) {
          if (func == 0) {
            break;
          }
          continue;
        }
        uint32_t class_reg = pci_read32(bus, dev, func, 0x08);
        uint8_t class_code = (class_reg >> 24) & 0xFF;
        uint8_t subclass = (class_reg >> 16) & 0xFF;
        uint8_t prog_if = (class_reg >> 8) & 0xFF;
        if (class_code == 0x0C && subclass == 0x03 && prog_if == 0x30) {
          uint32_t bar0 = pci_read32(bus, dev, func, 0x10);
          uint32_t bar1 = pci_read32(bus, dev, func, 0x14);
          uint64_t base = (uint64_t)(bar0 & ~0xFu);
          if ((bar0 & 0x06u) == 0x04u) {
            base |= ((uint64_t)bar1) << 32;
          }
          out_dev->bus = (uint8_t)bus;
          out_dev->dev = dev;
          out_dev->func = func;
          out_dev->bar0 = base;
          return 1;
        }
      }
    }
  }
  return 0;
}
