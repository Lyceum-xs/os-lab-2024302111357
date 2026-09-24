#include "types.h"
#include "memlayout.h"
#include "defs.h"

#define RHR 0
#define THR 0
#define IER 1
#define FCR 2
#define LCR 3
#define ISR 2
#define LSR 5
#define LSR_RX_READY 0x01
#define LSR_TX_IDLE 0x20

static inline uchar
read_reg(int reg)
{
  return *(volatile uchar *)(UART0 + reg);
}

static inline void
write_reg(int reg, uchar value)
{
  *(volatile uchar *)(UART0 + reg) = value;
}

void
uartinit(void)
{
  write_reg(IER, 0);
  write_reg(LCR, 0x03);
  write_reg(FCR, 0x01);
  write_reg(IER, 0x01);
}

void
uartputc_sync(int c)
{
  while ((read_reg(LSR) & LSR_TX_IDLE) == 0)
    ;
  write_reg(THR, (uchar)c);
}

int
uartgetc(void)
{
  if ((read_reg(LSR) & LSR_RX_READY) == 0)
    return -1;
  return read_reg(RHR);
}

void
uartintr(void)
{
  int c;
  (void)read_reg(ISR);
  while ((c = uartgetc()) >= 0)
    consoleintr(c);
}

void
uartpoll(void)
{
  int c;
  while ((c = uartgetc()) >= 0)
    consoleintr(c);
}
