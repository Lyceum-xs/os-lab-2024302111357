#include "types.h"
#include "riscv.h"
#include "memlayout.h"
#include "pagetable.h"
#include "defs.h"

#define LEAF (PTE_R | PTE_W | PTE_X)
/* RSW bit belongs to this single-core, non-preemptible kernel transaction. */
#define PTE_NEW (1UL << 8)
pagetable_t kernel_pagetable;
extern char trampoline[], kernel_text_end[];

static pte_t *
walk_create(pagetable_t table, uint64 va, int alloc, int transaction)
{
  pte_t *created[2];
  int count = 0;
  if (va >= MAXVA)
    return 0;
  for (int level = 2; level > 0; level--) {
    pte_t *pte = &table[PX(level, va)];
    if (*pte & PTE_V) {
      if (*pte & LEAF)
        goto fail;
    } else {
      if (!alloc)
        goto fail;
      pagetable_t next = kalloc();
      if (!next)
        goto fail;
      *pte = PA2PTE(next) | PTE_V | (transaction ? PTE_NEW : 0);
      created[count++] = pte;
    }
    table = (pagetable_t)PTE2PA(*pte);
  }
  return &table[PX(0, va)];
fail:
  while (count) {
    pte_t *pte = created[--count];
    void *page = (void *)PTE2PA(*pte);
    *pte = 0;
    sfence_vma();
    kfree(page);
  }
  return 0;
}

pte_t *
walk(pagetable_t table, uint64 va, int alloc)
{
  return walk_create(table, va, alloc, 0);
}

/* Visit only the affected interval. Existing PTEs, even empty tables, survive. */
static void
finish_mapping(pagetable_t table, int level, uint64 base,
               uint64 lo, uint64 hi, int rollback)
{
  uint64 span = 1UL << PXSHIFT(level);
  uint first = (lo > base ? lo - base : 0) / span;
  uint last = (hi - 1 - base) / span;
  if (last > 511)
    last = 511;
  for (uint i = first; i <= last; i++) {
    pte_t *pte = &table[i];
    if (!(*pte & PTE_V))
      continue;
    uint64 pa = PTE2PA(*pte);
    if (level && !(*pte & LEAF))
      finish_mapping((pagetable_t)pa, level - 1, base + i * span,
                     lo, hi, rollback);
    if (*pte & PTE_NEW) {
      if (rollback) {
        int child_table = !(*pte & LEAF);
        *pte = 0;
        sfence_vma();
        if (child_table)
          kfree((void *)pa);
      } else {
        *pte &= ~PTE_NEW;
      }
    }
  }
}

int
mappages(pagetable_t table, uint64 va, uint64 size, uint64 pa, int perm)
{
  if (!size || va % PGSIZE || pa % PGSIZE || va >= MAXVA ||
      size > MAXVA - va || !(perm & LEAF) ||
      ((perm & PTE_W) && !(perm & PTE_R)))
    return -1;
  uint64 end = PGROUNDUP(va + size);
  uint64 a;
  for (a = va; a < end; a += PGSIZE, pa += PGSIZE) {
    pte_t *pte = walk_create(table, a, 1, 1);
    if (!pte || (*pte & PTE_V)) {
      finish_mapping(table, 2, 0, va, a + PGSIZE, 1);
      return -1;
    }
    *pte = PA2PTE(pa) | perm | PTE_V | PTE_NEW;
  }
  finish_mapping(table, 2, 0, va, end, 0);
  sfence_vma();
  return 0;
}

static int
empty_table(pagetable_t table)
{
  for (int i = 0; i < 512; i++)
    if (table[i] & PTE_V)
      return 0;
  return 1;
}

