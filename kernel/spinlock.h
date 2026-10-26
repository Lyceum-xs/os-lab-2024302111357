#ifndef SPINLOCK_H
#define SPINLOCK_H

struct cpu;
struct spinlock {
  unsigned int locked;
  const char *name;
  struct cpu *cpu;
};

void initlock(struct spinlock *, const char *);
void acquire(struct spinlock *);
void release(struct spinlock *);
int holding(struct spinlock *);
void push_off(void);
void pop_off(void);

#endif
