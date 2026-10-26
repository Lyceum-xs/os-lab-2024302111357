#include "types.h"
#include "course_sid.h"
#include "param.h"
#include "memlayout.h"
#include "proc.h"
#include "pagetable.h"
#include "defs.h"

#define KSTACK_SIZE (1024 * LAB1_STACK_KB)
#define STRIDE_SCALE (1000ULL << 32)
#if LAB4_POLICY_ID != 7
#error This scheduler implements course policy 7.
#endif
struct proc proc_table[NPROC];
struct cpu cpus[NCPU];
static uchar kstacks[NPROC][KSTACK_SIZE] __attribute__((aligned(16)));
static struct spinlock wait_lock, pid_lock;
static struct proc *initproc;
static int nextpid = 1;
/* Single-core scheduler clock; access only with interrupts disabled. */
static uint64 virtual_time;
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
  p->stride = p->pass = 0;
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
    p->stride = STRIDE_SCALE;
    p->pass = virtual_time;
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

static void
normalize_passes(void)
{
  if (virtual_time < (1ULL << 60))
    return;
  for (int i = 0; i < NPROC; i++) {
    struct proc *p = &proc_table[i];
    acquire(&p->lock);
    p->pass = p->pass > virtual_time ? p->pass - virtual_time : 0;
    release(&p->lock);
  }
  virtual_time = 0;
}

static struct proc *
chooseproc(struct cpu *c)
{
  struct proc *chosen = 0;
  uint64 best = ~0ULL;
  int next = 0;
  for (int offset = 0; offset < NPROC; offset++) {
    int i = (c->rr_idx + offset) % NPROC;
    struct proc *p = &proc_table[i];
    acquire(&p->lock);
    if (p->state == RUNNABLE && (!chosen || p->pass < best)) {
      chosen = p;
      best = p->pass;
      next = (i + 1) % NPROC;
    }
    release(&p->lock);
  }
  if (chosen)
    c->rr_idx = next;
  return chosen;
}

void
scheduler(void)
{
  struct cpu *c = mycpu();
  for (;;) {
    intr_on();
    intr_off();
    normalize_passes();
    struct proc *chosen = chooseproc(c);
    if (chosen) {
      acquire(&chosen->lock);
      if (chosen->state != RUNNABLE)
        panic("scheduler candidate");
      virtual_time = chosen->pass;
      chosen->pass += chosen->stride;
      c->proc = chosen;
      chosen->state = RUNNING;
      chosen->last_slice_ticks = 0;
      swtch(&c->context, &chosen->context);
      c->proc = 0;
      release(&chosen->lock);
    } else {
      asm volatile("wfi");
    }
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
    if (p->state == SLEEPING && p->chan == chan) {
      if (p->pass < virtual_time)
        p->pass = virtual_time;
      p->state = RUNNABLE;
    }
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
  child->stride = parent->stride;
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
  /* wait_lock prevents the parent from checking until ZOMBIE is visible. */
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
      printf("PROC pid=%d name=%s state=%s tickets=%d cpu=%d slice=%d pass=%x\n",
             q->pid, q->name, states[q->state], q->tickets, q->cpu_ticks,
             q->last_slice_ticks, q->pass);
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
      if (p->state == SLEEPING) {
        if (p->pass < virtual_time)
          p->pass = virtual_time;
        p->state = RUNNABLE;
      }
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
  p->stride = STRIDE_SCALE / p->tickets;
  p->pass = virtual_time;
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
  p->stride = STRIDE_SCALE / value;
  p->pass = virtual_time;
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

#ifdef LAB4_SELFTEST
static void
policy_check(int ok, const char *message)
{
  if (!ok)
    panic(message);
}

void
schedselftest(void)
{
  struct cpu *c = mycpu();
  struct proc *p[3];
  const char *names[] = {"hi", "spin", "sh"};
  int counts[3] = {0};
  uint free_before = kfreepages();
  for (int i = 0; i < 3; i++) {
    p[i] = allocproc();
    policy_check(p[i] != 0, "policy test alloc");
    memcpy(p[i]->name, names[i], strlen(names[i]) + 1);
    proc_exec_policy(p[i]);
    policy_check(p[i]->tickets == (1 << i), "policy default tickets");
    p[i]->state = RUNNABLE;
    release(&p[i]->lock);
  }
  /* Exercise the actual picker for all three named defaults. */
  for (int round = 0; round < 700; round++) {
    struct proc *chosen = chooseproc(c);
    policy_check(chosen != 0, "policy pick");
    acquire(&chosen->lock);
    virtual_time = chosen->pass;
    chosen->pass += chosen->stride;
    for (int i = 0; i < 3; i++)
      if (chosen == p[i])
        counts[i]++;
    release(&chosen->lock);
  }
  policy_check(counts[0] == 100 && counts[1] == 200 && counts[2] == 400,
               "policy named shares");
  printf("SCHEDTEST named dispatches=%d:%d:%d PASS\n", counts[0], counts[1], counts[2]);

  c->proc = p[0];
  policy_check(ksetickets(0x7fffffff) == 0 && p[0]->stride > 0,
               "policy large tickets");
  acquire(&p[0]->lock);
  memcpy(p[0]->name, "sh", 3);
  proc_exec_policy(p[0]);
  policy_check(p[0]->tickets == 0x7fffffff, "exec overrides explicit tickets");
  release(&p[0]->lock);
  int childpid = kfork();
  policy_check(childpid > 0, "policy fork");
  acquire(&wait_lock);
  for (int i = 0; i < NPROC; i++) {
    struct proc *child = &proc_table[i];
    acquire(&child->lock);
    if (child->pid == childpid) {
      policy_check(child->tickets_set && child->tickets == p[0]->tickets &&
                   child->cpu_ticks == 0, "fork tickets and counters");
      freeproc(child);
    }
    release(&child->lock);
  }
  release(&wait_lock);
  c->proc = 0;

  uint64 floor = (1ULL << 60) + 100;
  virtual_time = floor;
  acquire(&p[0]->lock);
  p[0]->state = SLEEPING;
  p[0]->chan = p;
  p[0]->pass = 0;
  release(&p[0]->lock);
  wakeup(p);
  policy_check(p[0]->pass == floor, "wakeup stale pass");
  struct proc *fresh = allocproc();
  policy_check(fresh && fresh->pass == floor, "new process stale pass");
  freeproc(fresh);
  release(&fresh->lock);
  for (int i = 0; i < 3; i++) {
    acquire(&p[i]->lock);
    p[i]->pass = floor + i * 10;
    release(&p[i]->lock);
  }
  normalize_passes();
  policy_check(virtual_time == 0 && p[0]->pass == 0 &&
               p[1]->pass == 10 && p[2]->pass == 20, "pass normalization");
  acquire(&wait_lock);
  for (int i = 0; i < 3; i++) {
    acquire(&p[i]->lock);
    freeproc(p[i]);
    release(&p[i]->lock);
  }
  release(&wait_lock);
  c->rr_idx = 0;
  nextpid = 1;
  policy_check(kfreepages() == free_before && c->noff == 0,
               "policy test resources");
  printf("SCHEDTEST overrides inheritance wakeup normalization PASS\n");
  printf("SCHEDTEST all PASS\n");
}
#endif
