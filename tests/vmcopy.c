#include "kernel/types.h"
#include "kernel/riscv.h"
#include "kernel/memlayout.h"
#include "user/user.h"

static void
require(int ok)
{
  if (!ok) {
    printf("VMCOPY FAIL\n");
    exit(1);
  }
}

int
main(void)
{
  char *base = sbrk(2 * PGSIZE);
  require(base != (char *)-1 && ((uint64)base % PGSIZE) == 0);
  char *cross = base + PGSIZE - 2;
  memcpy(cross, "cross-page\n", 11);
  require(write(1, cross, 11) == 11);
  require(write(1, (void *)(MAXVA - 1), 2) == -1);
  require(write(1, (void *)TRAPFRAME, 1) == -1);
  require(write(1, (void *)~0UL, 2) == -1);
  require(read(0, (void *)0, 1) == -1);
  require(read(0, (void *)TRAPFRAME, 1) == -1);
  /* Invalid next page must fail before consuming input. */
  require(read(0, base + 2 * PGSIZE - 1, 2) == -1);
  require(exec((char *)TRAPFRAME, 0) == -1);

  printf("VMCOPY input-ready\n");
  int n = 0;
  while (n < 5) {
    int got = read(0, cross + n, 5 - n);
    require(got > 0);
    n += got;
  }
  require(memcmp(cross, "abcd\n", 5) == 0);

  int pid = fork();
  require(pid >= 0);
  if (!pid)
    exit(37);
  require(wait((int *)TRAPFRAME) == -1);
  require(wait((int *)cross) == pid);
  int status;
  memcpy(&status, cross, sizeof(status));
  require(status == 37);

  memcpy(cross, "hi", 3);
  pid = fork();
  require(pid >= 0);
  if (!pid) {
    exec(cross, 0);
    exit(1);
  }
  require(wait(&status) == pid && status == 0);
  require(sbrk(-2 * PGSIZE) != (char *)-1);
  printf("VMCOPY PASS copyin copyout copyinstr permissions\n");
  exit(0);
}
