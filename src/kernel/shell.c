#include "shell.h"
#include "console.h"
#include "keyboard.h"
#include "kernel.h"
#include "font.h"
#include "drivers/xhci.h"
#include "drivers/ahci.h"
#include "drivers/nvme.h"
#include "drivers/block.h"
#include "fs/fat32.h"
#include <stdint.h>

static void u32_to_str(uint32_t v, char *out)
{
  char buf[11];
  int i = 0;
  if (v == 0)
  {
    out[0] = '0';
    out[1] = 0;
    return;
  }
  while (v > 0 && i < 10)
  {
    buf[i++] = (char)('0' + (v % 10));
    v /= 10;
  }
  int j = 0;
  while (i > 0)
  {
    out[j++] = buf[--i];
  }
  out[j] = 0;
}

static int streq(const char *a, const char *b)
{
  while (*a && *b)
  {
    if (*a != *b)
    {
      return 0;
    }
    a++;
    b++;
  }
  return *a == *b;
}

static void print_info(void)
{
  char num[12];
  console_write("fb: ");
  u32_to_str(g_boot_info.fb.width, num);
  console_write(num);
  console_write("x");
  u32_to_str(g_boot_info.fb.height, num);
  console_write(num);
  console_write_line("");
}

static void reboot(void)
{
  __asm__ __volatile__("outb %0, %1" : : "a"((uint8_t)0xFE), "Nd"((uint16_t)0x64));
  for (;;)
  {
    __asm__ __volatile__("hlt");
  }
}

static inline uint32_t make_pixel(uint8_t r, uint8_t g, uint8_t b,
                                  uint32_t format)
{
  // 0: PixelRedGreenBlueReserved8BitPerColor
  // 1: PixelBlueGreenRedReserved8BitPerColor
  if (format == 0)
  {
    return ((uint32_t)b << 16) | ((uint32_t)g << 8) | (uint32_t)r;
  }
  return ((uint32_t)r << 16) | ((uint32_t)g << 8) | (uint32_t)b;
}

static inline void fb_put_pixel(FrameBuffer *fb, int x, int y, uint32_t color)
{
  if (!fb || !fb->base)
  {
    return;
  }
  if ((uint32_t)x >= fb->width || (uint32_t)y >= fb->height)
  {
    return;
  }
  uint32_t *pixels = (uint32_t *)fb->base;
  pixels[(uint32_t)y * fb->pixels_per_scanline + (uint32_t)x] = color;
}

static void fb_fill(FrameBuffer *fb, uint32_t color)
{
  if (!fb || !fb->base)
  {
    return;
  }
  uint32_t *pixels = (uint32_t *)fb->base;
  for (uint32_t y = 0; y < fb->height; ++y)
  {
    uint32_t row = y * fb->pixels_per_scanline;
    for (uint32_t x = 0; x < fb->width; ++x)
    {
      pixels[row + x] = color;
    }
  }
}

static void draw_char_at(FrameBuffer *fb, int x, int y, char c,
                         uint32_t fg, uint32_t bg)
{
  if ((unsigned char)c > 127)
  {
    c = '?';
  }
  const unsigned char *glyph = font8x8_basic[(unsigned char)c];
  for (int row = 0; row < 16; ++row)
  {
    unsigned char bits = glyph[row >> 1];
    for (int col = 0; col < 8; ++col)
    {
      uint32_t color = (bits & (1u << (7 - col))) ? fg : bg;
      fb_put_pixel(fb, x + col, y + row, color);
    }
  }
}

static void draw_text_at(FrameBuffer *fb, int x, int y, const char *text,
                         uint32_t fg, uint32_t bg)
{
  if (!fb || !text)
  {
    return;
  }
  int cx = x;
  for (const char *p = text; *p; ++p)
  {
    draw_char_at(fb, cx, y, *p, fg, bg);
    cx += 8;
  }
}

static float fast_inv_sqrt(float number)
{
  float x2 = number * 0.5f;
  float y = number;
  uint32_t i = *(uint32_t *)&y;
  i = 0x5f3759df - (i >> 1);
  y = *(float *)&i;
  y = y * (1.5f - (x2 * y * y));
  return y;
}

static float fast_sin(float x)
{
  const float PI = 3.14159265f;
  const float TWO_PI = 6.28318530f;
  // Range reduce to [-PI, PI]
  while (x > PI)
    x -= TWO_PI;
  while (x < -PI)
    x += TWO_PI;
  // 5th-order approximation
  float x2 = x * x;
  return x * (1.0f - x2 / 6.0f + (x2 * x2) / 120.0f);
}

static float fast_cos(float x)
{
  const float PI = 3.14159265f;
  return fast_sin(x + (PI * 0.5f));
}

