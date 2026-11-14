#include "types.h"
#include "riscv.h"
#include "memlayout.h"
#include "course_sid.h"
#include "defs.h"
#include "spinlock.h"

#define MAXPAGES ((PHYSTOP - KERNBASE) / PGSIZE)
static uint64 freebits[MAXPAGES / 64];
static uint16 refcnt[MAXPAGES];
static uint shared_pages;
static uint64 cow_copies;
static uint64 first_page;
static uint npages, available;
static int highest;
static struct spinlock memlock;
extern char kernel_end[];

void
kinit(void)
{
  initlock(&memlock, "physical pages");
  first_page = PGROUNDUP((uint64)kernel_end);
  uint64 limit = PHYSTOP - (uint64)LAB3_MEMCAP_DELTA * PGSIZE;
  if (first_page >= limit)
    panic("no physical memory");
  npages = (limit - first_page) / PGSIZE;
  memset(freebits, 0, sizeof(freebits));
  memset(refcnt, 0, sizeof(refcnt));
  for (uint i = 0; i < npages; i++)
    freebits[i / 64] |= 1UL << (i % 64);
  available = npages;
  highest = (npages - 1) / 64;
}

uint
kfreepages(void)
{
  acquire(&memlock);
  uint count = available;
  release(&memlock);
  return count;
}

uint
ktotalpages(void)
{
  return npages;
}

void *
kalloc(void)
{
  acquire(&memlock);
  if (!available) {
    release(&memlock);
    return 0;
  }
  uint index = 0;
#if LAB3_ALLOC_ORDER == 1
  while (highest >= 0 && !freebits[highest])
    highest--;
  int bit = 63;
  while (!(freebits[highest] & (1UL << bit)))
    bit--;
  index = (uint)highest * 64 + bit;
#elif LAB3_ALLOC_ORDER == 0
  while (!(freebits[index / 64] & (1UL << (index % 64))))
    index++;
#else
  /* Search outwards from the middle, lower address first on ties. */
  uint middle = (npages - 1) / 2;
  for (uint distance = 0; distance < npages; distance++) {
    if (distance <= middle) {
      index = middle - distance;
      if (freebits[index / 64] & (1UL << (index % 64)))
        break;
    }
    index = middle + distance;
    if (index < npages && (freebits[index / 64] & (1UL << (index % 64))))
      break;
  }
#endif
  freebits[index / 64] &= ~(1UL << (index % 64));
  uint64 pa = first_page + (uint64)index * PGSIZE;
  refcnt[(pa - KERNBASE) / PGSIZE] = 1;
  available--;
  release(&memlock);
  void *page = (void *)pa;
  memset(page, 0, PGSIZE);
  return page;
}

void
kfree(void *page)
{
  uint64 pa = (uint64)page;
  if (pa % PGSIZE || pa < first_page ||
      pa >= first_page + (uint64)npages * PGSIZE)
    panic("kfree range");
  uint index = (pa - first_page) / PGSIZE;
  uint64 bit = 1UL << (index % 64);
  acquire(&memlock);
  uint16 *refs = &refcnt[(pa - KERNBASE) / PGSIZE];
  if (!*refs)
    panic("kfree: refcnt underflow");
  if (*refs == 2)
    shared_pages--;
  if (--*refs) {
    release(&memlock);
    return;
  }
  memset(page, 0x5a, PGSIZE);
  freebits[index / 64] |= bit;
  if ((int)(index / 64) > highest)
    highest = index / 64;
  available++;
  release(&memlock);
}

static uint
refindex(uint64 pa)
{
  if (pa % PGSIZE || pa < first_page ||
      pa >= first_page + (uint64)npages * PGSIZE)
    panic("kref range");
  return (pa - KERNBASE) / PGSIZE;
}

void
kref_inc(uint64 pa)
{
  uint index = refindex(pa);
  acquire(&memlock);
  if (!refcnt[index] || refcnt[index] == 65535)
    panic("kref overflow or free page");
  if (refcnt[index]++ == 1)
    shared_pages++;
  release(&memlock);
}

uint
kref_get(uint64 pa)
{
  uint index = refindex(pa);
  acquire(&memlock);
  uint count = refcnt[index];
  release(&memlock);
  return count;
}

uint64
kshared_count(void)
{
  acquire(&memlock);
  uint64 count = shared_pages;
  release(&memlock);
  return count;
}

uint64
kcow_copy_count(void)
{
  acquire(&memlock);
  uint64 count = cow_copies;
  release(&memlock);
  return count;
}

void
kcow_record(void)
{
  acquire(&memlock);
  cow_copies++;
  release(&memlock);
}
