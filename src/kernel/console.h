#ifndef CONSOLE_H
#define CONSOLE_H

#include <stdint.h>
#include "kernel.h"

void console_init(FrameBuffer *fb);
void console_clear(void);
void console_putc(char c);
void console_write(const char *s);
void console_write_line(const char *s);
void console_set_header(const char *text);

#endif
