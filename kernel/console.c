#include "types.h"
#include "course_sid.h"
#include "memlayout.h"
#include "defs.h"

struct {
  char buf[LAB2_BUF_SIZE];
  uint r;
  uint w;
} cons;

void
consoleinit(void)
{
  memset(&cons, 0, sizeof(cons));
  uartinit();
}

void
consputc(int c)
{
  uartputc_sync(c);
}

void
consoleintr(int c)
{
  if (c == '\r')
    c = '\n';
  if (cons.w - cons.r >= LAB2_BUF_SIZE)
    return;
  cons.buf[cons.w++ % LAB2_BUF_SIZE] = (char)c;
  consputc(c);
}

int
consoleread(uint64 dst, int n)
{
  int count = 0;
  int c;

  if (dst < USER_BASE || dst + (uint64)n > USER_BASE + USER_IMAGE_MAX)
    return -1;
  if (n == 0)
    return 0;
  while (count < n) {
    while (cons.r == cons.w && count == 0)
      uartpoll();
    if (cons.r == cons.w)
      break;
    *(char *)(dst + count) = cons.buf[cons.r++ % LAB2_BUF_SIZE];
    count++;
    c = *(char *)(dst + count - 1);
    if (LAB2_BUF_SEMANTICS == 0 && c == '\n')
      break;
    if (cons.r == cons.w)
      uartpoll();
  }
  return count;
}

int
consolewrite(uint64 src, int n)
{
  int i;
  if (src < USER_BASE || src + (uint64)n > USER_BASE + USER_IMAGE_MAX)
    return -1;
  for (i = 0; i < n; i++)
    consputc(*(char *)(src + i));
  return n;
}
