#include "types.h"
#include "proc.h"
#include "syscall.h"
#include "memlayout.h"
#include "defs.h"

static int
valid_user_range(uint64 addr, int n)
{
  if (n < 0 || addr < USER_BASE)
    return 0;
  if ((uint64)n > USER_BASE + USER_IMAGE_MAX - addr)
    return 0;
  return 1;
}

int
copyin(void *dst, uint64 src, int n)
{
  if (!valid_user_range(src, n))
    return -1;
  memcpy(dst, (void *)src, (uint)n);
  return 0;
}

int
copyout(uint64 dst, const void *src, int n)
{
  if (!valid_user_range(dst, n))
    return -1;
  memcpy((void *)dst, src, (uint)n);
  return 0;
}

int
copyinstr(char *dst, uint64 src, int max)
{
  int i;
  if (max <= 0)
    return -1;
  for (i = 0; i < max; i++) {
    if (!valid_user_range(src + (uint64)i, 1))
      return -1;
    dst[i] = *(char *)(src + (uint64)i);
    if (dst[i] == '\0')
      return 0;
  }
  dst[max - 1] = '\0';
  return -1;
}

int
argint(int n, int *ip)
{
  struct proc *p = myproc();
  if (p == 0 || n < 0 || n > 2)
    return -1;
  *ip = (int)(n == 0 ? p->tf.a0 : n == 1 ? p->tf.a1 : p->tf.a2);
  return 0;
}

int
argaddr(int n, uint64 *ip)
{
  struct proc *p = myproc();
  if (p == 0 || n < 0 || n > 2)
    return -1;
  *ip = n == 0 ? p->tf.a0 : n == 1 ? p->tf.a1 : p->tf.a2;
  return 0;
}

int
argstr(int n, char *buf, int max)
{
  uint64 addr;
  if (argaddr(n, &addr) < 0)
    return -1;
  return copyinstr(buf, addr, max);
}

int sys_fork(void);
int sys_exit(void);
int sys_wait(void);
int sys_read(void);
int sys_write(void);
int sys_exec(void);
int sys_getpid(void);
int sys_unimplemented(void);

static int (*syscalls[23])(void) = {
  [SYS_fork] = sys_fork,
  [SYS_exit] = sys_exit,
  [SYS_wait] = sys_wait,
  [SYS_pipe] = sys_unimplemented,
  [SYS_read] = sys_read,
  [SYS_kill] = sys_unimplemented,
  [SYS_exec] = sys_exec,
  [SYS_fstat] = sys_unimplemented,
  [SYS_chdir] = sys_unimplemented,
  [SYS_dup] = sys_unimplemented,
  [SYS_getpid] = sys_getpid,
  [SYS_sbrk] = sys_unimplemented,
  [SYS_pause] = sys_unimplemented,
  [SYS_uptime] = sys_unimplemented,
  [SYS_open] = sys_unimplemented,
  [SYS_write] = sys_write,
  [SYS_mknod] = sys_unimplemented,
  [SYS_unlink] = sys_unimplemented,
  [SYS_link] = sys_unimplemented,
  [SYS_mkdir] = sys_unimplemented,
  [SYS_close] = sys_unimplemented,
  [SYS_sync] = sys_unimplemented,
};

void
syscall(void)
{
  struct proc *p = myproc();
  uint64 num;

  if (p == 0)
    return;
  num = p->tf.a7;
  if (num < sizeof(syscalls) / sizeof(syscalls[0]) && syscalls[num] != 0)
    p->tf.a0 = syscalls[num]();
  else
    p->tf.a0 = (uint64)-1;
}
