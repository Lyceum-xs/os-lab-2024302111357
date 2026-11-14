#include "kernel/types.h"
#include "user/user.h"

/* The supplied shell splits spaces/tabs; the supplied gets keeps newlines. */
char *sh_gets(char *buf, int max)
{
  gets(buf, max);
  uint n = strlen(buf);
  if (n && (buf[n - 1] == '\n' || buf[n - 1] == '\r'))
    buf[n - 1] = 0;
  return buf;
}
