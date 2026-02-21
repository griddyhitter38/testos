#include "drivers/ahci.h"
#include "console.h"
#include "drivers/pci.h"
#include <stdint.h>

typedef struct {
  int present;
  uint8_t bus;
  uint8_t dev;
  uint8_t func;
  uint64_t abar;
  uint32_t hba_pi;
  uint32_t hba_ghc;
  uint32_t hba_cap;
} AhciInfo;

static AhciInfo g_ahci;

typedef volatile struct {
  uint32_t clb;
  uint32_t clbu;
  uint32_t fb;
  uint32_t fbu;
  uint32_t is;
  uint32_t ie;
  uint32_t cmd;
  uint32_t rsv0;
  uint32_t tfd;
  uint32_t sig;
  uint32_t ssts;
  uint32_t sctl;
  uint32_t serr;
  uint32_t sact;
  uint32_t ci;
  uint32_t sntf;
  uint32_t fbs;
  uint32_t rsv1[11];
  uint32_t vendor[4];
} HbaPort;

typedef volatile struct {
  uint32_t cap;
  uint32_t ghc;
  uint32_t is;
  uint32_t pi;
  uint32_t vs;
  uint32_t ccc_ctl;
  uint32_t ccc_pts;
  uint32_t em_loc;
  uint32_t em_ctl;
  uint32_t cap2;
  uint32_t bohc;
  uint8_t  rsv[0xA0 - 0x2C];
  HbaPort  ports[32];
} HbaMem;

typedef struct {
  uint8_t  cfis[64];
  uint8_t  acmd[16];
  uint8_t  rsv[48];
  struct {
    uint32_t dba;
    uint32_t dbau;
    uint32_t rsv0;
    uint32_t dbc_i;
  } prdt[1];
} HbaCmdTable;

typedef struct {
  uint16_t flags;
  uint16_t prdtl;
  uint32_t prdbc;
  uint32_t ctba;
  uint32_t ctbau;
  uint32_t rsv[4];
} HbaCmdHeader;

static HbaMem *g_hba = 0;
static HbaPort *g_port = 0;

static uint8_t g_cmd_list[1024] __attribute__((aligned(1024)));
static uint8_t g_fis[256] __attribute__((aligned(256)));
static uint8_t g_cmd_table[256] __attribute__((aligned(128)));

static inline uint32_t mmio_read32(uint64_t base, uint32_t offset) {
  volatile uint32_t *addr = (volatile uint32_t *)(uintptr_t)(base + offset);
  return *addr;
}

static void stop_port(HbaPort *port) {
  port->cmd &= ~0x01u; // ST
  while (port->cmd & 0x8000u) {
  }
  port->cmd &= ~0x10u; // FRE
  while (port->cmd & 0x4000u) {
  }
}

static void start_port(HbaPort *port) {
  port->cmd |= 0x10u; // FRE
  port->cmd |= 0x01u; // ST
}