static void
prune(pagetable_t root, uint64 va)
{
  pte_t *p2 = &root[PX(2, va)];
  if (!(*p2 & PTE_V) || (*p2 & LEAF))
    return;
  pagetable_t l1 = (pagetable_t)PTE2PA(*p2);
  pte_t *p1 = &l1[PX(1, va)];
  if ((*p1 & PTE_V) && !(*p1 & LEAF)) {
    pagetable_t l0 = (pagetable_t)PTE2PA(*p1);
    if (empty_table(l0)) {
      *p1 = 0;
      sfence_vma();
      kfree(l0);
    }
  }
  if (empty_table(l1)) {
    *p2 = 0;
    sfence_vma();
    kfree(l1);
  }
}

void
uvmunmap(pagetable_t table, uint64 va, uint64 npages, int free_pages)
{
  if (va % PGSIZE || va >= MAXVA || npages > (MAXVA - va) / PGSIZE)
    panic("uvmunmap range");
  for (uint64 i = 0; i < npages; i++, va += PGSIZE) {
    pte_t *pte = walk(table, va, 0);
    if (pte && (*pte & PTE_V)) {
      if (!(*pte & LEAF))
        panic("uvmunmap nonleaf");
      uint64 pa = PTE2PA(*pte);
      *pte = 0;
      sfence_vma();
      if (free_pages)
        kfree((void *)pa);
    }
    prune(table, va);
  }
}

static void
free_tree(pagetable_t table)
{
  for (int i = 0; i < 512; i++) {
    pte_t entry = table[i];
    if (!(entry & PTE_V))
      continue;
    table[i] = 0;
    sfence_vma();
    if (!(entry & LEAF))
      free_tree((pagetable_t)PTE2PA(entry));
    else if (entry & PTE_U)
      kfree((void *)PTE2PA(entry));
  }
  kfree(table);
}

void
uvmfree(pagetable_t table)
{
  if (table)
    free_tree(table);
}

pagetable_t
uvmcreate(uint64 trapframe)
{
  pagetable_t table = kalloc();
  if (!table)
    return 0;
  if (mappages(table, TRAMPOLINE, PGSIZE, (uint64)trampoline, PTE_R | PTE_X) < 0 ||
      mappages(table, TRAPFRAME, PGSIZE, trapframe, PTE_R | PTE_W) < 0) {
    uvmfree(table);
    return 0;
  }
  return table;
}

void
kvminit(void)
{
  kernel_pagetable = kalloc();
  uint64 text_end = (uint64)kernel_text_end;
  if (!kernel_pagetable ||
      mappages(kernel_pagetable, KERNBASE, text_end - KERNBASE,
               KERNBASE, PTE_R | PTE_X) < 0 ||
      mappages(kernel_pagetable, text_end, PHYSTOP - text_end,
               text_end, PTE_R | PTE_W) < 0 ||
      mappages(kernel_pagetable, UART0, PGSIZE, UART0, PTE_R | PTE_W) < 0 ||
      mappages(kernel_pagetable, PLIC, 0x400000, PLIC, PTE_R | PTE_W) < 0 ||
      mappages(kernel_pagetable, TRAMPOLINE, PGSIZE, (uint64)trampoline,
               PTE_R | PTE_X) < 0)
    panic("kernel pagetable");
}

void
kvminithart(void)
{
  sfence_vma();
  w_satp(MAKE_SATP(kernel_pagetable));
  sfence_vma();
}

int
uvmgrow(pagetable_t table, uint64 oldsz, uint64 newsz)
{
  if (newsz < oldsz || newsz >= TRAPFRAME)
    return -1;
  uint64 start = PGROUNDUP(oldsz), a;
  for (a = start; a < newsz; a += PGSIZE) {
    void *page = kalloc();
    if (!page)
      goto fail;
    if (mappages(table, a, PGSIZE, (uint64)page, PTE_U | PTE_R | PTE_W) < 0) {
      kfree(page);
      goto fail;
    }
  }
  return 0;
fail:
  uvmunmap(table, start, (a - start) / PGSIZE, 1);
  return -1;
}

