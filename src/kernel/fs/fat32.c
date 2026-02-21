#include "fs/fat32.h"
#include "drivers/block.h"
#include "console.h"
#include <stdint.h>

typedef struct {
  uint8_t jump[3];
  uint8_t oem[8];
  uint16_t bytes_per_sector;
  uint8_t sectors_per_cluster;
  uint16_t reserved_sectors;
  uint8_t fat_count;
  uint16_t root_entries;
  uint16_t total_sectors16;
  uint8_t media;
  uint16_t fat_size16;
  uint16_t sectors_per_track;
  uint16_t heads;
  uint32_t hidden_sectors;
  uint32_t total_sectors32;
  uint32_t fat_size32;
  uint16_t ext_flags;
  uint16_t fs_version;
  uint32_t root_cluster;
  uint16_t fs_info;
  uint16_t backup_boot;
  uint8_t reserved[12];
  uint8_t drive_number;
  uint8_t reserved1;
  uint8_t boot_sig;
  uint32_t volume_id;
  uint8_t volume_label[11];
  uint8_t fs_type[8];
} __attribute__((packed)) Fat32Bpb;

typedef struct {
  uint8_t name[11];
  uint8_t attr;
  uint8_t ntres;
  uint8_t crt_time_tenth;
  uint16_t crt_time;
  uint16_t crt_date;
  uint16_t acc_date;
  uint16_t fst_clus_hi;
  uint16_t wrt_time;
  uint16_t wrt_date;
  uint16_t fst_clus_lo;
  uint32_t file_size;
} __attribute__((packed)) FatDirEnt;

static int g_mounted = 0;
static Fat32Bpb g_bpb;
static uint32_t g_fat_start_lba = 0;
static uint32_t g_data_start_lba = 0;
static uint32_t g_part_lba = 0;

typedef struct {
  uint8_t type[16];
  uint8_t guid[16];
  uint64_t first_lba;
  uint64_t last_lba;
  uint64_t attrs;
  uint16_t name[36];
} __attribute__((packed)) GptEntry;

static const uint8_t gpt_esp_type[16] = {
  0x28,0x73,0x2A,0xC1,0x1F,0xF8,0xD2,0x11,
  0xBA,0x4B,0x00,0xA0,0xC9,0x3E,0xC9,0x3B
};

static uint32_t cluster_to_lba(uint32_t cluster) {
  return g_part_lba + g_data_start_lba + (cluster - 2) * g_bpb.sectors_per_cluster;
}

static int read_sector(uint32_t lba, void *out) {
  return block_read(lba, 1, out);
}

static int gpt_find_esp(uint32_t *out_lba) {
  uint8_t hdr[512];
  if (!read_sector(1, hdr)) {
    return 0;
  }
  if (!(hdr[0] == 'E' && hdr[1] == 'F' && hdr[2] == 'I' && hdr[3] == ' ' &&
        hdr[4] == 'P' && hdr[5] == 'A' && hdr[6] == 'R' && hdr[7] == 'T')) {
    return 0;
  }
  uint64_t entries_lba = *(uint64_t *)(hdr + 0x48);
  uint32_t num_entries = *(uint32_t *)(hdr + 0x50);
  uint32_t entry_size = *(uint32_t *)(hdr + 0x54);
  if (entry_size < sizeof(GptEntry)) {
    return 0;
  }
  uint8_t sec[512];
  uint32_t entries_per_sector = 512 / entry_size;
  for (uint32_t i = 0; i < num_entries; ++i) {
    uint32_t lba = (uint32_t)(entries_lba + (i / entries_per_sector));
    if (!read_sector(lba, sec)) {
      return 0;
    }
    uint32_t offset = (i % entries_per_sector) * entry_size;
    GptEntry *ent = (GptEntry *)(sec + offset);
    if (ent->type[0] == 0 && ent->type[1] == 0) {
      continue;
    }
    int match = 1;
    for (int k = 0; k < 16; ++k) {
      if (ent->type[k] != gpt_esp_type[k]) {
        match = 0;
        break;
      }
    }
    if (match) {
      *out_lba = (uint32_t)ent->first_lba;
      return 1;
    }
  }
  return 0;
}

int fat32_mount(void) {
  g_mounted = 0;
  const BlockDevice *dev = block_get();
  if (!dev || dev->block_size != 512) {
    return 0;
  }
  g_part_lba = 0;
  uint32_t esp_lba = 0;
  if (gpt_find_esp(&esp_lba)) {
    g_part_lba = esp_lba;
  }
  if (!read_sector(g_part_lba, &g_bpb)) {
    return 0;
  }
  if (!(g_bpb.bytes_per_sector == 512 && g_bpb.sectors_per_cluster != 0)) {
    return 0;
  }
  if (g_bpb.fat_size32 == 0) {
    return 0;
  }
  g_fat_start_lba = g_bpb.reserved_sectors;
  g_data_start_lba = g_bpb.reserved_sectors + g_bpb.fat_count * g_bpb.fat_size32;
  g_mounted = 1;
  return 1;
}

