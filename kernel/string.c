#include "types.h"

void *
memset(void *dst, int c, uint n)
{
  uchar *p = dst;
  while (n--)
    *p++ = (uchar)c;
  return dst;
}

void *
memcpy(void *dst, const void *src, uint n)
{
  uchar *d = dst;
  const uchar *s = src;
  while (n--)
    *d++ = *s++;
  return dst;
}

int
memcmp(const void *a, const void *b, uint n)
{
  const uchar *x = a;
  const uchar *y = b;
  while (n--) {
    if (*x != *y)
      return *x - *y;
    x++;
    y++;
  }
  return 0;
}

uint
strlen(const char *s)
{
  uint n = 0;
  while (s[n])
    n++;
  return n;
}

int
strcmp(const char *a, const char *b)
{
  while (*a && *a == *b) {
    a++;
    b++;
  }
  return (uchar)*a - (uchar)*b;
}
