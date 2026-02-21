#include "console.h"
#include "font.h"

static FrameBuffer *g_fb = NULL;
static uint32_t g_fg = 0xFFFFFFFF;
static uint32_t g_bg = 0x00000000;
static uint32_t g_cols = 0;
static uint32_t g_rows = 0;
static uint32_t g_cursor_x = 0;
static uint32_t g_cursor_y = 0;
static uint32_t g_top_margin_rows = 1;
static const char *g_header_text = "TestOS";

static inline uint32_t make_pixel(uint8_t r, uint8_t g, uint8_t b,
                                  uint32_t format) {
  // 0: PixelRedGreenBlueReserved8BitPerColor
  // 1: PixelBlueGreenRedReserved8BitPerColor
  if (format == 0) {
    return ((uint32_t)b << 16) | ((uint32_t)g << 8) | (uint32_t)r;
  }
  return ((uint32_t)r << 16) | ((uint32_t)g << 8) | (uint32_t)b;
}

static void fill_rect(uint32_t x, uint32_t y, uint32_t w, uint32_t h,
                      uint32_t color) {
  if (!g_fb || !g_fb->base) {
    return;
  }
  if (x >= g_fb->width || y >= g_fb->height) {
    return;
  }
  uint32_t max_x = x + w;
  uint32_t max_y = y + h;
  if (max_x > g_fb->width) {
    max_x = g_fb->width;
  }
  if (max_y > g_fb->height) {
    max_y = g_fb->height;
  }
  uint32_t *pixels = (uint32_t *)g_fb->base;
  for (uint32_t py = y; py < max_y; ++py) {
    uint32_t row = py * g_fb->pixels_per_scanline;
    for (uint32_t px = x; px < max_x; ++px) {
      pixels[row + px] = color;
    }
  }
}

static void scroll_if_needed(void) {
  if (g_cursor_y < g_rows) {
    return;
  }
  uint32_t *pixels = (uint32_t *)g_fb->base;
  uint32_t row_pixels = g_fb->pixels_per_scanline;
  uint32_t char_h = 16;
  uint32_t scroll_px = char_h;
  uint32_t total_rows = g_fb->height;
  uint32_t start_y = g_top_margin_rows * char_h;

  for (uint32_t y = start_y; y < total_rows - scroll_px; ++y) {
    uint32_t src = (y + scroll_px) * row_pixels;
    uint32_t dst = y * row_pixels;
    for (uint32_t x = 0; x < g_fb->width; ++x) {
      pixels[dst + x] = pixels[src + x];
    }
  }
  fill_rect(0, total_rows - scroll_px, g_fb->width, scroll_px, g_bg);
  if (g_cursor_y > 0) {
    g_cursor_y--;
  }
}

void console_init(FrameBuffer *fb) {
  g_fb = fb;
  g_fg = make_pixel(0xE0, 0xE0, 0xE0, fb->pixel_format);
  g_bg = make_pixel(0x10, 0x10, 0x10, fb->pixel_format);
  g_cols = fb->width / 8;
  g_rows = (fb->height / 16) - g_top_margin_rows;
  g_cursor_x = 0;
  g_cursor_y = 0;
  console_clear();
}

void console_clear(void) {
  if (!g_fb) {
    return;
  }
  fill_rect(0, 0, g_fb->width, g_fb->height, g_bg);
  g_cursor_x = 0;
  g_cursor_y = 0;
  if (g_header_text) {
    console_set_header(g_header_text);
  }
}

void console_putc(char c) {
  if (!g_fb) {
    return;
  }
  if (c == '\n') {
    g_cursor_x = 0;
    g_cursor_y++;
    scroll_if_needed();
    return;
  }
  if (c == '\r') {
    g_cursor_x = 0;
    return;
  }
  if (c == '\b') {
    if (g_cursor_x > 0) {
      g_cursor_x--;
    }
    return;
  }
  if (c == '\t') {
    g_cursor_x = (g_cursor_x + 4) & ~3u;
    return;
  }
  if (c < 0 || c > 127) {
    c = '?';
  }

  uint32_t px = g_cursor_x * 8;
  uint32_t py = (g_cursor_y + g_top_margin_rows) * 16;

  for (uint32_t row = 0; row < 16; ++row) {
    const unsigned char *glyph = font8x8_basic[(unsigned char)c];
    unsigned char bits = glyph[row >> 1];
    for (uint32_t col = 0; col < 8; ++col) {
      uint32_t color = (bits & (1u << (7 - col))) ? g_fg : g_bg;
      uint32_t x = px + col;
      uint32_t y = py + row;
      if (x < g_fb->width && y < g_fb->height) {
        uint32_t *pixels = (uint32_t *)g_fb->base;
        pixels[y * g_fb->pixels_per_scanline + x] = color;
      }
    }
  }

  g_cursor_x++;
  if (g_cursor_x >= g_cols) {
    g_cursor_x = 0;
    g_cursor_y++;
    scroll_if_needed();
  }
}

void console_write(const char *s) {
  if (!s) {
    return;
  }
  while (*s) {
    console_putc(*s++);
  }
}

void console_write_line(const char *s) {
  console_write(s);
  console_putc('\n');
}

void console_set_header(const char *text) {
  g_header_text = text;
  if (!g_fb || !text) {
    return;
  }
  // Clear header band.
  fill_rect(0, 0, g_fb->width, 16, g_bg);

  // Compute centered position.
  uint32_t len = 0;
  while (text[len]) {
    len++;
  }
  uint32_t total_cols = g_fb->width / 8;
  uint32_t start_col = 0;
  if (len < total_cols) {
    start_col = (total_cols - len) / 2;
  }

  // Draw header text on the top row without top-margin offset.
  for (uint32_t i = 0; i < len; ++i) {
    unsigned char c = (unsigned char)text[i];
    if (c > 127) {
      c = '?';
    }
    const unsigned char *glyph = font8x8_basic[c];
    uint32_t px = (start_col + i) * 8;
    uint32_t py = 0;
    for (uint32_t row = 0; row < 16; ++row) {
      unsigned char bits = glyph[row >> 1];
      for (uint32_t col = 0; col < 8; ++col) {
        uint32_t color = (bits & (1u << (7 - col))) ? g_fg : g_bg;
        uint32_t x = px + col;
        uint32_t y = py + row;
        if (x < g_fb->width && y < g_fb->height) {
          uint32_t *pixels = (uint32_t *)g_fb->base;
          pixels[y * g_fb->pixels_per_scanline + x] = color;
        }
      }
    }
  }
}
