#include <stdarg.h>

void consputc(int c);

static void
printint(long long value, int base, int sign)
{
  static char digits[] = "0123456789abcdef";
  char buf[32];
  int i = 0;
  unsigned long long x;

  if (sign && value < 0) {
    x = (unsigned long long)(-(value + 1)) + 1;
    consputc('-');
  } else {
    x = (unsigned long long)value;
  }

  if (x == 0)
    buf[i++] = '0';
  while (x != 0) {
    buf[i++] = digits[x % (unsigned)base];
    x /= (unsigned)base;
  }
  while (--i >= 0)
    consputc(buf[i]);
}

static void
printptr(unsigned long long value)
{
  static char digits[] = "0123456789abcdef";
  char buf[16];
  int i = 0;

  consputc('0');
  consputc('x');
  if (value == 0)
    buf[i++] = '0';
  while (value != 0) {
    buf[i++] = digits[value & 0xf];
    value >>= 4;
  }
  while (--i >= 0)
    consputc(buf[i]);
}

void
vprintf(const char *fmt, va_list ap)
{
  int c;
  const char *s;

  for (; (c = *fmt) != 0; fmt++) {
    if (c != '%') {
      consputc(c);
      continue;
    }
    c = *++fmt;
    if (c == 'd') {
      printint(va_arg(ap, int), 10, 1);
    } else if (c == 'x') {
      printptr(va_arg(ap, unsigned long long));
    } else if (c == 's') {
      s = va_arg(ap, const char *);
      if (s == 0)
        s = "(null)";
      while (*s)
        consputc(*s++);
    } else if (c == '%') {
      consputc('%');
    } else if (c != 0) {
      consputc('%');
      consputc(c);
    }
  }
}

void
printf(const char *fmt, ...)
{
  va_list ap;

  va_start(ap, fmt);
  vprintf(fmt, ap);
  va_end(ap);
}
