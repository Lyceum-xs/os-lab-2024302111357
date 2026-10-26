#include "types.h"
#include "course_sid.h"
#include "param.h"
#include "memlayout.h"
#include "proc.h"
#include "pagetable.h"
#include "defs.h"

#define KSTACK_SIZE (1024 * LAB1_STACK_KB)
struct proc proc_table[NPROC];
struct cpu cpus[NCPU];
static uchar kstacks[NPROC][KSTACK_SIZE] __attribute__((aligned(16)));
static struct spinlock wait_lock, pid_lock;
static struct proc *initproc;
static int nextpid = 1;
extern char trampoline[], userret[];

typedef char check_tf_a0[__builtin_offsetof(struct trapframe, a0) == 112 ? 1 : -1];
typedef char check_tf_t6[__builtin_offsetof(struct trapframe, t6) == 280 ? 1 : -1];
typedef char check_tf_size[sizeof(struct trapframe) == 288 ? 1 : -1];
typedef char check_context_size[sizeof(struct context) == 112 ? 1 : -1];
typedef char check_context_s11[__builtin_offsetof(struct context, s11) == 104 ? 1 : -1];

static void
forkret(void)
{
  release(&myproc()->lock);
  uint64 satp = prepare_return(myproc());
  uint64 entry = TRAMPOLINE + (uint64)(userret - trampoline);
  ((void (*)(uint64))entry)(satp);
  panic("userret returned");
}

static void
freeproc(struct proc *p)
{
  if (!holding(&p->lock))
    panic("freeproc lock");
  uvmfree(p->pagetable);
  if (p->tf)
    kfree(p->tf);
  p->pagetable = 0;
  p->tf = 0;
  p->parent = 0;
  p->pid = p->exit_status = p->killed = 0;
  p->cpu_ticks = p->last_slice_ticks = 0;
  p->tickets = 1;
  p->tickets_set = 0;
  p->sz = p->heap_base = 0;
  p->chan = 0;
  memset(&p->context, 0, sizeof(p->context));
  memset(p->name, 0, sizeof(p->name));
  p->state = UNUSED;
}

void
procinit(void)
{
  initlock(&wait_lock, "wait");
  initlock(&pid_lock, "pid");
  for (int i = 0; i < NPROC; i++) {
    memset(&proc_table[i], 0, sizeof(proc_table[i]));
    initlock(&proc_table[i].lock, "proc");
    proc_table[i].kstack_top = (uint64)&kstacks[i][KSTACK_SIZE];
  }
}

int
cpuid(void)
{
  return r_tp();
}

struct cpu *
mycpu(void)
{
  if (intr_get() || cpuid() < 0 || cpuid() >= NCPU)
    panic("mycpu context");
  return &cpus[cpuid()];
}

struct proc *
myproc(void)
{
  push_off();
  struct proc *p = mycpu()->proc;
  pop_off();
  return p;
}

static struct proc *
allocproc(void)
{
  for (int i = 0; i < NPROC; i++) {
    struct proc *p = &proc_table[i];
    acquire(&p->lock);
    if (p->state != UNUSED) {
      release(&p->lock);
      continue;
    }
    p->state = USED;
    acquire(&pid_lock);
    p->pid = nextpid++;
    release(&pid_lock);
    p->tickets = 1;
    p->tf = kalloc();
    if (!p->tf || !(p->pagetable = uvmcreate((uint64)p->tf))) {
      freeproc(p);
      release(&p->lock);
      return 0;
    }
    p->context.sp = p->kstack_top;
    p->context.ra = (uint64)forkret;
    return p;
  }
  return 0;
}

void
userinit(void)
{
  struct proc *p = allocproc();
  if (!p)
    panic("userinit memory");
  initproc = p;
  mycpu()->proc = p;
  release(&p->lock);
  if (kexec("sh") < 0)
    panic("userinit exec");
  dump_pagetable(p->pagetable);
  acquire(&p->lock);
  p->state = RUNNABLE;
  mycpu()->proc = 0;
  release(&p->lock);
}

