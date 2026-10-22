#include "types.h"
#include "proc.h"
#include "syscall.h"
#include "pagetable.h"
#include "defs.h"

int
argaddr(int n, uint64 *value)
{
  struct trapframe *tf = myproc()->tf;
  if (n < 0 || n > 2)
    return -1;
  *value = n == 0 ? tf->a0 : n == 1 ? tf->a1 : tf->a2;
  return 0;
}

int
argint(int n, int *value)
{
  uint64 raw;
  if (argaddr(n, &raw) < 0)
    return -1;
  *value = (int)raw;
  return 0;
}

int
argstr(int n, char *buf, int max)
{
  uint64 addr;
  if (max <= 0 || argaddr(n, &addr) < 0)
    return -1;
  return copyinstr(myproc()->pagetable, buf, addr, max);
}

uint64 sys_fork(void), sys_exit(void), sys_wait(void), sys_read(void);
uint64 sys_write(void), sys_exec(void), sys_getpid(void), sys_sbrk(void);

static uint64 (*syscalls[23])(void) = {
  [SYS_fork] = sys_fork,
  [SYS_exit] = sys_exit,
  [SYS_wait] = sys_wait,
  [SYS_read] = sys_read,
  [SYS_exec] = sys_exec,
  [SYS_getpid] = sys_getpid,
  [SYS_sbrk] = sys_sbrk,
  [SYS_write] = sys_write,
};

void
syscall(void)
{
  struct proc *p = myproc();
  uint64 num = p->tf->a7;
  p->tf->a0 = num < 23 && syscalls[num] ? syscalls[num]() : (uint64)-1;
}