int
uvmcopy(pagetable_t old, pagetable_t new, uint64 sz)
{
  for (uint64 a = 0; a < sz; a += PGSIZE) {
    pte_t *pte = walk(old, a, 0);
    if (!pte || !(*pte & PTE_V))
      continue;
    if (!(*pte & PTE_U))
      return -1;
    void *page = kalloc();
    if (!page)
      return -1;
    memcpy(page, (void *)PTE2PA(*pte), PGSIZE);
    if (mappages(new, a, PGSIZE, (uint64)page, PTE_FLAGS(*pte)) < 0) {
      kfree(page);
      return -1;
    }
  }
  return 0;
}

uint64
userpa(pagetable_t table, uint64 va, int perm)
{
  pte_t *pte = walk(table, va, 0);
  if (!pte || ((*pte & (PTE_V | PTE_U | perm)) != (PTE_V | PTE_U | perm)) ||
      !(*pte & LEAF))
    return 0;
  return PTE2PA(*pte) + (va % PGSIZE);
}

int
usercheck(pagetable_t table, uint64 addr, uint64 n, int perm)
{
  if (addr >= MAXVA || n > MAXVA - addr)
    return -1;
  for (uint64 a = addr; n;) {
    if (!userpa(table, a, perm))
      return -1;
    uint64 part = PGSIZE - a % PGSIZE;
    if (part > n)
      part = n;
    a += part;
    n -= part;
  }
  return 0;
}

static int
copyuser(pagetable_t table, uint64 user, void *kernel, uint64 n, int out)
{
  int perm = out ? PTE_W : PTE_R;
  if (usercheck(table, user, n, perm) < 0)
    return -1;
  char *ptr = kernel;
  while (n) {
    uint64 pa = userpa(table, user, perm);
    uint64 part = PGSIZE - user % PGSIZE;
    if (part > n)
      part = n;
    if (out)
      memcpy((void *)pa, ptr, part);
    else
      memcpy(ptr, (void *)pa, part);
    n -= part;
    ptr += part;
    user += part;
  }
  return 0;
}

int
copyin(pagetable_t table, void *dst, uint64 src, uint64 n)
{
  return copyuser(table, src, dst, n, 0);
}

int
copyout(pagetable_t table, uint64 dst, const void *src, uint64 n)
{
  return copyuser(table, dst, (void *)src, n, 1);
}

int
copyinstr(pagetable_t table, char *dst, uint64 src, uint64 max)
{
  if (src >= MAXVA || !max)
    return -1;
  while (max && src < MAXVA) {
    uint64 pa = userpa(table, src, PTE_R);
    if (!pa)
      return -1;
    uint64 n = PGSIZE - src % PGSIZE;
    if (n > max)
      n = max;
    for (uint64 i = 0; i < n; i++) {
      char c = *(char *)(pa + i);
      *dst++ = c;
      if (!c)
        return 0;
    }
    src += n;
    max -= n;
  }
  return -1;
}

static void
dump_level(pagetable_t table, int level, uint64 base)
{
  for (int i = 0; i < 512; i++) {
    pte_t pte = table[i];
    if (!(pte & PTE_V))
      continue;
    uint64 va = base | ((uint64)i << PXSHIFT(level));
    uint64 pa = PTE2PA(pte);
    char perm[] = "----";
    if (pte & PTE_R) perm[0] = 'r';
    if (pte & PTE_W) perm[1] = 'w';
    if (pte & PTE_X) perm[2] = 'x';
    if (pte & PTE_U) perm[3] = 'u';
    if (pte & LEAF)
      printf("LEAF va=%x pa=%x perm=%s\n", va, pa, perm);
    else {
      printf("L%d va=%x pa=%x perm=----\n", level, va, pa);
      dump_level((pagetable_t)pa, level - 1, va);
    }
  }
}

void
dump_pagetable(pagetable_t table)
{
  printf("DUMP-PAGETABLE begin\n");
  dump_level(table, 2, 0);
  printf("DUMP-PAGETABLE end\n");
}