void
scheduler(void)
{
  struct cpu *c = mycpu();
  for (;;) {
    intr_on();
    intr_off();
    int ran = 0;
    for (int offset = 0; offset < NPROC; offset++) {
      int i = (c->rr_idx + offset) % NPROC;
      struct proc *p = &proc_table[i];
      acquire(&p->lock);
      if (p->state != RUNNABLE) {
        release(&p->lock);
        continue;
      }
      c->rr_idx = (i + 1) % NPROC;
      c->proc = p;
      p->state = RUNNING;
      p->last_slice_ticks = 0;
      swtch(&c->context, &p->context);
      c->proc = 0;
      release(&p->lock);
      ran = 1;
      break;
    }
    if (!ran)
      asm volatile("wfi");
  }
}

static void
sched(void)
{
  struct proc *p = myproc();
  if (!p || !holding(&p->lock))
    panic("sched p->lock");
  if (mycpu()->noff != 1)
    panic("sched locks");
  if (p->state == RUNNING)
    panic("sched state");
  if (intr_get())
    panic("sched interruptible");
  int intena = mycpu()->intena;
  swtch(&p->context, &mycpu()->context);
  mycpu()->intena = intena;
}

void
yield(void)
{
  struct proc *p = myproc();
  acquire(&p->lock);
  p->state = RUNNABLE;
  sched();
  release(&p->lock);
}

void
sleep(void *chan, struct spinlock *lk)
{
  struct proc *p = myproc();
  if (!p || lk == &p->lock || !holding(lk))
    panic("sleep lock");
  acquire(&p->lock);
  release(lk);
  p->chan = chan;
  p->state = SLEEPING;
  sched();
  p->chan = 0;
  release(&p->lock);
  acquire(lk);
}

void
wakeup(void *chan)
{
  struct proc *self = myproc();
  for (int i = 0; i < NPROC; i++) {
    struct proc *p = &proc_table[i];
    if (p == self)
      continue;
    acquire(&p->lock);
    if (p->state == SLEEPING && p->chan == chan)
      p->state = RUNNABLE;
    release(&p->lock);
  }
}

int
kfork(void)
{
  struct proc *parent = myproc();
  struct proc *child = allocproc();
  if (!child)
    return -1;
  if (uvmcopy(parent->pagetable, child->pagetable, parent->sz) < 0) {
    freeproc(child);
    release(&child->lock);
    return -1;
  }
  *child->tf = *parent->tf;
  child->tf->a0 = 0;
  child->sz = parent->sz;
  child->heap_base = parent->heap_base;
  memcpy(child->name, parent->name, sizeof(child->name));
  child->tickets = parent->tickets;
  child->tickets_set = parent->tickets_set;
  int pid = child->pid;
  release(&child->lock);
  acquire(&wait_lock);
  child->parent = parent;
  release(&wait_lock);
  acquire(&child->lock);
  child->state = RUNNABLE;
  release(&child->lock);
  return pid;
}

int
kwait(uint64 addr)
{
  struct proc *parent = myproc();
  if (addr && usercheck(parent->pagetable, addr, sizeof(int), PTE_W) < 0)
    return -1;
  acquire(&wait_lock);
  for (;;) {
    int children = 0;
    for (int i = 0; i < NPROC; i++) {
      struct proc *p = &proc_table[i];
      if (p->parent != parent)
        continue;
      acquire(&p->lock);
      children = 1;
      if (p->state == ZOMBIE) {
        if (addr && copyout(parent->pagetable, addr, &p->exit_status, sizeof(int)) < 0) {
          release(&p->lock);
          release(&wait_lock);
          return -1;
        }
        int pid = p->pid;
        freeproc(p);
        release(&p->lock);
        release(&wait_lock);
        return pid;
      }
      release(&p->lock);
    }
    if (!children || killed(parent)) {
      release(&wait_lock);
      return -1;
    }
    sleep(parent, &wait_lock);
  }
}

void
kexit(int status)
{
  struct proc *p = myproc();
  if (p == initproc)
    panic("initial shell exited");
  acquire(&wait_lock);
  for (int i = 0; i < NPROC; i++) {
    if (proc_table[i].parent == p) {
      proc_table[i].parent = initproc;
      wakeup(initproc);
    }
  }
  wakeup(p->parent);
  acquire(&p->lock);
  p->exit_status = status;
  p->state = ZOMBIE;
  release(&wait_lock);
  sched();
  panic("zombie returned");
}

