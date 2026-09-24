#include "types.h"
#include "memlayout.h"
#include "proc.h"
#include "defs.h"

int
sys_fork(void)
{
  return kfork();
}

int
sys_exit(void)
{
  int status;
  if (argint(0, &status) < 0)
    status = -1;
  kexit(status);
  return -1;
}

int
sys_wait(void)
{
  uint64 addr;
  if (argaddr(0, &addr) < 0)
    return -1;
  return kwait(addr);
}

int
sys_read(void)
{
  int fd, n;
  uint64 addr;
  if (argint(0, &fd) < 0 || argaddr(1, &addr) < 0 || argint(2, &n) < 0)
    return -1;
  if (fd != 0 || n < 0)
    return -1;
  return consoleread(addr, n);
}

int
sys_write(void)
{
  int fd, n;
  uint64 addr;
  if (argint(0, &fd) < 0 || argaddr(1, &addr) < 0 || argint(2, &n) < 0)
    return -1;
  if ((fd != 1 && fd != 2) || n < 0)
    return -1;
  return consolewrite(addr, n);
}

int
sys_exec(void)
{
  char name[32];
  int i;
  if (argstr(0, name, sizeof(name)) < 0)
    return -1;
  for (i = 0; name[i] != '\0'; i++) {
    if (name[i] == '\n' || name[i] == '\r') {
      name[i] = '\0';
      break;
    }
  }
  return kexec(name);
}

int
sys_getpid(void)
{
  struct proc *p = myproc();
  return p == 0 ? -1 : p->pid;
}

int
sys_unimplemented(void)
{
  return -1;
}
