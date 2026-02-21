#include "drivers/xhci.h"
#include "drivers/pci.h"
#include "console.h"
#include <stdint.h>

static inline uint32_t mmio_read32(uint64_t base, uint32_t offset) {
  volatile uint32_t *addr = (volatile uint32_t *)(uintptr_t)(base + offset);
  return *addr;
}

static inline void mmio_write32(uint64_t base, uint32_t offset, uint32_t v) {
  volatile uint32_t *addr = (volatile uint32_t *)(uintptr_t)(base + offset);
  *addr = v;
}

static void console_write_hex32(uint32_t v) {
  char hex[9];
  for (int i = 0; i < 8; ++i) {
    uint8_t nibble = (v >> (28 - 4 * i)) & 0xF;
    hex[i] = (nibble < 10) ? (char)('0' + nibble) : (char)('A' + (nibble - 10));
  }
  hex[8] = 0;
  console_write(hex);
}

static int wait_for_mask(uint64_t base, uint32_t offset, uint32_t mask,
                         uint32_t expected, uint32_t spins) {
  while (spins--) {
    uint32_t v = mmio_read32(base, offset);
    if ((v & mask) == expected) {
      return 1;
    }
  }
  return 0;
}

typedef struct {
  int present;
  uint8_t caplength;
  uint16_t hci_version;
  uint32_t hcsparams1;
  uint32_t hccparams1;
  uint32_t dboff;
  uint32_t rtsoff;
  uint32_t max_ports;
  uint32_t portsc[8];
  uint64_t mmio_base;
  uint8_t bus;
  uint8_t dev;
  uint8_t func;
} XhciInfo;

static XhciInfo g_xhci;

int xhci_init(void) {
  g_xhci.present = 0;
  PciXhciDevice dev;
  if (!pci_find_xhci(&dev)) {
    return 0;
  }
  g_xhci.present = 1;
  g_xhci.bus = dev.bus;
  g_xhci.dev = dev.dev;
  g_xhci.func = dev.func;

  uint64_t base = dev.bar0;
  g_xhci.mmio_base = base;

  uint32_t caplength_hciversion = mmio_read32(base, 0x00);
  uint8_t caplength = caplength_hciversion & 0xFF;
  uint16_t hci_version = (caplength_hciversion >> 16) & 0xFFFF;
  uint32_t hcsparams1 = mmio_read32(base, 0x04);
  uint32_t hccparams1 = mmio_read32(base, 0x10);
  uint32_t dboff = mmio_read32(base, 0x14);
  uint32_t rtsoff = mmio_read32(base, 0x18);

  g_xhci.caplength = caplength;
  g_xhci.hci_version = hci_version;
  g_xhci.hcsparams1 = hcsparams1;
  g_xhci.hccparams1 = hccparams1;
  g_xhci.dboff = dboff;
  g_xhci.rtsoff = rtsoff;

  uint64_t op_base = base + caplength;
  const uint32_t USBCMD = 0x00;
  const uint32_t USBSTS = 0x04;

  // Stop the controller if running.
  uint32_t cmd = mmio_read32(op_base, USBCMD);
  cmd &= ~1u;
  mmio_write32(op_base, USBCMD, cmd);
  wait_for_mask(op_base, USBSTS, 1u, 1u, 1000000);

  // Reset controller.
  cmd = mmio_read32(op_base, USBCMD);
  cmd |= (1u << 1);
  mmio_write32(op_base, USBCMD, cmd);
  if (!wait_for_mask(op_base, USBCMD, (1u << 1), 0u, 1000000)) {
    return 0;
  }

  uint32_t max_ports = (hcsparams1 >> 24) & 0xFF;
  g_xhci.max_ports = max_ports;

  // Port status registers are at op_base + 0x400, 0x10 per port.
  for (uint32_t p = 0; p < max_ports && p < 8; ++p) {
    uint32_t portsc = mmio_read32(op_base, 0x400 + p * 0x10);
    g_xhci.portsc[p] = portsc;
  }

  return 1;
}

void xhci_print_info(void) {
  if (!g_xhci.present) {
    console_write_line("xHCI: not found");
    return;
  }

  console_write("xHCI: bus ");
  console_putc('0' + (g_xhci.bus / 100));
  console_putc('0' + ((g_xhci.bus / 10) % 10));
  console_putc('0' + (g_xhci.bus % 10));
  console_write(" dev ");
  console_putc('0' + (g_xhci.dev / 10));
  console_putc('0' + (g_xhci.dev % 10));
  console_write(" func ");
  console_putc('0' + (g_xhci.func % 10));
  console_putc('\n');

  console_write("xHCI MMIO base 0x");
  console_write_hex32((uint32_t)(g_xhci.mmio_base >> 32));
  console_write_hex32((uint32_t)g_xhci.mmio_base);
  console_putc('\n');

  console_write("caplength=0x");
  console_write_hex32(g_xhci.caplength);
  console_write(" hci=0x");
  console_write_hex32(g_xhci.hci_version);
  console_putc('\n');

  console_write("hcsparams1=0x");
  console_write_hex32(g_xhci.hcsparams1);
  console_write(" hccparams1=0x");
  console_write_hex32(g_xhci.hccparams1);
  console_putc('\n');

  console_write("dboff=0x");
  console_write_hex32(g_xhci.dboff);
  console_write(" rtsoff=0x");
  console_write_hex32(g_xhci.rtsoff);
  console_putc('\n');

  console_write("ports=");
  console_write_hex32(g_xhci.max_ports);
  console_putc('\n');

  for (uint32_t p = 0; p < g_xhci.max_ports && p < 8; ++p) {
    console_write("port");
    console_putc('0' + (p + 1));
    console_write("=0x");
    console_write_hex32(g_xhci.portsc[p]);
    console_putc('\n');
  }
}
