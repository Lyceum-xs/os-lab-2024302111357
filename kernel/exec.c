#include "types.h"
#include "memlayout.h"
#include "proc.h"
#include "defs.h"

extern char _uprog_table[];

struct uprog_entry {
  uint64 start;
  uint64 end;
  char name[1];
};

static uint64
align8(uint64 x)
{
  return (x + 7) & ~7ull;
}

int
kexec(const char *name)
{
  struct proc *p = myproc();
  uint64 cursor = (uint64)_uprog_table;
  uint64 start;
  uint64 end;
  uint size;
  char *entry_name;

  if (p == 0)
    return -1;
  for (;;) {
    start = *(uint64 *)cursor;
    end = *(uint64 *)(cursor + sizeof(uint64));
    cursor += 2 * sizeof(uint64);
    if (start == 0 && end == 0)
      return -1;
    entry_name = (char *)cursor;
    if (strcmp(entry_name, name) == 0)
      break;
    cursor = align8(cursor + strlen(entry_name) + 1);
    cursor = align8(cursor);
    cursor = align8(cursor + (end - start));
  }

  if (end < start || end - start > USER_IMAGE_MAX)
    return -1;
  size = (uint)(end - start);
  memset(p->image, 0, USER_IMAGE_MAX);
  memcpy(p->image, (void *)start, size);
  p->image_size = USER_IMAGE_MAX;
  p->tf.epc = USER_BASE;
  p->tf.sp = USER_STACK_TOP;
  p->tf.ra = 0;
  p->tf.gp = 0;
  p->tf.tp = 0;
  p->tf.a0 = 0;
  p->tf.a1 = 0;
  p->tf.a2 = 0;
  memcpy((void *)USER_BASE, p->image, USER_IMAGE_MAX);
  return 0;
}
