#include "kernel/types.h"
#include "user/user.h"

int
main(void)
{
  int pid = fork();
  if (pid < 0) {
    printf("VMCONCURRENT FAIL fork\n");
    exit(1);
  }
  if (pid == 0) {
    exec("spin", 0);
    exit(1);
  }
  printf("VMCONCURRENT shell and spin\n");
  exec("sh", 0);
  printf("VMCONCURRENT FAIL exec\n");
  exit(1);
}
