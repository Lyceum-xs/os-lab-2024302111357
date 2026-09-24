#include "types.h"
#include "course_sid.h"
#include "memlayout.h"
#include "riscv.h"
#include "proc.h"
#include "defs.h"

extern char trampoline[];
extern char uservec[];

static uint timer_ticks;

void
trapinit(void)
{
  timer_ticks = 0;
}

void
timerintr(void)
{
  timer_ticks++;
  *(volatile uint64 *)CLINT_MTIMECMP(0) =
      *(volatile uint64 *)CLINT_MTIME + 1000000;
}

int
timer_should_yield(void)
{
  return LAB2_TICK <= 1 || timer_ticks % LAB2_TICK == 0;
}

static void
sync_from_user(struct proc *p)
{
  memcpy(p->image, (void *)USER_BASE, USER_IMAGE_MAX);
}

static void
sync_to_user(struct proc *p)
{
  memcpy((void *)USER_BASE, p->image, USER_IMAGE_MAX);
}

void
prepare_return(struct proc *p)
{
  uint64 x;

  intr_off();
  w_stvec(TRAMPOLINE + (uint64)(uservec - trampoline));

  p->tf.kernel_satp = 0;
  p->tf.kernel_sp = p->kstack_top;
  p->tf.kernel_trap = (uint64)usertrap;
  p->tf.kernel_hartid = r_tp();

  x = r_sstatus();
  x &= ~SSTATUS_SPP;
  x |= SSTATUS_SPIE;
  w_sstatus(x);
  w_sepc(p->tf.epc);

  memcpy((void *)TRAPFRAME, &p->tf, sizeof(p->tf));
  sync_to_user(p);
}

uint64
usertrap(void)
{
  struct proc *p = myproc();
  uint64 scause;
  int which_dev = 0;

  if (p == 0)
    panic("usertrap: no process");
  if ((r_sstatus() & SSTATUS_SPP) != 0)
    panic("usertrap: not from user mode");

  memcpy(&p->tf, (void *)TRAPFRAME, sizeof(p->tf));
  p->tf.epc = r_sepc();
  scause = r_scause();

  if (scause == 8) {
    p->tf.epc += 4;
    syscall();
  } else {
    which_dev = devintr();
    if (which_dev == 0)
      kexit(-1);
  }

  sync_from_user(p);

  if (which_dev == 2 && timer_should_yield())
    schedule_after_trap(1);
  if (p->state != RUNNING)
    scheduler();

  prepare_return(p);
  return 0;
}