void fat32_print_info(void) {
  if (!g_mounted) {
    console_write_line("FAT32: not mounted");
    return;
  }
  console_write("FAT32: root=");
  char num[12];
  uint32_t v = g_bpb.root_cluster;
  int i = 0;
  if (v == 0) {
    num[i++] = '0';
  } else {
    char buf[12];
    int j = 0;
    while (v > 0) {
      buf[j++] = (char)('0' + (v % 10));
      v /= 10;
    }
    while (j > 0) {
      num[i++] = buf[--j];
    }
  }
  num[i] = 0;
  console_write_line(num);
}

static uint32_t fat_next_cluster(uint32_t cluster) {
  uint32_t fat_offset = cluster * 4;
  uint32_t fat_sector = g_fat_start_lba + (fat_offset / 512);
  uint32_t ent_offset = fat_offset % 512;
  uint8_t sec[512];
  if (!read_sector(fat_sector, sec)) {
    return 0x0FFFFFFF;
  }
  uint32_t val = *(uint32_t *)(sec + ent_offset);
  return val & 0x0FFFFFFF;
}

int fat32_list_root(void) {
  if (!g_mounted) {
    return 0;
  }
  uint32_t cluster = g_bpb.root_cluster;
  uint8_t sec[512];
  for (;;) {
    uint32_t lba = cluster_to_lba(cluster);
    for (uint32_t s = 0; s < g_bpb.sectors_per_cluster; ++s) {
      if (!read_sector(lba + s, sec)) {
        return 0;
      }
      for (uint32_t off = 0; off < 512; off += sizeof(FatDirEnt)) {
        FatDirEnt *ent = (FatDirEnt *)(sec + off);
        if (ent->name[0] == 0x00) {
          return 1;
        }
        if (ent->name[0] == 0xE5 || ent->attr == 0x0F) {
          continue;
        }
        char name[13];
        int idx = 0;
        for (int i = 0; i < 8; ++i) {
          if (ent->name[i] == ' ') {
            break;
          }
          name[idx++] = (char)ent->name[i];
        }
        if (ent->name[8] != ' ') {
          name[idx++] = '.';
          for (int i = 8; i < 11; ++i) {
            if (ent->name[i] == ' ') {
              break;
            }
            name[idx++] = (char)ent->name[i];
          }
        }
        name[idx] = 0;
        console_write_line(name);
      }
    }
    uint32_t next = fat_next_cluster(cluster);
    if (next >= 0x0FFFFFF8) {
      break;
    }
    cluster = next;
  }
  return 1;
}

int fat32_read_file(const char *name, void *out, uint32_t max_bytes, uint32_t *out_size) {
  if (!g_mounted || !name || !out || max_bytes == 0) {
    return 0;
  }
  uint32_t cluster = g_bpb.root_cluster;
  uint8_t sec[512];
  char target[11];
  for (int i = 0; i < 11; ++i) {
    target[i] = ' ';
  }
  int i = 0;
  int j = 0;
  while (name[i] && name[i] != '.' && j < 8) {
    char c = name[i++];
    if (c >= 'a' && c <= 'z') c -= 32;
    target[j++] = c;
  }
  if (name[i] == '.') {
    i++;
    j = 8;
    while (name[i] && j < 11) {
      char c = name[i++];
      if (c >= 'a' && c <= 'z') c -= 32;
      target[j++] = c;
    }
  }

  for (;;) {
    uint32_t lba = cluster_to_lba(cluster);
    for (uint32_t s = 0; s < g_bpb.sectors_per_cluster; ++s) {
      if (!read_sector(lba + s, sec)) {
        return 0;
      }
      for (uint32_t off = 0; off < 512; off += sizeof(FatDirEnt)) {
        FatDirEnt *ent = (FatDirEnt *)(sec + off);
        if (ent->name[0] == 0x00) {
          return 0;
        }
        if (ent->name[0] == 0xE5 || ent->attr == 0x0F) {
          continue;
        }
        int match = 1;
        for (int k = 0; k < 11; ++k) {
          if (ent->name[k] != (uint8_t)target[k]) {
            match = 0;
            break;
          }
        }
        if (!match) {
          continue;
        }
        uint32_t file_cluster = ((uint32_t)ent->fst_clus_hi << 16) | ent->fst_clus_lo;
        uint32_t remaining = ent->file_size;
        if (remaining > max_bytes) {
          remaining = max_bytes;
        }
        uint8_t *dst = (uint8_t *)out;
        while (remaining > 0 && file_cluster >= 2) {
          uint32_t file_lba = cluster_to_lba(file_cluster);
          for (uint32_t ss = 0; ss < g_bpb.sectors_per_cluster && remaining > 0; ++ss) {
            if (!read_sector(file_lba + ss, sec)) {
              return 0;
            }
            uint32_t copy = remaining < 512 ? remaining : 512;
            for (uint32_t n = 0; n < copy; ++n) {
              dst[n] = sec[n];
            }
            dst += copy;
            remaining -= copy;
          }
          uint32_t next = fat_next_cluster(file_cluster);
          if (next >= 0x0FFFFFF8) {
            break;
          }
          file_cluster = next;
        }
        if (out_size) {
          *out_size = ent->file_size;
        }
        return 1;
      }
    }
    uint32_t next = fat_next_cluster(cluster);
    if (next >= 0x0FFFFFF8) {
      break;
    }
    cluster = next;
  }
  return 0;
}
