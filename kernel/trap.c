#include "types.h"
#include "course_sid.h"
#include "memlayout.h"
#include "riscv.h"
#include "proc.h"
#include "pagetable.h"
#include "defs.h"

extern char trampoline[], uservec[], kernelvec[];
static uint ticks;
static struct spinlock tickslock;

void
trapinit(void)
{
  initlock(&tickslock, "ticks");
  w_stvec((uint64)kernelvec);
}

void
timerintr(void)
{
  w_stimecmp(r_time() + 100000);
  acquire(&tickslock);
  ticks++;
  wakeup(&ticks);
  release(&tickslock);
}

int
pause_ticks(int n)
{
  if (n < 0)
    return -1;
  acquire(&tickslock);
  uint start = ticks;
  while ((uint)(ticks - start) < (uint)n) {
    if (killed(myproc())) {
      release(&tickslock);
      return -1;
    }
    sleep(&ticks, &tickslock);
  }
  release(&tickslock);
  return 0;
}

uint
uptime_ticks(void)
{
  acquire(&tickslock);
  uint value = ticks;
  release(&tickslock);
  return value;
}

uint64
prepare_return(struct proc *p)
{
  intr_off();
  p->tf->kernel_satp = MAKE_SATP(kernel_pagetable);
  p->tf->kernel_sp = p->kstack_top;
  p->tf->kernel_trap = (uint64)usertrap;
  p->tf->kernel_hartid = r_tp();
  w_stvec(TRAMPOLINE + (uint64)(uservec - trampoline));
  w_sstatus((r_sstatus() & ~SSTATUS_SPP) | SSTATUS_SPIE);
  w_sepc(p->tf->epc);
  return MAKE_SATP(p->pagetable);
}

uint64
usertrap(void)
{
  w_stvec((uint64)kernelvec);
  struct proc *p = myproc();
  if (!p || (r_sstatus() & SSTATUS_SPP))
    panic("usertrap origin");
  p->tf->epc = r_sepc();
  uint64 cause = r_scause();
  int device = 0;
  if (cause == 8) {
    if (killed(p))
      kexit(-1);
    p->tf->epc += 4;
    intr_on();
    syscall();
  } else if (!(device = devintr())) {
    printf("USER-FAULT pid=%d cause=%x epc=%x va=%x\n",
           p->pid, cause, p->tf->epc, r_stval());
    kexit(-1);
  }
  if (device == 2 && proc_tick())
    yield();
  if (killed(p))
    kexit(-1);
  return prepare_return(p);
}

void
kerneltrap(void)
{
  uint64 epc = r_sepc(), status = r_sstatus();
  if (!(status & SSTATUS_SPP) || intr_get() || !devintr()) {
    printf("KERNEL-FAULT cause=%x epc=%x va=%x\n", r_scause(), epc, r_stval());
    panic("kerneltrap");
  }
  w_sepc(epc);
  w_sstatus(status);
}
