#include "types.h"
#include "proc.h"
#include "vm.h"
#include "defs.h"

uint64 sys_fork(void) { return kfork(); }
uint64 sys_getpid(void) { return myproc()->pid; }

uint64
sys_exit(void)
{
  int status;
  argint(0, &status);
  kexit(status);
}

uint64
sys_wait(void)
{
  uint64 addr;
  argaddr(0, &addr);
  return kwait(addr);
}

uint64
sys_read(void)
{
  int fd, n;
  uint64 addr;
  argint(0, &fd);
  argaddr(1, &addr);
  argint(2, &n);
  if (fd != 0 || n < 0)
    return (uint64)-1;
  return consoleread(addr, n);
}

uint64
sys_write(void)
{
  int fd, n;
  uint64 addr;
  argint(0, &fd);
  argaddr(1, &addr);
  argint(2, &n);
  if ((fd != 1 && fd != 2) || n < 0)
    return (uint64)-1;
  return consolewrite(addr, n);
}

uint64
sys_exec(void)
{
  char name[32];
  if (argstr(0, name, sizeof(name)) < 0)
    return (uint64)-1;
  for (int i = 0; name[i]; i++)
    if (name[i] == '\n' || name[i] == '\r') {
      name[i] = '\0';
      break;
    }
  return kexec(name);
}

uint64
sys_sbrk(void)
{
  int n, mode;
  argint(0, &n);
  argint(1, &mode);
  if (mode != SBRK_EAGER && mode != SBRK_LAZY)
    return (uint64)-1;
  return growproc(n);
}
