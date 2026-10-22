#include "types.h"
#include "course_sid.h"
#include "memlayout.h"
#include "riscv.h"
#include "proc.h"
#include "pagetable.h"
#include "defs.h"

extern char trampoline[], uservec[], kernelvec[];
static uint timer_ticks;

void
trapinit(void)
{
  w_stvec((uint64)kernelvec);
}

void
timerintr(void)
{
  timer_ticks++;
  w_stimecmp(r_time() + 100000);
}

int
timer_should_yield(void)
{
  return timer_ticks % LAB2_TICK == 0;
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
    p->tf->epc += 4;
    syscall();
  } else if (!(device = devintr())) {
    printf("USER-FAULT pid=%d cause=%x epc=%x va=%x\n",
           p->pid, cause, p->tf->epc, r_stval());
    kexit(-1);
  }
  if (device == 2 && timer_should_yield())
    yield();
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
