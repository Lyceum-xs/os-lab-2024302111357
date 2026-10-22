#include "types.h"
#include "course_sid.h"
#include "proc.h"
#include "pagetable.h"
#include "defs.h"

static struct {
  char buf[LAB2_BUF_SIZE];
  uint r, w, e;
} cons;

void consoleinit(void) { uartinit(); }
void consputc(int c) { uartputc_sync(c); }

void
consoleintr(int c)
{
  if (c == 16) {
    procdump();
    return;
  }
  if (c == '\r')
    c = '\n';
  if (cons.e - cons.r == LAB2_BUF_SIZE)
    return;
  cons.buf[cons.e++ % LAB2_BUF_SIZE] = (char)c;
  consputc(c);
  if (LAB2_BUF_SEMANTICS == 1 || c == '\n' || cons.e - cons.r == LAB2_BUF_SIZE) {
    cons.w = cons.e;
    wakeup(&cons.r);
  }
}

int
consoleread(uint64 dst, int n)
{
  pagetable_t table = myproc()->pagetable;
  if (n < 0 || usercheck(table, dst, n, PTE_W) < 0)
    return -1;
  int count = 0;
  while (count < n) {
    while (cons.r == cons.w) {
      if (count)
        return count;
      sleep(&cons.r);
    }
    char c = cons.buf[cons.r % LAB2_BUF_SIZE];
    if (copyout(table, dst + count, &c, 1) < 0)
      return -1;
    cons.r++;
    count++;
    if (LAB2_BUF_SEMANTICS == 0 && c == '\n')
      break;
  }
  return count;
}

int
consolewrite(uint64 src, int n)
{
  pagetable_t table = myproc()->pagetable;
  if (n < 0 || usercheck(table, src, n, PTE_R) < 0)
    return -1;
  char buf[128];
  int count = 0;
  while (count < n) {
    int part = n - count;
    if (part > sizeof(buf))
      part = sizeof(buf);
    if (copyin(table, buf, src + count, part) < 0)
      return -1;
    for (int i = 0; i < part; i++)
      consputc(buf[i]);
    count += part;
  }
  return count;
}
