#include "drivers/block.h"
#include "drivers/ahci.h"
#include "drivers/nvme.h"

static BlockDevice g_block;
static int g_has_block = 0;

int block_init(void) {
  g_has_block = 0;
  if (nvme_init(&g_block)) {
    g_has_block = 1;
    return 1;
  }
  if (ahci_init(&g_block)) {
    g_has_block = 1;
    return 1;
  }
  return 0;
}

const BlockDevice *block_get(void) {
  return g_has_block ? &g_block : 0;
}

int block_read(uint64_t lba, uint32_t count, void *out) {
  if (!g_has_block || !g_block.read) {
    return 0;
  }
  return g_block.read(lba, count, out);
}
