#include "kernel/types.h"
#include "kernel/param.h"
#include "kernel/course_sid.h"
#include "kernel/riscv.h"
#include "kernel/memlayout.h"
#include "user/user.h"

static void require(int ok, const char *stage)
{
  if (!ok) {
    printf("COWTEST FAIL %s\n", stage);
    exit(1);
  }
}

static void reap(int pid, int expected)
{
  int status;
  require(wait(&status) == pid && status == expected, "child status");
}

static void no_shared(void)
{
  require(pmc(1) == 0, "shared pages after reap");
}

static void exec_cases(void)
{
  char *area = sbrk(3 * PGSIZE);
  require(area != SBRK_ERROR, "argv allocation");
  char *cross = area + PGSIZE - 2;
  strcpy(cross, "cross-page");
  char *args[] = {"cowtest", "args", "alpha", "", cross, 0};
  int pid = fork();
  require(pid >= 0, "argv fork");
  if (!pid) {
    exec("cowtest", args);
    exit(1);
  }
  reap(pid, 0);
  no_shared();
  require(exec("hi", (char **)TRAPFRAME) == -1, "bad argv table");
  require(exec("hi", (char **)(MAXVA - 4)) == -1, "argv table overflow");
  char *bad[] = {(char *)TRAPFRAME, 0};
  require(exec("hi", bad) == -1, "bad argv string");
  require(exec((char *)TRAPFRAME, args) == -1, "bad program name");
  memset(area, 'x', PGSIZE);
  char *longarg[] = {area, 0};
  require(exec("hi", longarg) == -1, "unterminated argument");
  area[PGSIZE - 1] = 0;
  require(exec("hi", longarg) == -1, "argument stack overflow");
  char *many[MAXARG + 1];
  for (int i = 0; i < MAXARG; i++)
    many[i] = "a";
  many[MAXARG] = 0;
  require(exec("hi", many) == -1, "too many arguments");
  many[0] = "cowtest";
  many[1] = "maxargs";
  many[MAXARG - 1] = 0;
  pid = fork();
  require(pid >= 0, "maxargs fork");
  if (!pid) {
    exec("cowtest", many);
    exit(1);
  }
  reap(pid, 0);
  /* Place the argv pointer itself across a page boundary. */
  char *single[] = {"hi", 0};
  char **crossargv = (char **)(area + 2 * PGSIZE - 4);
  memcpy(crossargv, single, sizeof(single));
  pid = fork();
  require(pid >= 0, "crossargv fork");
  if (!pid) {
    exec("hi", crossargv);
    exit(1);
  }
  reap(pid, 0);
  require(sbrk(-3 * PGSIZE) != SBRK_ERROR, "argv release");
  no_shared();
  printf("COWTEST exec arguments and rejection PASS\n");
}

int main(int argc, char **argv)
{
  if (argc > 1) {
    require(((uint64)argv & 15) == 0 && argv[argc] == 0, "argv layout");
    if (!strcmp(argv[1], "args")) {
      require(argc == 5 && !strcmp(argv[0], "cowtest") && !strcmp(argv[2], "alpha") &&
              !strcmp(argv[3], "") && !strcmp(argv[4], "cross-page"), "argv contents");
    } else {
      require(!strcmp(argv[1], "maxargs") && argc == MAXARG - 1, "maxarg count");
      for (int i = 2; i < argc; i++)
        require(!strcmp(argv[i], "a"), "maxarg content");
    }
    printf("COWTEST argv child PASS argc=%d\n", argc);
    exit(0);
  }
  require(pmc(-1) == (unsigned long long)-1 && pmc(2) == (unsigned long long)-1,
          "pmc invalid selector");
  char *page = sbrk(PGSIZE);
  require(page != SBRK_ERROR, "single page");
  for (int round = 0; round < 8; round++) {
    page[0] = 11;
    int pid = fork();
    require(pid >= 0, "alternating fork");
    if (!pid) {
      if (round & 1)
        pause(2);
      page[0] = 22;
      pause(3);
      require(page[0] == 22, "child isolated");
      exit(0);
    }
    if (!(round & 1))
      pause(2);
    page[0] = 33;
    reap(pid, 0);
    require(page[0] == 33, "parent isolated");
    no_shared();
  }
  printf("COWTEST alternating writes PASS\n");
  page[0] = 17;
  for (int i = 0; i < 8; i++) {
    int pid = fork();
    require(pid >= 0, "concurrent fork");
    if (!pid) {
      page[0] = 40 + i;
      pause(3);
      require(page[0] == 40 + i, "concurrent isolation");
      exit(10 + i);
    }
  }
  uint seen = 0;
  for (int i = 0; i < 8; i++) {
    int status;
    require(wait(&status) > 0 && status >= 10 && status < 18, "concurrent wait");
    require(!(seen & (1U << (status - 10))), "duplicate exit status");
    seen |= 1U << (status - 10);
  }
  require(page[0] == 17 && seen == 255, "concurrent parent");
  no_shared();
  require(sbrk(-PGSIZE) != SBRK_ERROR, "single release");
  printf("COWTEST concurrent children PASS shared=0\n");

  int pid = fork();
  require(pid >= 0, "readonly fork");
  if (!pid) {
    asm volatile("sd zero, 0(zero)" ::: "memory");
    exit(1);
  }
  reap(pid, -1);
  no_shared();
  printf("COWTEST readonly protection PASS\n");

  int pages = LAB5_COW_BUDGET + 2;
  char *bulk = sbrk(pages * PGSIZE);
  require(bulk != SBRK_ERROR, "budget memory");
  for (int i = 0; i < pages; i++)
    bulk[i * PGSIZE] = 7;
  pid = fork();
  require(pid >= 0, "budget fork");
  if (!pid) {
    for (int i = 0; i < pages; i++)
      bulk[i * PGSIZE] = 9;
    for (int i = 0; i < pages; i++)
      require(bulk[i * PGSIZE] == 9, "budget continued copying");
    exit(0);
  }
  reap(pid, 0);
  for (int i = 0; i < pages; i++)
    require(bulk[i * PGSIZE] == 7, "budget parent data");
  require(sbrk(-pages * PGSIZE) != SBRK_ERROR, "budget release");
  no_shared();
  printf("COWTEST budget downgrade PASS pages=%d\n", pages);
  exec_cases();
  printf("COWTEST all PASS shared=%llu\n", pmc(1));
  exit(0);
}
