#ifdef LAB5_SELFTEST
#include "types.h"
#include "course_sid.h"
#include "memlayout.h"
#include "proc.h"
#include "vm.h"
#include "pagetable.h"
#include "defs.h"

static void *held[(PHYSTOP - KERNBASE) / PGSIZE];

static void check(int ok, const char *what)
{
  if (!ok)
    panic(what);
}

static uint reserve(uint left)
{
  uint n = 0;
  while (kfreepages() > left) {
    held[n] = kalloc();
    check(held[n++] != 0, "cow reserve");
  }
  return n;
}

static void restore(uint n)
{
  while (n)
    kfree(held[--n]);
}

void cowselftest(void)
{
  uint original = kfreepages();
  char *page = kalloc();
  page[0] = 42;
  check(kref_get((uint64)page) == 1, "new ref one");
  kref_inc((uint64)page);
  kref_inc((uint64)page);
  check(kref_get((uint64)page) == 3 && kshared_count() == 1, "three refs");
  kfree(page);
  kfree(page);
  check(page[0] == 42 && kshared_count() == 0 && kfreepages() == original - 1,
        "shared page not poisoned");
  kfree(page);
  check(kfreepages() == original, "ref balance");
  printf("COWKTEST allocation share release PASS\n");

  pagetable_t parent = kalloc(), child = kalloc();
  char *code = kalloc(), *data = kalloc(), *high = kalloc();
  data[0] = 12;
  high[0] = 34;
  check(mappages(parent, 0, PGSIZE, (uint64)code, PTE_R | PTE_X | PTE_U) == 0,
        "cow code map");
  check(mappages(parent, PGSIZE, PGSIZE, (uint64)data, PTE_R | PTE_W | PTE_U) == 0,
        "cow data map");
  check(mappages(parent, 1UL << 21, PGSIZE, (uint64)high, PTE_R | PTE_W | PTE_U) == 0,
        "cow high map");
  pte_t before = *walk(parent, PGSIZE, 0);
  uint n = reserve(2);
  check(uvmcopy(parent, child, (1UL << 21) + PGSIZE) < 0, "fork partial OOM");
  check(*walk(parent, PGSIZE, 0) == before, "fork failure parent unchanged");
  uvmfree(child);
  check(kfreepages() == 3 && kref_get((uint64)data) == 1 && kshared_count() == 0,
        "fork rollback references");
  restore(n);
  printf("COWKTEST partial-fork rollback PASS\n");

  child = kalloc();
  check(uvmcopy(parent, child, (1UL << 21) + PGSIZE) == 0, "cow fork");
  check(kshared_count() == 3 && kref_get((uint64)data) == 2, "cow shared count");
  check(!(*walk(parent, PGSIZE, 0) & PTE_W) && (*walk(parent, PGSIZE, 0) & PTE_COW),
        "cow parent flags");
  check(!(*walk(child, 0, 0) & PTE_COW) && cow_alloc(child, 0) < 0 &&
        cow_alloc(child, 2 * PGSIZE) < 0 && cow_alloc(child, MAXVA) < 0,
        "cow guard and readonly");
  uint64 copies = kcow_copy_count();
  check(copyout(child, 2 * PGSIZE - 1, "ab", 2) < 0 &&
        kcow_copy_count() == copies, "cow full-range validation");
  before = *walk(child, PGSIZE, 0);
  n = reserve(0);
  check(cow_alloc(child, PGSIZE) < 0 && *walk(child, PGSIZE, 0) == before &&
        kref_get((uint64)data) == 2 && kcow_copy_count() == copies,
        "cow allocation OOM atomicity");
  restore(n);
  check(copyout(child, PGSIZE, "Z", 1) == 0 && data[0] == 12 &&
        kref_get((uint64)data) == 1 && kcow_copy_count() == copies + 1,
        "cow copyout separation");
  n = reserve(0);
  check(cow_alloc(parent, PGSIZE) == 0 && kcow_copy_count() == copies + 1,
        "exclusive cow succeeds without free pages");
  restore(n);
  printf("COWKTEST copyout OOM and exclusive recovery PASS\n");

  struct proc target;
  memset(&target, 0, sizeof(target));
  initlock(&target.lock, "cow killed test");
  target.killed = 1;
  target.pagetable = child;
#if LAB5_KILL_SEMANTICS == 1
  check(cowfault(&target, 1UL << 21) == 0 && kcow_copy_count() == copies + 2,
        "killed process finishes current copy");
#else
  check(cowfault(&target, 1UL << 21) < 0 && kcow_copy_count() == copies + 1,
        "killed process skips copy");
#endif
  uvmfree(parent);
  uvmfree(child);
  check(kfreepages() == original && kshared_count() == 0, "cow final conservation");
  printf("COWKTEST kill-semantics=%d all PASS\n", LAB5_KILL_SEMANTICS);
}
#endif