static void heart_point(float t, float u, float *x, float *y, float *z)
{
  float s = fast_sin(t);
  float c = fast_cos(t);
  float s2 = fast_sin(2.0f * t);
  float c2 = fast_cos(2.0f * t);
  float s3 = fast_sin(3.0f * t);
  float c3 = fast_cos(3.0f * t);
  float s4 = fast_sin(4.0f * t);
  float c4 = fast_cos(4.0f * t);

  float s3t = s * s * s;
  float r = 16.0f * s3t;
  float yv = 13.0f * c - 5.0f * c2 - 2.0f * c3 - c4;

  float su = fast_sin(u);
  float cu = fast_cos(u);
  *x = r * su;
  *y = yv;
  *z = r * cu;
}

static void cross3(float ax, float ay, float az,
                   float bx, float by, float bz,
                   float *ox, float *oy, float *oz)
{
  *ox = ay * bz - az * by;
  *oy = az * bx - ax * bz;
  *oz = ax * by - ay * bx;
}

static void project_point(float x, float y, float z,
                          int *sx, int *sy, float *depth)
{
  const int W = 320;
  const int H = 200;

  float scale = 0.045f;
  float dist = 5.5f;

  float px = x * scale;
  float py = y * scale;
  float pz = z * scale + dist;

  if (pz <= 0.1f)
  {
    *sx = -1;
    *sy = -1;
    return;
  }

  *sx = (int)(W * 0.5f + (px / pz) * (float)W * 0.8f);
  *sy = (int)(H * 0.5f - (py / pz) * (float)W * 0.8f);
  *depth = 1.0f / pz;
}

static void render_heart_3d(void)
{
  FrameBuffer *fb = &g_boot_info.fb;
  if (!fb || !fb->base)
  {
    console_write_line("no framebuffer");
    return;
  }

  const int W = 320;
  const int H = 200;
  static float zbuf[W * H];
  static uint32_t cbuf[W * H];

  uint32_t bg = make_pixel(0x0D, 0x0A, 0x12, fb->pixel_format);
  for (int i = 0; i < W * H; ++i)
  {
    zbuf[i] = -1e9f;
    cbuf[i] = bg;
  }

  // Lighting
  float lx = -0.3f, ly = 0.5f, lz = 0.8f;
  float l_len2 = lx * lx + ly * ly + lz * lz;
  float l_inv = fast_inv_sqrt(l_len2);
  lx *= l_inv;
  ly *= l_inv;
  lz *= l_inv;

  // Rotation (front-facing for a clearer heart silhouette)
  float ay = 0.25f;
  float ax = -0.15f;
  float cy = fast_cos(ay), sy = fast_sin(ay);
  float cx = fast_cos(ax), sx = fast_sin(ax);

  const float PI = 3.14159265f;
  float t_step = 0.05f;
  float u_step = 0.12f;
  float dt = 0.01f;
  float du = 0.01f;

  for (float t = 0.0f; t < PI * 2.0f; t += t_step)
  {
    for (float u = 0.0f; u < PI; u += u_step)
    {
      float x, y, z;
      heart_point(t, u, &x, &y, &z);

      float x1, y1, z1;
      float x2, y2, z2;
      heart_point(t + dt, u, &x1, &y1, &z1);
      heart_point(t, u + du, &x2, &y2, &z2);

      float v1x = x1 - x, v1y = y1 - y, v1z = z1 - z;
      float v2x = x2 - x, v2y = y2 - y, v2z = z2 - z;
      float nx, ny, nz;
      cross3(v1x, v1y, v1z, v2x, v2y, v2z, &nx, &ny, &nz);
      float n_len2 = nx * nx + ny * ny + nz * nz;
      if (n_len2 > 0.0001f)
      {
        float n_inv = fast_inv_sqrt(n_len2);
        nx *= n_inv;
        ny *= n_inv;
        nz *= n_inv;
      }

      // Rotate point and normal
      float xr = x * cy + z * sy;
      float zr = -x * sy + z * cy;
      float yr = y;

      float y2r = yr * cx - zr * sx;
      float z2r = yr * sx + zr * cx;
      float x2r = xr;

      float nxr = nx * cy + nz * sy;
      float nzr = -nx * sy + nz * cy;
      float nyr = ny;
      float ny2r = nyr * cx - nzr * sx;
      float nz2r = nyr * sx + nzr * cx;
      float nx2r = nxr;

      // Perspective projection
      float scale = 0.045f;
      float dist = 5.5f;
      float px = x2r * scale;
      float py = y2r * scale;
      float pz = z2r * scale + dist;
      if (pz <= 0.1f)
      {
        continue;
      }

      int sxp = (int)(W * 0.5f + (px / pz) * (float)W * 0.8f);
      int syp = (int)(H * 0.5f - (py / pz) * (float)W * 0.8f);
      if (sxp < 0 || sxp >= W || syp < 0 || syp >= H)
      {
        continue;
      }

      float depth = 1.0f / pz;
      int idx = syp * W + sxp;
      if (depth <= zbuf[idx])
      {
        continue;
      }
      zbuf[idx] = depth;

      float diff = nx2r * lx + ny2r * ly + nz2r * lz;
      if (diff < 0.0f)
        diff = 0.0f;
      float shade = 0.25f + 0.75f * diff;

      uint8_t r = (uint8_t)(210.0f * shade + 25.0f);
      uint8_t g = (uint8_t)(20.0f * shade + 5.0f);
      uint8_t b = (uint8_t)(60.0f * shade + 10.0f);
      cbuf[idx] = make_pixel(r, g, b, fb->pixel_format);
    }
  }

  // Blit to framebuffer (nearest neighbor scaling)
  for (uint32_t y = 0; y < fb->height; ++y)
  {
    uint32_t sy = (uint32_t)((y * (uint32_t)H) / fb->height);
    uint32_t row = y * fb->pixels_per_scanline;
    for (uint32_t x = 0; x < fb->width; ++x)
    {
      uint32_t sx = (uint32_t)((x * (uint32_t)W) / fb->width);
      ((uint32_t *)fb->base)[row + x] = cbuf[sy * W + sx];
    }
  }

  const char *left = "I";
  const char *right = "YOU";
  int ty = (int)(fb->height * 0.55f);
  if (ty >= 0 && ty + 16 < (int)fb->height)
  {
    uint32_t red = make_pixel(0xE0, 0x10, 0x10, fb->pixel_format);
    int w = (int)fb->width;
    int gap = 32;
    int left_w = 8 * 1;
    int right_w = 8 * 3;
    int center = w / 2;
    int tx_left = center - gap - left_w;
    int tx_right = center + gap;
    draw_text_at(fb, tx_left, ty, left, red, bg);
    draw_text_at(fb, tx_right, ty, right, red, bg);
  }
}

