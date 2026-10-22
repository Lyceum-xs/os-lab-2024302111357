#include "types.h"
#include "course_sid.h"
#include "param.h"
#include "memlayout.h"
#include "proc.h"
#include "pagetable.h"
#include "defs.h"

struct uprog {
  uint64 start, end, entry, writable, mem_end;
  char name[32];
};
extern const struct uprog _uprog_table[];

int
kexec(const char *name)
{
  struct proc *p = myproc();
  const struct uprog *image;
  for (image = _uprog_table; image->start; image++)
    if (!strcmp(image->name, name))
      break;
  if (!image->start || image->end < image->start ||
      image->end - image->start > image->mem_end ||
      image->writable % PGSIZE || image->entry >= image->writable ||
      image->mem_end > PHYSTOP - KERNBASE)
    return -1;

  pagetable_t table = uvmcreate((uint64)p->tf);
  if (!table)
    return -1;
  uint64 bytes = image->end - image->start;
  uint64 image_end = PGROUNDUP(image->mem_end);
  for (uint64 a = 0; a < image_end; a += PGSIZE) {
    void *page = kalloc();
    if (!page)
      goto fail;
    if (a < bytes) {
      uint64 n = bytes - a;
      if (n > PGSIZE)
        n = PGSIZE;
      memcpy(page, (void *)(image->start + a), n);
    }
    int perm = PTE_R | PTE_U | (a < image->writable ? PTE_X : PTE_W);
    if (mappages(table, a, PGSIZE, (uint64)page, perm) < 0) {
      kfree(page);
      goto fail;
    }
  }
  uint64 stack_base = image_end + LAB3_GUARD_PAGES * PGSIZE;
  uint64 stack_top = stack_base + USERSTACK * PGSIZE;
  if (uvmgrow(table, stack_base, stack_top) < 0)
    goto fail;

  pagetable_t old = p->pagetable;
  p->pagetable = table;
  p->sz = p->heap_base = stack_top;
  memset(p->tf, 0, sizeof(*p->tf));
  p->tf->epc = image->entry;
  p->tf->sp = stack_top;
  memset(p->name, 0, sizeof(p->name));
  uint n = strlen(name);
  if (n >= sizeof(p->name))
    n = sizeof(p->name) - 1;
  memcpy(p->name, name, n);
  uvmfree(old);
  return 0;
fail:
  uvmfree(table);
  return -1;
}
