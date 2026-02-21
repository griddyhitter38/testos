#include "drivers/nvme.h"
#include "console.h"
#include "drivers/pci.h"
#include <stdint.h>

typedef struct {
  int present;
  uint8_t bus;
  uint8_t dev;
  uint8_t func;
  uint64_t bar0;
  uint32_t cap_lo;
  uint32_t cap_hi;
  uint32_t vs;
} NvmeInfo;

static NvmeInfo g_nvme;

static inline uint32_t mmio_read32(uint64_t base, uint32_t offset) {
  volatile uint32_t *addr = (volatile uint32_t *)(uintptr_t)(base + offset);
  return *addr;
}

static inline void mmio_write32(uint64_t base, uint32_t offset, uint32_t v) {
  volatile uint32_t *addr = (volatile uint32_t *)(uintptr_t)(base + offset);
  *addr = v;
}

static uint8_t g_nvme_admin_queue[4096 * 2] __attribute__((aligned(4096)));
static uint8_t g_nvme_cq[4096] __attribute__((aligned(4096)));
static uint8_t g_nvme_sq[4096] __attribute__((aligned(4096)));
static uint32_t g_nvme_sq_tail = 0;
static uint32_t g_nvme_cq_head = 0;
static uint16_t g_nvme_cq_phase = 1;

typedef struct {
  uint32_t cdw0;
  uint32_t nsid;
  uint64_t rsvd2;
  uint64_t mptr;
  uint64_t prp1;
  uint64_t prp2;
  uint32_t cdw10;
  uint32_t cdw11;
  uint32_t cdw12;
  uint32_t cdw13;
  uint32_t cdw14;
  uint32_t cdw15;
} NvmeCmd;

typedef struct {
  uint32_t cdw0;
  uint32_t rsvd1;
  uint16_t sq_head;
  uint16_t sq_id;
  uint16_t cid;
  uint16_t status;
} NvmeCpl;

static uint64_t g_nvme_bar = 0;
static uint32_t g_nvme_ns = 1;
static uint64_t g_nvme_blocks = 0;
static uint16_t g_nvme_cid = 10;

static int nvme_wait_cq(uint16_t cid) {
  volatile NvmeCpl *cq = (volatile NvmeCpl *)g_nvme_cq;
  uint32_t spins = 1000000;
  while (spins--) {
    NvmeCpl cpl = cq[g_nvme_cq_head];
    uint16_t phase = (cpl.status >> 15) & 1;
    if (phase == g_nvme_cq_phase) {
      if (cpl.cid == cid) {
        g_nvme_cq_head = (g_nvme_cq_head + 1) & 0x3F;
        if (g_nvme_cq_head == 0) {
          g_nvme_cq_phase ^= 1;
        }
        mmio_write32(g_nvme_bar, 0x1000 + 0x04, g_nvme_cq_head);
        return (cpl.status & 0x1) == 0;
      }
      g_nvme_cq_head = (g_nvme_cq_head + 1) & 0x3F;
      if (g_nvme_cq_head == 0) {
        g_nvme_cq_phase ^= 1;
      }
    }
  }
  return 0;
}

static int nvme_submit_cmd(NvmeCmd *cmd, uint16_t cid) {
  volatile NvmeCmd *sq = (volatile NvmeCmd *)g_nvme_sq;
  cmd->cdw0 = (cmd->cdw0 & 0xFFFF0000u) | cid;
  sq[g_nvme_sq_tail] = *cmd;
  g_nvme_sq_tail = (g_nvme_sq_tail + 1) & 0x3F;
  mmio_write32(g_nvme_bar, 0x1000 + 0x00, g_nvme_sq_tail);
  return nvme_wait_cq(cid);
}

static int nvme_read_lba(uint64_t lba, uint32_t count, void *out) {
  if (!out || count == 0) {
    return 0;
  }
  uint64_t bytes = (uint64_t)count * 512u;
  if (bytes > 4096) {
    return 0; // single-PRP only
  }

  NvmeCmd cmd;
  for (uint32_t i = 0; i < sizeof(NvmeCmd); ++i) {
    ((uint8_t *)&cmd)[i] = 0;
  }
  cmd.cdw0 = 0x02; // Read
  cmd.nsid = g_nvme_ns;
  cmd.prp1 = (uint64_t)(uintptr_t)out;
  cmd.cdw10 = (uint32_t)(lba & 0xFFFFFFFFu);
  cmd.cdw11 = (uint32_t)(lba >> 32);
  cmd.cdw12 = (count - 1) & 0xFFFFu;

  uint16_t cid = g_nvme_cid++;
  if (g_nvme_cid >= 0xFF) {
    g_nvme_cid = 10;
  }
  return nvme_submit_cmd(&cmd, cid);
}