static void draw_heart_scene(void)
{
  render_heart_3d();
}

static void exec_command(char *line)
{
  if (line[0] == 0)
  {
    return;
  }
  if (streq(line, "help"))
  {
    console_write_line("commands: help clear echo info reboot mount ls cat");
    return;
  }
  if (streq(line, "clear"))
  {
    console_clear();
    return;
  }
  if (streq(line, "info"))
  {
    print_info();
    xhci_print_info();
    ahci_print_info();
    nvme_print_info();
    const BlockDevice *dev = block_get();
    if (dev)
    {
      console_write("block: ");
      console_write_line(dev->name);
    }
    else
    {
      console_write_line("block: none");
    }
    fat32_print_info();
    return;
  }
  if (streq(line, "mount"))
  {
    if (fat32_mount())
    {
      console_write_line("FAT32 mounted");
    }
    else
    {
      console_write_line("FAT32 mount failed");
    }
    return;
  }
  if (streq(line, "ls"))
  {
    if (!fat32_mount())
    {
      console_write_line("FAT32 mount failed");
      return;
    }
    if (!fat32_list_root())
    {
      console_write_line("ls failed");
    }
    return;
  }
  if (line[0] == 'c' && line[1] == 'a' && line[2] == 't' &&
      (line[3] == ' ' || line[3] == 0))
  {
    if (line[3] == 0)
    {
      console_write_line("usage: cat NAME");
      return;
    }
    if (!fat32_mount())
    {
      console_write_line("FAT32 mount failed");
      return;
    }
    static uint8_t buf[4096];
    uint32_t out_size = 0;
    if (!fat32_read_file(&line[4], buf, sizeof(buf), &out_size))
    {
      console_write_line("cat failed");
      return;
    }
    for (uint32_t i = 0; i < out_size && i < sizeof(buf); ++i)
    {
      char c = (char)buf[i];
      if (c == 0)
        break;
      console_putc(c);
    }
    console_putc('\n');
    return;
  }
  if (streq(line, "reboot"))
  {
    reboot();
    return;
  }
  if (streq(line, "heart"))
  {
    draw_heart_scene();
    return;
  }
  if (line[0] == 'e' && line[1] == 'c' && line[2] == 'h' && line[3] == 'o' &&
      (line[4] == ' ' || line[4] == 0))
  {
    if (line[4] == ' ')
    {
      console_write_line(&line[5]);
    }
    else
    {
      console_write_line("");
    }
    return;
  }
  console_write("unknown command: ");
  console_write_line(line);
}

void shell_run(void)
{
  char line[128];
  for (;;)
  {
    console_write("> ");
    uint32_t idx = 0;
    for (;;)
    {
      char c = keyboard_getchar();
      if (c == '\n')
      {
        console_putc('\n');
        line[idx] = 0;
        break;
      }
      if (c == '\b')
      {
        if (idx > 0)
        {
          idx--;
          console_putc('\b');
          console_putc(' ');
          console_putc('\b');
        }
        continue;
      }
      if (idx + 1 < sizeof(line))
      {
        line[idx++] = c;
        console_putc(c);
      }
    }
    exec_command(line);
  }
}
