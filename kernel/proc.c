#include "types.h"
#include "course_sid.h"
#include "param.h"
#include "memlayout.h"
#include "proc.h"
#include "defs.h"

#define KSTACK_SIZE (4096 * LAB1_STACK_KB)

struct proc proc_table[NPROC];
struct proc *current_proc;

static uchar kstacks[NPROC][KSTACK_SIZE] __attribute__((aligned(16)));
static int nextpid = 1;

extern void userret(uint64);

static void
freeproc(struct proc *p)
{
  p->pid = 0;
  p->state = UNUSED;
  p->parent = 0;
  p->exit_status = 0;
  p->image_size = 0;
  p->wait_addr = 0;
  p->waiting = 0;
  memset(&p->tf, 0, sizeof(p->tf));
  memset(p->name, 0, sizeof(p->name));
}

void
procinit(void)
{
  int i;

  current_proc = 0;
  for (i = 0; i < NPROC; i++) {
    memset(&proc_table[i], 0, sizeof(proc_table[i]));
    proc_table[i].kstack_top = (uint64)&kstacks[i][KSTACK_SIZE];
    proc_table[i].state = UNUSED;
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
  int i;
  struct proc *p;

  for (i = 0; i < NPROC; i++) {
    p = &proc_table[i];
    if (p->state != UNUSED)
      continue;
    p->pid = nextpid++;
    p->state = USED;
    p->parent = 0;
    p->exit_status = 0;
    p->image_size = USER_IMAGE_MAX;
    p->wait_addr = 0;
    p->waiting = 0;
    memset(&p->tf, 0, sizeof(p->tf));
    memset(p->image, 0, sizeof(p->image));
    memset(p->name, 0, sizeof(p->name));
    return p;
  }
  return 0;
}

void
userinit(void)
{
  struct proc *p = allocproc();
  if (p == 0)
    panic("userinit");
  current_proc = p;
  p->parent = 0;
  if (kexec("sh") < 0)
    panic("userinit exec");
  p->state = RUNNABLE;
  current_proc = 0;
}

static void
run_user(struct proc *p)
{
  current_proc = p;
  p->state = RUNNING;
  memcpy((void *)USER_BASE, p->image, USER_IMAGE_MAX);
  prepare_return(p);
  userret(0);
}

void
scheduler(void)
{
  int i;

  for (;;) {
    for (i = 0; i < NPROC; i++) {
      if (proc_table[i].state == RUNNABLE)
        run_user(&proc_table[i]);
    }
    uartpoll();
  }
}

void
schedule_after_trap(int ignored)
{
  (void)ignored;
  if (current_proc != 0 && current_proc->state == RUNNING)
    current_proc->state = RUNNABLE;
  scheduler();
}

int
kfork(void)
{
  struct proc *parent = myproc();
  struct proc *child = allocproc();

  if (parent == 0 || child == 0)
    return -1;
  child->parent = parent;
  child->tf = parent->tf;
  child->tf.a0 = 0;
  child->image_size = parent->image_size;
  memcpy(child->image, parent->image, USER_IMAGE_MAX);
  child->state = RUNNABLE;
  child->name[0] = 'c';
  child->name[1] = 'h';
  child->name[2] = 'i';
  child->name[3] = 'l';
  child->name[4] = 'd';
  return child->pid;
}

static int
has_child(struct proc *parent)
{
  int i;
  for (i = 0; i < NPROC; i++)
    if (proc_table[i].parent == parent && proc_table[i].state != UNUSED)
      return 1;
  return 0;
}

static int
reap_one(struct proc *parent, uint64 addr)
{
  int i;
  struct proc *p;
  int pid;

  for (i = 0; i < NPROC; i++) {
    p = &proc_table[i];
    if (p->parent != parent || p->state != ZOMBIE)
      continue;
    pid = p->pid;
    if (addr != 0 && addr >= USER_BASE && addr + sizeof(int) <= USER_BASE + USER_IMAGE_MAX)
      memcpy((void *)addr, &p->exit_status, sizeof(int));
    freeproc(p);
    return pid;
  }
  return -1;
}

int
kwait(uint64 addr)
{
  struct proc *p = myproc();
  int pid;

  if (p == 0)
    return -1;
  pid = reap_one(p, addr);
  if (pid >= 0)
    return pid;
  if (!has_child(p))
    return -1;

  p->wait_addr = addr;
  p->waiting = 1;
  p->state = SLEEPING;
  scheduler();
  return -1;
}

void
wake_parent(struct proc *child)
{
  struct proc *parent = child->parent;

  if (parent == 0 || parent->state != SLEEPING || !parent->waiting)
    return;
  if (parent->wait_addr != 0 &&
      parent->wait_addr >= USER_BASE &&
      parent->wait_addr + sizeof(int) <= USER_BASE + USER_IMAGE_MAX)
    memcpy(parent->image + (parent->wait_addr - USER_BASE),
           &child->exit_status, sizeof(int));
  parent->tf.a0 = child->pid;
  parent->wait_addr = 0;
  parent->waiting = 0;
  parent->state = RUNNABLE;
  freeproc(child);
}

void
kexit(int status)
{
  struct proc *p = myproc();
  int i;

  if (p == 0)
    panic("exit without process");
  p->exit_status = status;
  p->state = ZOMBIE;
  wake_parent(p);
  for (i = 0; i < NPROC; i++) {
    if (proc_table[i].parent == p)
      proc_table[i].parent = 0;
  }
  scheduler();
}
