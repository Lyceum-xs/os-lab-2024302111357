#include "course_sid.h"

void consputc(int c);

static void
banner(void)
{
  const char text[] = "oslab sid=2024302111357 mod97=0x32\n";
  const char *p;

  for (p = text; *p; p++) {
    consputc(*p);
    if (LAB1_BANNER_PROTOCOL == 1)
      consputc('.');
  }
}

void
main(void)
{
  banner();
  for (;;)
    ;
}
