#include "course_sid.h"
#include "types.h"
#include "memlayout.h"
#include "riscv.h"
#include "defs.h"
#include "pagetable.h"

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
  kinit();
#ifdef LAB3_SELFTEST
  vmselftest();
#endif
  kvminit();
  kvminithart();
  plicinit();
  plicinithart();
  procinit();
  userinit();
  scheduler();
}
