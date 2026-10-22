#ifdef LAB3_SELFTEST
#include "types.h"
#include "riscv.h"
#include "memlayout.h"
#include "course_sid.h"
#include "pagetable.h"
#include "defs.h"

static void *held[(PHYSTOP - KERNBASE) / PGSIZE];

static void
check(int condition, const char *message)
{
  if (!condition)
    panic(message);
}

static uint
reserve_until(uint remaining)
{
  uint n = 0;
  while (kfreepages() > remaining) {
    held[n] = kalloc();
    check(held[n] != 0, "test reserve");
    n++;
  }
  return n;
}

static void
release(uint n)
{
  while (n)
    kfree(held[--n]);
}

void
vmselftest(void)
{
  uint original = kfreepages();
  extern char kernel_end[];
  uint expected = (PHYSTOP - PGROUNDUP((uint64)kernel_end)) / PGSIZE - LAB3_MEMCAP_DELTA;
  check(original == expected && original == ktotalpages(), "allocator quota");
  void *a = kalloc(), *b = kalloc();
  check(a && b && (uint64)a % PGSIZE == 0, "allocator alignment");
#if LAB3_ALLOC_ORDER == 1
  check(a > b, "allocator descending");
#elif LAB3_ALLOC_ORDER == 0
  check(a < b, "allocator ascending");
#endif
  memset(a, 0x5a, PGSIZE);
  kfree(a);
  void *again = kalloc();
  check(again == a, "allocator reuse order");
  for (uint i = 0; i < PGSIZE; i++)
    check(((uchar *)again)[i] == 0, "allocator zero");
  kfree(again);
  kfree(b);
  check(kfreepages() == original, "allocator balance");
  printf("KTEST allocator PASS pages=%d delta=%d order=%d\n", original,
         LAB3_MEMCAP_DELTA, LAB3_ALLOC_ORDER);

  pagetable_t root = kalloc();
  void *sentinel = kalloc();
  check(mappages(root, 0, PGSIZE, (uint64)sentinel, PTE_R | PTE_W | PTE_U) == 0,
        "test sentinel");
  pte_t saved = *walk(root, 0, 0);
  uint before = kfreepages();
  check(mappages(root, 0, PGSIZE, (uint64)sentinel, PTE_R | PTE_U) < 0 &&
        *walk(root, 0, 0) == saved && kfreepages() == before, "duplicate mapping");
  check(walk(root, MAXVA, 1) == 0, "noncanonical walk");

  /* Preserve a pre-existing empty subtree while rolling back new subtrees. */
  check(walk(root, 2UL << 30, 1) != 0, "empty subtree");
  uint n = reserve_until(1);
  check(walk(root, 3UL << 30, 1) == 0 && kfreepages() == 1, "walk rollback");
  check(mappages(root, (1UL << 30) - PGSIZE, 2 * PGSIZE,
                 (uint64)sentinel, PTE_R | PTE_U) < 0, "map must OOM");
  check(kfreepages() == 1 && *walk(root, 0, 0) == saved, "map rollback balance");
  check(walk(root, 2UL << 30, 0) != 0, "old empty subtree preserved");
  check(!walk(root, (1UL << 30) - PGSIZE, 0), "new subtree reclaimed");
  release(n);
  printf("KTEST mapping-rollback PASS\n");

  n = reserve_until(3);
  check(uvmgrow(root, (1UL << 21) - PGSIZE, (1UL << 21) + 4 * PGSIZE) < 0,
        "growth must OOM");
  check(kfreepages() == 3 && *walk(root, 0, 0) == saved, "growth rollback balance");
  release(n);

  check(mappages(root, PGSIZE, PGSIZE, (uint64)sentinel, PTE_R | PTE_U) == 0,
        "readonly alias");
  char input[] = "safe";
  check(copyout(root, PGSIZE - 2, input, 4) < 0, "copyout write protection");
  check(((char *)sentinel)[PGSIZE - 2] == 0, "copyout atomic validation");
  check(copyin(root, input, MAXVA - 1, 2) < 0, "copyin overflow");
  uvmunmap(root, PGSIZE, 1, 0);
  uvmfree(root);
  check(kfreepages() == original, "vm final balance");
  printf("KTEST growth-and-copy PASS\n");

  n = reserve_until(0);
  check(kalloc() == 0, "allocator OOM");
  release(n);
  check(kfreepages() == original, "exhaustion balance");
  printf("KTEST all PASS\n");
}
#endif
