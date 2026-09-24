#include "types.h"
#include "course_sid.h"
#include "riscv.h"

void main(void);

__attribute__((aligned(16))) char stack0[4096 * LAB1_STACK_KB * 2];

void
start(void)
{
  uint64 x;

  w_mie(0);
  x = r_mstatus();
  x &= ~MSTATUS_MPP_MASK;
  x |= MSTATUS_MPP_S;
  w_mstatus(x);
  w_mepc((uint64)main);
  w_satp(0);
  w_medeleg(0xffff);
  w_mideleg(0xffff);
  w_sie(r_sie() | SIE_SEIE | SIE_STIE);
  w_pmpaddr0(0x3fffffffffffffull);
  w_pmpcfg0(0xf);
  w_tp(r_mhartid());
  asm volatile("mret");
  for (;;)
    ;
}
