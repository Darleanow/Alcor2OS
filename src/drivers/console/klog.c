/**
 * @file src/drivers/console/klog.c
 * @brief Debugcon-only kernel log. See @c include/alcor2/drivers/klog.h.
 */

#include <alcor2/arch/io.h>
#include <alcor2/drivers/klog.h>
#include <alcor2/types.h>
#include <stdarg.h>

/** @brief Sideband debug-console port — sinks bytes to the host backend
 * when one is attached, discards them otherwise. */
#define DEBUGCON_PORT 0xE9

/**
 * @brief Emit a single byte to debugcon.
 *
 * @param c  Byte to write.
 */
static inline void klog_putc(char c)
{
  outb(DEBUGCON_PORT, (u8)c);
}

/**
 * @brief Emit a NUL-terminated string to debugcon.
 *
 * @param s  String to write; @c NULL prints "(null)".
 */
static void klog_puts(const char *s)
{
  if(!s)
    s = "(null)";
  while(*s)
    klog_putc(*s++);
}

/**
 * @brief Emit an unsigned integer in base @p base.
 *
 * @param v     Value to print.
 * @param base  Radix (10 or 16 supported).
 */
static void klog_putuint(u64 v, u32 base)
{
  static const char digits[] = "0123456789abcdef";
  char              buf[32];
  u32               n = 0;

  if(v == 0) {
    klog_putc('0');
    return;
  }
  while(v && n < sizeof(buf)) {
    buf[n++] = digits[v % base];
    v /= base;
  }
  while(n)
    klog_putc(buf[--n]);
}

/**
 * @brief Emit a signed integer in base 10.
 *
 * @param v  Value to print.
 */
static void klog_putint(i64 v)
{
  if(v < 0) {
    klog_putc('-');
    v = -v;
  }
  klog_putuint((u64)v, 10);
}

void klogf(const char *fmt, ...)
{
  va_list ap;
  va_start(ap, fmt);
  for(; *fmt; fmt++) {
    if(*fmt != '%') {
      klog_putc(*fmt);
      continue;
    }
    fmt++;
    /* Skip the 'l' length modifier — every integer arg is promoted to
     * 64-bit through va_arg anyway. */
    if(*fmt == 'l')
      fmt++;
    switch(*fmt) {
    case 's':
      klog_puts(va_arg(ap, const char *));
      break;
    case 'c':
      klog_putc((char)va_arg(ap, int));
      break;
    case 'd':
      klog_putint(va_arg(ap, i64));
      break;
    case 'u':
      klog_putuint(va_arg(ap, u64), 10);
      break;
    case 'x':
      klog_putuint(va_arg(ap, u64), 16);
      break;
    case '%':
      klog_putc('%');
      break;
    case '\0':
      /* dangling '%' at end of format — stop cleanly */
      va_end(ap);
      return;
    default:
      /* Unknown specifier — echo verbatim so callers see the typo. */
      klog_putc('%');
      klog_putc(*fmt);
      break;
    }
  }
  va_end(ap);
}
