#include "course_sid.h"
#include "types.h"
#include "memlayout.h"
#include "riscv.h"
#include "defs.h"

static void
banner(void)
{
  const char text[] = "oslab sid=2024302111357 mod97=0x32\n";
  const char *p;
  for (p = text; *p; p++) {
    uartputc_sync(*p);
    if (LAB1_BANNER_PROTOCOL == 1)
      uartputc_sync('.');
  }
}

void
main(void)
{
  banner();
  consoleinit();
  trapinit();
  plicinit();
  plicinithart();
  *(volatile uint64 *)CLINT_MTIMECMP(0) =
      *(volatile uint64 *)CLINT_MTIME + 1000000;
  procinit();
  userinit();
  intr_on();
  scheduler();
}
