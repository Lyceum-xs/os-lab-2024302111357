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
kexec(const char *name, char *const argv[])
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

  uint64 sp = stack_top, pointers[MAXARG];
  int argc = 0;
  while (argv && argv[argc]) {
    if (argc >= MAXARG - 1)
      goto fail;
    uint64 len = strlen(argv[argc]) + 1;
    if (len > sp - stack_base)
      goto fail;
    sp = (sp - len) & ~15UL;
    if (sp < stack_base || copyout(table, sp, argv[argc], len) < 0)
      goto fail;
    pointers[argc++] = sp;
  }
  pointers[argc] = 0;
  uint64 pointer_bytes = (argc + 1) * sizeof(uint64);
  if (pointer_bytes > sp - stack_base)
    goto fail;
  sp = (sp - pointer_bytes) & ~15UL;
  if (sp < stack_base || copyout(table, sp, pointers, pointer_bytes) < 0)
    goto fail;

  acquire(&p->lock);
  pagetable_t old = p->pagetable;
  p->pagetable = table;
  p->sz = p->heap_base = stack_top;
  memset(p->tf, 0, sizeof(*p->tf));
  p->tf->epc = image->entry;
  p->tf->sp = sp;
  p->tf->a1 = sp;
  memset(p->name, 0, sizeof(p->name));
  uint n = strlen(name);
  if (n >= sizeof(p->name))
    n = sizeof(p->name) - 1;
  memcpy(p->name, name, n);
  proc_exec_policy(p);
  release(&p->lock);
  uvmfree(old);
#ifdef LAB5_SELFTEST
  if (!strcmp(name, "echo")) {
    printf("EXECSTACK argc=%d sp=%x argv0=%x argv1=%x sentinel=%x\n",
           argc, sp, pointers[0], argc > 1 ? pointers[1] : 0UL, pointers[argc]);
    for (uint64 addr = sp; addr < stack_top && addr < sp + 64; addr += 8) {
      uint64 value;
      if (copyin(table, &value, addr, sizeof(value)) < 0)
        panic("exec stack snapshot");
      printf("EXECWORD va=%x data=%x\n", addr, value);
    }
  }
#endif
  return argc;
fail:
  uvmfree(table);
  return -1;
}