uint64
growproc(int n)
{
  struct proc *p = myproc();
  uint64 oldsz = p->sz, newsz;
  if (n >= 0) {
    if ((uint64)n >= TRAPFRAME - oldsz)
      return (uint64)-1;
    newsz = oldsz + (uint64)n;
    if (uvmgrow(p->pagetable, oldsz, newsz) < 0)
      return (uint64)-1;
  } else {
    uint64 amount = -(long)n;
    if (amount > oldsz - p->heap_base)
      return (uint64)-1;
    newsz = oldsz - amount;
    uvmunmap(p->pagetable, PGROUNDUP(newsz),
             (PGROUNDUP(oldsz) - PGROUNDUP(newsz)) / PGSIZE, 1);
  }
  p->sz = newsz;
  return oldsz;
}

void
procdump(void)
{
  struct proc *p = myproc();
  if (!p)
    p = initproc;
  if (p && p->pagetable) {
    acquire(&p->lock);
    printf("pid=%d name=%s sz=%x free=%d\n", p->pid, p->name, p->sz, kfreepages());
    dump_pagetable(p->pagetable);
    release(&p->lock);
  }
  static const char *states[] = {"UNUSED", "USED", "SLEEPING", "RUNNABLE", "RUNNING", "ZOMBIE"};
  for (int i = 0; i < NPROC; i++) {
    struct proc *q = &proc_table[i];
    acquire(&q->lock);
    if (q->state != UNUSED)
      printf("PROC pid=%d name=%s state=%s tickets=%d cpu=%d slice=%d\n",
             q->pid, q->name, states[q->state], q->tickets, q->cpu_ticks, q->last_slice_ticks);
    release(&q->lock);
  }
}

int
killed(struct proc *p)
{
  acquire(&p->lock);
  int value = p->killed;
  release(&p->lock);
  return value;
}

int
kkill(int pid)
{
  for (int i = 0; i < NPROC; i++) {
    struct proc *p = &proc_table[i];
    acquire(&p->lock);
    if (p->pid == pid && p != initproc && p->state != UNUSED && p->state != ZOMBIE) {
      p->killed = 1;
      if (p->state == SLEEPING)
        p->state = RUNNABLE;
      release(&p->lock);
      return 0;
    }
    release(&p->lock);
  }
  return -1;
}

int
proc_tick(void)
{
  struct proc *p = myproc();
  acquire(&p->lock);
  if (p->cpu_ticks < 0x7fffffff)
    p->cpu_ticks++;
  p->last_slice_ticks++;
  int expire = p->last_slice_ticks >= LAB2_TICK;
  release(&p->lock);
  return expire;
}

void
proc_exec_policy(struct proc *p)
{
  if (!holding(&p->lock))
    panic("exec policy lock");
  if (!p->tickets_set)
    p->tickets = !strcmp(p->name, "sh") ? 4 : !strcmp(p->name, "spin") ? 2 : 1;
}

int
ksetickets(int value)
{
  if (value < 1)
    return -1;
  struct proc *p = myproc();
  acquire(&p->lock);
  p->tickets = value;
  p->tickets_set = 1;
  release(&p->lock);
  return 0;
}

int
kschedstat(int pid, uint64 cpuaddr, uint64 sliceaddr)
{
  struct proc *self = myproc();
  if (usercheck(self->pagetable, cpuaddr, sizeof(int), PTE_W) < 0 ||
      usercheck(self->pagetable, sliceaddr, sizeof(int), PTE_W) < 0)
    return -1;
  if (pid == -1)
    pid = self->pid;
  for (int i = 0; i < NPROC; i++) {
    struct proc *p = &proc_table[i];
    acquire(&p->lock);
    if (p->pid == pid && p->state != UNUSED && p->state != USED && p->state != ZOMBIE) {
      int cpu = p->cpu_ticks, slice = p->last_slice_ticks;
      release(&p->lock);
      if (copyout(self->pagetable, cpuaddr, &cpu, sizeof(cpu)) < 0 ||
          copyout(self->pagetable, sliceaddr, &slice, sizeof(slice)) < 0)
        return -1;
      return 0;
    }
    release(&p->lock);
  }
  return -1;
}