int nvme_init(BlockDevice *out_dev) {
  g_nvme.present = 0;
  if (!out_dev) {
    return 0;
  }

  // NVMe: class 0x01, subclass 0x08, progIF 0x02
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
        if (class_code == 0x01 && subclass == 0x08 && prog_if == 0x02) {
          uint32_t bar0 = pci_read32(bus, dev, func, 0x10);
          uint32_t bar1 = pci_read32(bus, dev, func, 0x14);
          uint64_t base = (uint64_t)(bar0 & ~0xFu);
          if ((bar0 & 0x06u) == 0x04u) {
            base |= ((uint64_t)bar1) << 32;
          }
          g_nvme.present = 1;
          g_nvme.bus = (uint8_t)bus;
          g_nvme.dev = dev;
          g_nvme.func = func;
          g_nvme.bar0 = base;
          g_nvme.cap_lo = mmio_read32(base, 0x00);
          g_nvme.cap_hi = mmio_read32(base, 0x04);
          g_nvme.vs = mmio_read32(base, 0x08);

          g_nvme_bar = base;

          // Disable controller
          uint32_t cc = mmio_read32(base, 0x14);
          cc &= ~1u;
          mmio_write32(base, 0x14, cc);

          // Wait for CSTS.RDY=0
          uint32_t spins = 1000000;
          while ((mmio_read32(base, 0x1C) & 1u) && spins--) {
          }

          // Setup admin queues (64 entries)
          mmio_write32(base, 0x24, (uint32_t)((64 - 1) << 16) | (64 - 1));
          uint64_t asq = (uint64_t)(uintptr_t)g_nvme_sq;
          uint64_t acq = (uint64_t)(uintptr_t)g_nvme_cq;
          mmio_write32(base, 0x28, (uint32_t)asq);
          mmio_write32(base, 0x2C, (uint32_t)(asq >> 32));
          mmio_write32(base, 0x30, (uint32_t)acq);
          mmio_write32(base, 0x34, (uint32_t)(acq >> 32));

          g_nvme_sq_tail = 0;
          g_nvme_cq_head = 0;
          g_nvme_cq_phase = 1;

          // Enable controller (CC.EN=1)
          cc = mmio_read32(base, 0x14);
          cc |= 1u;
          mmio_write32(base, 0x14, cc);
          spins = 1000000;
          while (!(mmio_read32(base, 0x1C) & 1u) && spins--) {
          }
          if (spins == 0) {
            return 0;
          }

          // Identify controller (optional)
          uint8_t *id_buf = g_nvme_admin_queue;
          NvmeCmd cmd;
          for (uint32_t i = 0; i < sizeof(NvmeCmd); ++i) {
            ((uint8_t *)&cmd)[i] = 0;
          }
          cmd.cdw0 = 0x06; // Identify
          cmd.nsid = 0;
          cmd.prp1 = (uint64_t)(uintptr_t)id_buf;
          cmd.cdw10 = 1; // CNS=1 (controller)
          nvme_submit_cmd(&cmd, 1);

          // Identify namespace 1 to get size.
          for (uint32_t i = 0; i < 4096; ++i) {
            id_buf[i] = 0;
          }
          for (uint32_t i = 0; i < sizeof(NvmeCmd); ++i) {
            ((uint8_t *)&cmd)[i] = 0;
          }
          cmd.cdw0 = 0x06;
          cmd.nsid = 1;
          cmd.prp1 = (uint64_t)(uintptr_t)id_buf;
          cmd.cdw10 = 0; // CNS=0 (namespace)
          if (nvme_submit_cmd(&cmd, 2)) {
            uint64_t nsze = ((uint64_t *)id_buf)[0];
            g_nvme_blocks = nsze;
          }

          out_dev->name = "nvme";
          out_dev->block_size = 512;
          out_dev->block_count = g_nvme_blocks;
          out_dev->read = nvme_read_lba;
          return 1;
        }
      }
    }
  }
  return 0;
}

void nvme_print_info(void) {
  if (!g_nvme.present) {
    console_write_line("NVMe: not found");
    return;
  }
  console_write("NVMe: bus ");
  console_putc('0' + (g_nvme.bus / 100));
  console_putc('0' + ((g_nvme.bus / 10) % 10));
  console_putc('0' + (g_nvme.bus % 10));
  console_write(" dev ");
  console_putc('0' + (g_nvme.dev / 10));
  console_putc('0' + (g_nvme.dev % 10));
  console_write(" func ");
  console_putc('0' + (g_nvme.func % 10));
  console_putc('\n');

  console_write("BAR0=0x");
  for (int i = 0; i < 8; ++i) {
    uint8_t nibble = (g_nvme.bar0 >> (28 - 4 * i)) & 0xF;
    console_putc((nibble < 10) ? (char)('0' + nibble) : (char)('A' + (nibble - 10)));
  }
  console_putc('\n');

  console_write("CAP=0x");
  for (int i = 0; i < 8; ++i) {
    uint8_t nibble = (g_nvme.cap_hi >> (28 - 4 * i)) & 0xF;
    console_putc((nibble < 10) ? (char)('0' + nibble) : (char)('A' + (nibble - 10)));
  }
  for (int i = 0; i < 8; ++i) {
    uint8_t nibble = (g_nvme.cap_lo >> (28 - 4 * i)) & 0xF;
    console_putc((nibble < 10) ? (char)('0' + nibble) : (char)('A' + (nibble - 10)));
  }
  console_putc('\n');

  console_write("VS=0x");
  for (int i = 0; i < 8; ++i) {
    uint8_t nibble = (g_nvme.vs >> (28 - 4 * i)) & 0xF;
    console_putc((nibble < 10) ? (char)('0' + nibble) : (char)('A' + (nibble - 10)));
  }
  console_putc('\n');
}
