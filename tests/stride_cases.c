#include "common.h"

int
main(void)
{
  ASSERT(setickets(500) == 0, "observer tickets");
  unsigned long period = tick_cycles();
  unsigned long start = time_rdtime();
  unsigned long change = start + 180 * period;
  unsigned long deadline = start + 420 * period;
  int children[2];
  for (int i = 0; i < 2; i++) {
    children[i] = fork();
    ASSERT(children[i] >= 0, "fork");
    if (!children[i]) {
      ASSERT(setickets(i ? 18 : 6) == 0, "initial tickets");
      burn_until(change);
      ASSERT(setickets(i ? 6 : 18) == 0, "change tickets");
      burn_until(deadline);
      exit(0);
    }
  }
  int samples[2][2] = {{0}};
  pause(20);
  for (int phase = 0; phase < 2; phase++) {
    int before[2] = {cpu_of(children[0]), cpu_of(children[1])};
    ASSERT(before[0] >= 0 && before[1] >= 0, "baseline sample");
    for (int window = 0; window < 3; window++) {
      pause(40);
      for (int i = 0; i < 2; i++) {
        int now = cpu_of(children[i]);
        ASSERT(now > before[i], "window starvation");
        samples[phase][i] += now - before[i];
        before[i] = now;
      }
    }
    if (!phase) {
      while (time_rdtime() < change + 30 * period)
        pause(1);
    }
  }
  for (int i = 0; i < 2; i++)
    kill(children[i]);
  ASSERT(wait(0) > 0 && wait(0) > 0, "reap");
  printf("RESULT fixed_share = %d:%d\n", samples[0][0], samples[0][1]);
  printf("RESULT changed_share = %d:%d\n", samples[1][0], samples[1][1]);
  ASSERT(samples[0][1] * 10 > samples[0][0] * 24 &&
         samples[0][1] * 10 < samples[0][0] * 36, "fixed 1:3 share");
  ASSERT(samples[1][0] * 10 > samples[1][1] * 24 &&
         samples[1][0] * 10 < samples[1][1] * 36, "changed 3:1 share");
  PASS("stride_cases");
}
