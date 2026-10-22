#include "types.h"
#include "course_sid.h"
#include "riscv.h"

void main(void);

__attribute__((aligned(16))) char stack0[1024 * LAB1_STACK_KB];

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
  w_menvcfg(r_menvcfg() | MENVCFG_STCE | MENVCFG_ADUE);
  w_mcounteren(r_mcounteren() | 2);
  w_stimecmp(r_time() + 100000);
  w_tp(r_mhartid());
  asm volatile("mret");
  for (;;)
    ;
}
