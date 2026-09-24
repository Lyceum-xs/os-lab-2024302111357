#include "types.h"
#include "memlayout.h"

#define UART_THR 0
#define UART_LSR 5
#define UART_LSR_TX_IDLE 0x20

static inline void
uartwrite(uint64 addr, uchar value)
{
  *(volatile uchar *)addr = value;
}

static inline uchar
uartread(uint64 addr)
{
  return *(volatile uchar *)addr;
}

void
uartputc_sync(int c)
{
  while ((uartread(UART0 + UART_LSR) & UART_LSR_TX_IDLE) == 0)
    ;
  uartwrite(UART0 + UART_THR, (uchar)c);
}

void
consputc(int c)
{
  uartputc_sync(c);
}

void
consoleinit(void)
{
}
