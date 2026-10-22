#include "kernel/types.h"
#include "kernel/course_sid.h"
#include "user/user.h"

int
main(void)
{
  volatile char local = 0;
  uint64 stack_base = (uint64)&local & ~4095UL;
  for (int page = 1; page <= LAB3_GUARD_PAGES; page++) {
    for (int write_access = 0; write_access < 2; write_access++) {
      int pid = fork();
      if (pid < 0)
        goto fail;
      if (pid == 0) {
        volatile char *guard = (char *)(stack_base - page * 4096);
        if (write_access)
          *guard = 1;
        else
          local = *guard;
        exit(0);
      }
      int status = 0;
      if (wait(&status) != pid || status == 0)
        goto fail;
    }
  }
  printf("VMGUARD PASS pages=%d reads+writes killed\n", LAB3_GUARD_PAGES);
  exit(0);
fail:
  printf("VMGUARD FAIL\n");
  exit(1);
}
