#include "types.h"
#include "memlayout.h"
#include "riscv.h"
#include "defs.h"

void
plicinit(void)
{
  *(volatile uint32 *)(PLIC_PRIORITY + UART0_IRQ * 4) = 1;
}

void
plicinithart(void)
{
  *(volatile uint32 *)PLIC_SENABLE(0) |= (1 << UART0_IRQ);
  *(volatile uint32 *)PLIC_SPRIORITY(0) = 0;
}

int
devintr(void)
{
  uint64 scause = r_scause();

  if (scause == (1ull << 63 | 5) || scause == (1ull << 63 | 7)) {
    timerintr();
    return 2;
  }

  if (scause != (1ull << 63 | 9))
    return 0;

  int irq = *(volatile uint32 *)PLIC_SCLAIM(0);
  if (irq == UART0_IRQ) {
    uartintr();
    *(volatile uint32 *)PLIC_SCLAIM(0) = irq;
    return 1;
  }
  if (irq != 0)
    *(volatile uint32 *)PLIC_SCLAIM(0) = irq;
  return 0;
}
