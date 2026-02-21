#include "kernel.h"
#include "console.h"
#include "keyboard.h"
#include "shell.h"
#include "drivers/xhci.h"
#include "drivers/block.h"

BootInfo g_boot_info;

void kernel_main(struct BootInfo *info) {
  g_boot_info = *info;
  console_init(&g_boot_info.fb);
  keyboard_init();

  console_write_line("TestOS shell");
  console_write_line("type help for commands");
  xhci_init();
  block_init();
  shell_run();
}
