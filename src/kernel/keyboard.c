#include "keyboard.h"
#include <stdint.h>

static inline void outb(uint16_t port, uint8_t val) {
  __asm__ __volatile__("outb %0, %1" : : "a"(val), "Nd"(port));
}

static inline uint8_t inb(uint16_t port) {
  uint8_t ret;
  __asm__ __volatile__("inb %1, %0" : "=a"(ret) : "Nd"(port));
  return ret;
}

static inline void io_wait(void) {
  outb(0x80, 0);
}

static const char scancode_set1[128] = {
  0,  27, '1','2','3','4','5','6','7','8','9','0','-','=', '\b',
  '\t','q','w','e','r','t','y','u','i','o','p','[',']','\n', 0,
  'a','s','d','f','g','h','j','k','l',';','\'','`', 0, '\\',
  'z','x','c','v','b','n','m',',','.','/', 0, '*', 0, ' ',
  0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
  0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
  0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
  0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0
};

void keyboard_init(void) {
  // Drain output buffer.
  while (inb(0x64) & 0x01) {
    (void)inb(0x60);
    io_wait();
  }
}

char keyboard_getchar(void) {
  for (;;) {
    if (inb(0x64) & 0x01) {
      uint8_t sc = inb(0x60);
      if (sc & 0x80) {
        continue; // key release
      }
      char c = scancode_set1[sc];
      if (c) {
        return c;
      }
    }
  }
}
