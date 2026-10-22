#include "kernel/types.h"
#include "user/user.h"

#define PAGE 4096
#define CHUNK (16 * PAGE)
static void
require(int ok, const char *stage)
{
  if (!ok) {
    printf("VMSTRESS FAIL %s\n", stage);
    exit(1);
  }
}

int
main(void)
{
  char *initial = sbrk(0);
  require(sbrk(-1) == (char *)-1, "heap floor");
  char *page = sbrklazy(PAGE);
  require(page == initial, "lazy eager ABI");
  page[0] = 42;
  int pid = fork();
  require(pid >= 0, "fork");
  if (!pid) {
    page[0] = 9;
    require(exec("missing", 0) == -1 && page[0] == 9, "failed exec");
    exit(21);
  }
  int status;
  require(wait(&status) == pid && status == 21 && page[0] == 42, "isolation");
  require(sbrk(-PAGE) != (char *)-1 && sbrk(PAGE) == page && page[0] == 0, "regrow zero");
  require(sbrk(-PAGE) != (char *)-1, "restore break");

  int capacities[2];
  for (int round = 0; round < 2; round++) {
    int total = 0;
    while (sbrk(CHUNK) != (char *)-1)
      total += CHUNK;
    char *boundary = sbrk(0);
    require(sbrk(CHUNK) == (char *)-1 && sbrk(0) == boundary, "growth rollback");
    while (sbrk(PAGE) != (char *)-1)
      total += PAGE;
    capacities[round] = total;
    initial[0] = 64;
    require(exec("hi", 0) == -1 && initial[0] == 64, "exec OOM keeps image");

    /* Leave enough pages to begin fork/exec, but not enough to finish. */
    require(sbrk(-16 * PAGE) != (char *)-1, "make fork quota");
    total -= 16 * PAGE;
    require(fork() == -1 && initial[0] == 64, "partial fork rollback");
    int recovered = 0;
    while (sbrk(PAGE) != (char *)-1)
      recovered += PAGE;
    total += recovered;
    require(recovered == 16 * PAGE, "fork page leak");
    require(sbrk(-3 * PAGE) != (char *)-1, "make exec quota");
    total -= 3 * PAGE;
    require(exec("hi", 0) == -1 && initial[0] == 64, "partial exec rollback");
    recovered = 0;
    while (sbrk(PAGE) != (char *)-1)
      recovered += PAGE;
    total += recovered;
    require(recovered == 3 * PAGE, "exec page leak");
    require(sbrk(-total) != (char *)-1 && sbrk(0) == initial, "full release");
  }
  require(capacities[0] == capacities[1], "repeat capacity");
  printf("VMSTRESS PASS fork exec shrink rollback capacity=%d\n", capacities[0]);
  exit(0);
}
