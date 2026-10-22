#include "types.h"
#include "riscv.h"
#include "memlayout.h"
#include "course_sid.h"
#include "defs.h"

#define MAXPAGES ((PHYSTOP - KERNBASE) / PGSIZE)
static uint64 freebits[MAXPAGES / 64];
static uint64 first_page;
static uint npages, available;
static int highest;
extern char kernel_end[];

void
kinit(void)
{
  first_page = PGROUNDUP((uint64)kernel_end);
  uint64 limit = PHYSTOP - (uint64)LAB3_MEMCAP_DELTA * PGSIZE;
  if (first_page >= limit)
    panic("no physical memory");
  npages = (limit - first_page) / PGSIZE;
  memset(freebits, 0, sizeof(freebits));
  for (uint i = 0; i < npages; i++)
    freebits[i / 64] |= 1UL << (i % 64);
  available = npages;
  highest = (npages - 1) / 64;
}

uint
kfreepages(void)
{
  return available;
}

uint
ktotalpages(void)
{
  return npages;
}

void *
kalloc(void)
{
  if (!available)
    return 0;
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
  available--;
  void *page = (void *)(first_page + (uint64)index * PGSIZE);
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
  if (freebits[index / 64] & bit)
    panic("kfree twice");
  memset(page, 1, PGSIZE);
  freebits[index / 64] |= bit;
  if ((int)(index / 64) > highest)
    highest = index / 64;
  available++;
}