static int ahci_read_lba(uint64_t lba, uint32_t count, void *out) {
  if (!g_port || !out || count == 0) {
    return 0;
  }

  // Wait until port is not busy.
  while (g_port->tfd & (0x80u | 0x08u)) {
  }

  g_port->is = 0xFFFFFFFFu;

  HbaCmdHeader *hdr = (HbaCmdHeader *)g_cmd_list;
  hdr->flags = 0;
  hdr->flags |= (5u << 0);     // CFL = 5 dwords
  hdr->flags &= ~(1u << 6);    // W = 0 (read)
  hdr->prdtl = 1;
  hdr->prdbc = 0;
  hdr->ctba = (uint32_t)(uintptr_t)g_cmd_table;
  hdr->ctbau = 0;

  HbaCmdTable *tbl = (HbaCmdTable *)g_cmd_table;
  for (uint32_t i = 0; i < sizeof(HbaCmdTable); ++i) {
    ((uint8_t *)tbl)[i] = 0;
  }
  tbl->prdt[0].dba = (uint32_t)(uintptr_t)out;
  tbl->prdt[0].dbau = 0;
  tbl->prdt[0].dbc_i = (count * 512u) - 1u;

  uint8_t *cfis = tbl->cfis;
  cfis[0] = 0x27; // FIS type: Reg H2D
  cfis[1] = 1 << 7; // C
  cfis[2] = 0x25; // READ DMA EXT
  cfis[3] = 0;
  cfis[4] = (uint8_t)(lba & 0xFF);
  cfis[5] = (uint8_t)((lba >> 8) & 0xFF);
  cfis[6] = (uint8_t)((lba >> 16) & 0xFF);
  cfis[7] = 1 << 6; // device, LBA
  cfis[8] = (uint8_t)((lba >> 24) & 0xFF);
  cfis[9] = (uint8_t)((lba >> 32) & 0xFF);
  cfis[10] = (uint8_t)((lba >> 40) & 0xFF);
  cfis[11] = 0;
  cfis[12] = (uint8_t)(count & 0xFF);
  cfis[13] = (uint8_t)((count >> 8) & 0xFF);
  cfis[14] = 0;
  cfis[15] = 0;

  g_port->ci = 1u;

  // Wait for completion.
  uint32_t spins = 1000000;
  while ((g_port->ci & 1u) && spins--) {
  }
  if (g_port->is & (1u << 30)) {
    return 0;
  }
  return spins != 0;
}

static int ahci_identify(uint16_t *out_words) {
  if (!g_port || !out_words) {
    return 0;
  }
  while (g_port->tfd & (0x80u | 0x08u)) {
  }
  g_port->is = 0xFFFFFFFFu;

  HbaCmdHeader *hdr = (HbaCmdHeader *)g_cmd_list;
  hdr->flags = 0;
  hdr->flags |= (5u << 0);
  hdr->flags &= ~(1u << 6);
  hdr->prdtl = 1;
  hdr->prdbc = 0;
  hdr->ctba = (uint32_t)(uintptr_t)g_cmd_table;
  hdr->ctbau = 0;

  HbaCmdTable *tbl = (HbaCmdTable *)g_cmd_table;
  for (uint32_t i = 0; i < sizeof(HbaCmdTable); ++i) {
    ((uint8_t *)tbl)[i] = 0;
  }
  tbl->prdt[0].dba = (uint32_t)(uintptr_t)out_words;
  tbl->prdt[0].dbau = 0;
  tbl->prdt[0].dbc_i = (512u) - 1u;

  uint8_t *cfis = tbl->cfis;
  cfis[0] = 0x27;
  cfis[1] = 1 << 7;
  cfis[2] = 0xEC; // IDENTIFY DEVICE
  cfis[3] = 0;
  cfis[4] = 0;
  cfis[5] = 0;
  cfis[6] = 0;
  cfis[7] = 0;
  cfis[8] = 0;
  cfis[9] = 0;
  cfis[10] = 0;
  cfis[11] = 0;
  cfis[12] = 0;
  cfis[13] = 0;
  cfis[14] = 0;
  cfis[15] = 0;

  g_port->ci = 1u;
  uint32_t spins = 1000000;
  while ((g_port->ci & 1u) && spins--) {
  }
  if (g_port->is & (1u << 30)) {
    return 0;
  }
  return spins != 0;
}

