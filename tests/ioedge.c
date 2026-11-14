#include "kernel/types.h"
#include "kernel/course_sid.h"
#include "user/user.h"

static void require(int ok)
{
  if (!ok) {
    printf("IOEDGE FAIL\n");
    exit(1);
  }
}

int main(void)
{
  char buf[LAB2_BUF_SIZE + 2];
  require(read(-1, buf, 1) == -1 && read(0, buf, -1) == -1);
  require(write(0, buf, 1) == -1 && write(1, buf, -1) == -1);
  require(read(0, buf, 0) == 0 && write(1, buf, 0) == 0);
  printf("IOEDGE empty-ready\n");
  uint before = uptime();
  require(read(0, buf, 1) == 1 && buf[0] == 'Q' && uptime() - before >= 2);
  printf("IOEDGE blocked-read PASS\n");
  for (int round = 0; round < 3; round++) {
    memset(buf, 0x5a, sizeof(buf));
    printf("IOEDGE capacity-ready %d\n", round);
    int total = 0;
    while (total < LAB2_BUF_SIZE) {
      int n = read(0, buf + 1 + total, LAB2_BUF_SIZE - total);
      require(n > 0 && n <= LAB2_BUF_SIZE - total);
      total += n;
    }
    require(buf[0] == 0x5a && buf[LAB2_BUF_SIZE + 1] == 0x5a);
    for (int i = 0; i < LAB2_BUF_SIZE; i++)
      require(buf[i + 1] == 'a' + (i + round) % 26);
  }
  printf("IOEDGE PASS empty read capacity wraparound and invalid arguments\n");
  exit(0);
}
