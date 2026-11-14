#include <assert.h>
#include <limits.h>
#include <stdio.h>
#include <string.h>

void kprintf(const char *, ...);
static char output[256];
static unsigned used;

void consputc(int c)
{
  assert(used + 1 < sizeof(output));
  output[used++] = c;
  output[used] = 0;
}

static void expect(const char *text)
{
  assert(strcmp(output, text) == 0);
  used = 0;
  output[0] = 0;
}

int main(void)
{
  kprintf("%d %d %d %d", 0, -1, INT_MIN, INT_MAX);
  expect("0 -1 -2147483648 2147483647");
  kprintf("[%s][%s] %%", "", "ok");
  expect("[][ok] %");
  kprintf("%x %x %x", 0ULL, 0x48ULL, ULLONG_MAX);
  expect("0x0 0x48 0xffffffffffffffff");
  char incomplete[] = {'%', 0, 'X', 0};
  kprintf(incomplete);
  expect("");
  puts("PRINTF PASS integers strings hex percent and incomplete format");
  return 0;
}