int ahci_init(BlockDevice *out_dev) {
  g_ahci.present = 0;
  if (!out_dev) {
    return 0;
  }

  // AHCI: class 0x01, subclass 0x06, progIF 0x01
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
        if (class_code == 0x01 && subclass == 0x06 && prog_if == 0x01) {
          uint32_t bar5 = pci_read32(bus, dev, func, 0x24);
          uint64_t abar = (uint64_t)(bar5 & ~0xFu);
          g_ahci.present = 1;
          g_ahci.bus = (uint8_t)bus;
          g_ahci.dev = dev;
          g_ahci.func = func;
          g_ahci.abar = abar;
          g_ahci.hba_cap = mmio_read32(abar, 0x00);
          g_ahci.hba_ghc = mmio_read32(abar, 0x04);
          g_ahci.hba_pi = mmio_read32(abar, 0x0C);

          g_hba = (HbaMem *)(uintptr_t)abar;

          uint32_t pi = g_hba->pi;
          for (int port = 0; port < 32; ++port) {
            if (!(pi & (1u << port))) {
              continue;
            }
            HbaPort *p = &g_hba->ports[port];
            uint32_t ssts = p->ssts;
            uint8_t det = ssts & 0x0F;
            uint8_t ipm = (ssts >> 8) & 0x0F;
            if (det != 3 || ipm != 1) {
              continue;
            }
            if (p->sig != 0x00000101) {
              continue;
            }
            stop_port(p);
            p->clb = (uint32_t)(uintptr_t)g_cmd_list;
            p->clbu = 0;
            p->fb = (uint32_t)(uintptr_t)g_fis;
            p->fbu = 0;
            for (uint32_t i = 0; i < sizeof(g_cmd_list); ++i) {
              g_cmd_list[i] = 0;
            }
            for (uint32_t i = 0; i < sizeof(g_fis); ++i) {
              g_fis[i] = 0;
            }
            for (uint32_t i = 0; i < sizeof(g_cmd_table); ++i) {
              g_cmd_table[i] = 0;
            }
            start_port(p);
            g_port = p;

            out_dev->name = "ahci";
            out_dev->block_size = 512;
            out_dev->block_count = 0;
            out_dev->read = ahci_read_lba;

            uint16_t identify[256];
            if (ahci_identify(identify)) {
              uint64_t lba_count = ((uint64_t)identify[100]) |
                                   ((uint64_t)identify[101] << 16) |
                                   ((uint64_t)identify[102] << 32) |
                                   ((uint64_t)identify[103] << 48);
              if (lba_count != 0) {
                out_dev->block_count = lba_count;
              }
            }
            return 1;
          }
          return 0;
        }
      }
    }
  }
  return 0;
}

void ahci_print_info(void) {
  if (!g_ahci.present) {
    console_write_line("AHCI: not found");
    return;
  }
  console_write("AHCI: bus ");
  console_putc('0' + (g_ahci.bus / 100));
  console_putc('0' + ((g_ahci.bus / 10) % 10));
  console_putc('0' + (g_ahci.bus % 10));
  console_write(" dev ");
  console_putc('0' + (g_ahci.dev / 10));
  console_putc('0' + (g_ahci.dev % 10));
  console_write(" func ");
  console_putc('0' + (g_ahci.func % 10));
  console_putc('\n');

  console_write("ABAR=0x");
  // Print low 32 bits only for now.
  for (int i = 0; i < 8; ++i) {
    uint8_t nibble = (g_ahci.abar >> (28 - 4 * i)) & 0xF;
    console_putc((nibble < 10) ? (char)('0' + nibble) : (char)('A' + (nibble - 10)));
  }
  console_putc('\n');

  console_write("CAP=0x");
  for (int i = 0; i < 8; ++i) {
    uint8_t nibble = (g_ahci.hba_cap >> (28 - 4 * i)) & 0xF;
    console_putc((nibble < 10) ? (char)('0' + nibble) : (char)('A' + (nibble - 10)));
  }
  console_write(" GHC=0x");
  for (int i = 0; i < 8; ++i) {
    uint8_t nibble = (g_ahci.hba_ghc >> (28 - 4 * i)) & 0xF;
    console_putc((nibble < 10) ? (char)('0' + nibble) : (char)('A' + (nibble - 10)));
  }
  console_write(" PI=0x");
  for (int i = 0; i < 8; ++i) {
    uint8_t nibble = (g_ahci.hba_pi >> (28 - 4 * i)) & 0xF;
    console_putc((nibble < 10) ? (char)('0' + nibble) : (char)('A' + (nibble - 10)));
  }
  console_putc('\n');
}
