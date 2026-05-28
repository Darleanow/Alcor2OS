/**
 * @file include/alcor2/ktermios.h
 * @brief musl-compatible termios blob (60 bytes) for per-process TTY
 * discipline.
 *
 * Must match musl sizeof(struct termios) on x86_64 and sys_ioctl TCGETS/TCSETS.
 */

#ifndef ALCOR2_KTERMIOS_H
#define ALCOR2_KTERMIOS_H

#include <alcor2/types.h>

#define KTERM_MUSL_NCCS 32
#define KTERM_SZ        60

typedef struct
{
  u32 c_iflag;
  u32 c_oflag;
  u32 c_cflag;
  u32 c_lflag;
  u8  c_line;
  u8  pad[3];
  u8  c_cc[KTERM_MUSL_NCCS];
  u32 __c_ispeed;
  u32 __c_ospeed;
} k_termios_t;

_Static_assert(sizeof(k_termios_t) == KTERM_SZ, "k_termios_t must be 60 bytes");

/* musl arch/generic/bits/termios.h (octal) */
#define KTERM_ICRNL  0000400u
#define KTERM_ONLCR  0000004u
#define KTERM_CS8    0000060u
#define KTERM_CREAD  0000200u
#define KTERM_CLOCAL 0004000u
#define KTERM_ISIG   0000001u
#define KTERM_ICANON 0000002u
#define KTERM_ECHO   0000010u
#define KTERM_IEXTEN 0100000u
#define KTERM_B38400 0000017u

/* c_cc indices (musl arch/generic/bits/termios.h) */
#define KTERM_VINTR  0
#define KTERM_VQUIT  1
#define KTERM_VERASE 2
#define KTERM_VKILL  3
#define KTERM_VEOF   4
#define KTERM_VTIME  5
#define KTERM_VMIN   6

/* TTY ioctl request codes (Linux/musl wire ABI). Single source of truth: every
 * driver and syscall path that handles a TTY ioctl reads them from here. */
#define KTERM_TCGETS     0x5401
#define KTERM_TCSETS     0x5402
#define KTERM_TCSETSW    0x5403
#define KTERM_TCSETSF    0x5404
#define KTERM_TIOCGWINSZ 0x5413
#define KTERM_TIOCSWINSZ 0x5414

/**
 * @brief Linux-compatible @c struct @c winsize layout for TIOCGWINSZ/SWINSZ.
 *
 * Layout matches musl @c bits/ioctl.h so unmodified userspace TUIs (ncurses,
 * fleed, &hellip;) marshal it without translation.
 */
typedef struct
{
  u16 row, col, xpixel, ypixel;
} k_winsize_t;

/* VT100-era 80x25 fallback used when the console reports a zero geometry —
 * only happens if a TTY ioctl races boot before fb_console_init wrote a real
 * grid. Lets TUIs lay out something sane instead of dividing by zero. */
#define KTERM_WINSIZE_FALLBACK_COLS 80
#define KTERM_WINSIZE_FALLBACK_ROWS 25

void ktermios_init_default(k_termios_t *t);

#endif
