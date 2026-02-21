#ifndef KERNEL_H
#define KERNEL_H

#include <stdint.h>
#include <stddef.h>

typedef struct {
  uint8_t *base;
  size_t size;
  uint32_t width;
  uint32_t height;
  uint32_t pixels_per_scanline;
  uint32_t pixel_format;
} FrameBuffer;

typedef struct BootInfo {
  FrameBuffer fb;
} BootInfo;

void kernel_main(struct BootInfo *info);

extern BootInfo g_boot_info;

#endif
