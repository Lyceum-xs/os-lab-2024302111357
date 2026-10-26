#include "common.h"
#include "kernel/riscv.h"
#include "kernel/memlayout.h"

int
main(void)
{
  int cpu, slice;
  ASSERT(schedstat(-1, &cpu, &slice) == 0 && cpu >= 0 && slice >= 0, "self sample");
  ASSERT(schedstat(getpid(), &cpu, &slice) == 0, "self pid sample");
  ASSERT(schedstat(0, &cpu, &slice) == -1, "absent pid");
  ASSERT(schedstat(-2, &cpu, &slice) == -1, "negative pid");
  ASSERT(setickets(0) == -1 && setickets(-1) == -1, "invalid tickets");
  ASSERT(setickets(0x7fffffff) == 0 && setickets(500) == 0, "large tickets");
  ASSERT(pause(-1) == -1 && pause(0) == 0, "pause edge");
  ASSERT(kill(0) == -1 && kill(-1) == -1 && kill(1) == -1, "kill invalid/init");

  char *pages = sbrk(2 * PGSIZE);
  ASSERT(pages != (char *)-1, "sample memory");
  int *cross = (int *)(pages + PGSIZE - 2);
  ASSERT(schedstat(-1, cross, &slice) == 0, "cross-page sample");
  memcpy(&cpu, cross, sizeof(cpu));
  ASSERT(cpu >= 0, "sample content");
  int untouched = 12345;
  ASSERT(schedstat(-1, &untouched, (int *)TRAPFRAME) == -1 && untouched == 12345,
         "validate both outputs before writing");
  ASSERT(schedstat(-1, (int *)0, &slice) == -1, "readonly output");
  ASSERT(schedstat(-1, (int *)(MAXVA - 1), &slice) == -1, "overflow output");
  ASSERT(schedstat(-1, (int *)(pages + 2 * PGSIZE - 2), &slice) == -1,
         "unmapped second page");
  ASSERT(sbrk(-2 * PGSIZE) != (char *)-1, "release sample memory");

  int before = uptime();
  unsigned long t0 = time_rdtime();
  ASSERT(pause(5) == 0 && uptime() - before >= 5, "pause duration");
  ASSERT(time_rdtime() - t0 > 1000, "rdtime is CSR count, not ticks");

  int p = fork();
  ASSERT(p >= 0, "fork zombie");
  if (!p)
    exit(37);
  pause(4);
  ASSERT(schedstat(p, &cpu, &slice) == -1, "zombie not live");
  int status;
  ASSERT(wait(&status) == p && status == 37, "exit status");
  ASSERT(schedstat(p, &cpu, &slice) == -1, "reaped pid");

  for (int blocked_read = 0; blocked_read < 2; blocked_read++) {
    p = fork();
    ASSERT(p >= 0, "fork sleeper");
    if (!p) {
      if (blocked_read) {
        char c;
        read(0, &c, 1);
      } else {
        pause(1000);
      }
      exit(99);
    }
    pause(3);
    ASSERT(schedstat(p, &cpu, &slice) == 0, "sleeping pid live");
    ASSERT(kill(p) == 0, "kill sleeper");
    ASSERT(wait(&status) == p && status == -1, "killed sleeper reaped");
  }
  ASSERT(wait(0) == -1, "no children");
  PASS("schededge");
}
