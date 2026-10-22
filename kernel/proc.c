#include "types.h"
#include "course_sid.h"
#include "param.h"
#include "memlayout.h"
#include "proc.h"
#include "pagetable.h"
#include "defs.h"

#define KSTACK_SIZE (1024 * LAB1_STACK_KB)
struct proc proc_table[NPROC];
struct proc *current_proc;
static uchar kstacks[NPROC][KSTACK_SIZE] __attribute__((aligned(16)));
static struct context scheduler_context;
static struct proc *initproc;
static int nextpid = 1;
extern char trampoline[], userret[];

typedef char check_tf_a0[__builtin_offsetof(struct trapframe, a0) == 112 ? 1 : -1];
typedef char check_tf_t6[__builtin_offsetof(struct trapframe, t6) == 280 ? 1 : -1];
typedef char check_tf_size[sizeof(struct trapframe) == 288 ? 1 : -1];

static void
first_return(void)
{
  uint64 satp = prepare_return(myproc());
  uint64 entry = TRAMPOLINE + (uint64)(userret - trampoline);
  ((void (*)(uint64))entry)(satp);
  panic("userret returned");
}

static void
freeproc(struct proc *p)
{
  uvmfree(p->pagetable);
  if (p->tf)
    kfree(p->tf);
  uint64 stack = p->kstack_top;
  memset(p, 0, sizeof(*p));
  p->kstack_top = stack;
  p->state = UNUSED;
}

void
procinit(void)
{
  for (int i = 0; i < NPROC; i++) {
    memset(&proc_table[i], 0, sizeof(proc_table[i]));
    proc_table[i].kstack_top = (uint64)&kstacks[i][KSTACK_SIZE];
  }
}

struct proc *
myproc(void)
{
  return current_proc;
}

static struct proc *
allocproc(void)
{
  for (int i = 0; i < NPROC; i++) {
    struct proc *p = &proc_table[i];
    if (p->state != UNUSED)
      continue;
    p->state = USED;
    p->pid = nextpid++;
    p->tf = kalloc();
    if (!p->tf || !(p->pagetable = uvmcreate((uint64)p->tf))) {
      freeproc(p);
      return 0;
    }
    p->context.sp = p->kstack_top;
    p->context.ra = (uint64)first_return;
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
  current_proc = initproc = p;
  if (kexec("sh") < 0)
    panic("userinit exec");
  dump_pagetable(p->pagetable);
  p->state = RUNNABLE;
  current_proc = 0;
}

void
scheduler(void)
{
  for (;;) {
    intr_on();
    intr_off();
    int ran = 0;
    for (int i = 0; i < NPROC; i++) {
      struct proc *p = &proc_table[i];
      if (p->state != RUNNABLE)
        continue;
      current_proc = p;
      p->state = RUNNING;
      swtch(&scheduler_context, &p->context);
      current_proc = 0;
      ran = 1;
    }
    if (!ran)
      asm volatile("wfi");
  }
}

static void
sched(void)
{
  struct proc *p = myproc();
  if (!p || intr_get() || p->state == RUNNING)
    panic("sched state");
  swtch(&p->context, &scheduler_context);
}

void
yield(void)
{
  intr_off();
  myproc()->state = RUNNABLE;
  sched();
}

void
sleep(void *chan)
{
  if (intr_get())
    panic("sleep with interrupts");
  struct proc *p = myproc();
  p->chan = chan;
  p->state = SLEEPING;
  sched();
  p->chan = 0;
}

void
wakeup(void *chan)
{
  for (int i = 0; i < NPROC; i++)
    if (proc_table[i].state == SLEEPING && proc_table[i].chan == chan)
      proc_table[i].state = RUNNABLE;
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
    return -1;
  }
  *child->tf = *parent->tf;
  child->tf->a0 = 0;
  child->parent = parent;
  child->sz = parent->sz;
  child->heap_base = parent->heap_base;
  memcpy(child->name, parent->name, sizeof(child->name));
  child->state = RUNNABLE;
  return child->pid;
}

int
kwait(uint64 addr)
{
  struct proc *parent = myproc();
  if (addr && usercheck(parent->pagetable, addr, sizeof(int), PTE_W) < 0)
    return -1;
  for (;;) {
    int children = 0;
    for (int i = 0; i < NPROC; i++) {
      struct proc *p = &proc_table[i];
      if (p->state == UNUSED || p->parent != parent)
        continue;
      children = 1;
      if (p->state == ZOMBIE) {
        if (addr && copyout(parent->pagetable, addr, &p->exit_status, sizeof(int)) < 0)
          return -1;
        int pid = p->pid;
        freeproc(p);
        return pid;
      }
    }
    if (!children)
      return -1;
    sleep(parent);
  }
}

void
kexit(int status)
{
  struct proc *p = myproc();
  if (p == initproc)
    panic("initial shell exited");
  for (int i = 0; i < NPROC; i++) {
    if (proc_table[i].parent == p) {
      proc_table[i].parent = initproc;
      wakeup(initproc);
    }
  }
  p->exit_status = status;
  p->state = ZOMBIE;
  wakeup(p->parent);
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
    printf("pid=%d name=%s sz=%x free=%d\n", p->pid, p->name, p->sz, kfreepages());
    dump_pagetable(p->pagetable);
  }
}
