#include "types.h"
#include "proc.h"
#include "defs.h"

void
initlock(struct spinlock *lk, const char *name)
{
  lk->locked = 0;
  lk->name = name;
  lk->cpu = 0;
}

int
holding(struct spinlock *lk)
{
  return lk->locked && lk->cpu == mycpu();
}

void
push_off(void)
{
  int enabled = intr_get();
  intr_off();
  struct cpu *c = mycpu();
  if (!c->noff)
    c->intena = enabled;
  c->noff++;
}

void
pop_off(void)
{
  struct cpu *c = mycpu();
  if (intr_get() || c->noff < 1)
    panic("pop_off");
  c->noff--;
  if (!c->noff && c->intena)
    intr_on();
}

void
acquire(struct spinlock *lk)
{
  push_off();
  if (holding(lk))
    panic("acquire recursive");
  while (__atomic_exchange_n(&lk->locked, 1, __ATOMIC_ACQUIRE))
    ;
  lk->cpu = mycpu();
}

void
release(struct spinlock *lk)
{
  if (!holding(lk))
    panic("release owner");
  lk->cpu = 0;
  __atomic_store_n(&lk->locked, 0, __ATOMIC_RELEASE);
  pop_off();
}
