#include "common.h"

static void
run(char c)
{
  for (int i = 0; i < 16; i++) {
    for (volatile int d = 0; d < 2000000; d++)
      ;
    write(1, &c, 1);
  }
  exit(0);
}

int
main(void)
{
  int a = fork();
  ASSERT(a >= 0, "fork A");
  if (!a)
    run('A');
  int b = fork();
  ASSERT(b >= 0, "fork B");
  if (!b)
    run('B');
  int status;
  ASSERT(wait(&status) > 0 && status == 0, "wait A/B");
  ASSERT(wait(&status) > 0 && status == 0, "wait B/A");
  printf("\nalt done\n");
  exit(0);
}
