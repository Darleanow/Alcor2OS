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

/** @brief Number of control characters in @c c_cc — matches musl @c NCCS so
 * the 60-byte struct copies cleanly across the user/kernel boundary. */
#define KTERM_MUSL_NCCS 32

/** @brief Wire size of @ref k_termios_t — the value the static_assert below
 * enforces, also referenced by ioctl marshalling for length checks. */
#define KTERM_SZ 60

/**
 * @brief Kernel mirror of musl's @c struct @c termios.
 *
 * Layout/size are frozen by the syscall ABI (TCGETS/TCSETS copy 60 bytes
 * verbatim). Adding fields means breaking userspace; instead, store kernel-
 * private TTY state next to the per-proc termios in @c proc_t.
 */
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

/* The KTERM_* flag values below mirror musl's @c arch/generic/bits/termios.h
 * verbatim (octal in musl, octal here) so userspace setting a TCSETS bit and
 * the kernel testing it land on the same numeric value. Do not renumber. */

/** @brief Map CR to NL on input — line discipline echoes a real newline when
 * the user hits Enter. */
#define KTERM_ICRNL 0000400u

/** @brief Map NL to CR-NL on output — the framebuffer console expects \r\n,
 * so this flag papers over UNIX programs that emit bare \n. */
#define KTERM_ONLCR 0000004u

/** @brief 8-bit character size — the PS/2 layer always delivers full bytes,
 * any narrower setting would silently drop high-bit chars (UTF-8 lead bytes).
 */
#define KTERM_CS8 0000060u

/** @brief Enable receiver — required for a TTY to actually deliver input;
 * cleared by callers that want to stop reading without closing the fd. */
#define KTERM_CREAD 0000200u

/** @brief Ignore modem status lines — we have no real UART; without this flag
 * apps using termios on /dev/tty would block waiting for DCD. */
#define KTERM_CLOCAL 0004000u

/** @brief Generate signals (SIGINT/SIGQUIT/SIGTSTP) on the matching control
 * char — what makes Ctrl-C kill the foreground process via the line
 * discipline. */
#define KTERM_ISIG 0000001u

/** @brief Canonical mode (line-buffered input with editing). Cleared by
 * ncurses/vega-editor for raw byte-at-a-time reads. */
#define KTERM_ICANON 0000002u

/** @brief Echo typed characters — set for cooked input so the user sees what
 * they type; cleared for password prompts and full-screen TUIs. */
#define KTERM_ECHO 0000010u

/** @brief Enable extended input processing (musl/glibc IEXTEN). Required for
 * Ctrl-V (lnext) and ^O (discard) to be interpreted by the line discipline. */
#define KTERM_IEXTEN 0100000u

/** @brief Baud-rate bit pattern for 38400 — speed is meaningless on a
 * framebuffer console but tcgetattr+tcsetattr round-trip must preserve it
 * verbatim or musl complains. */
#define KTERM_B38400 0000017u

/* The KTERM_V* indices below identify slots inside @c k_termios_t::c_cc; same
 * numbering as musl so userspace and kernel agree which byte is which control
 * character. */

/** @brief Index of the interrupt char (default ^C). */
#define KTERM_VINTR 0

/** @brief Index of the quit char (default ^\\). */
#define KTERM_VQUIT 1

/** @brief Index of the erase char (default ^?/DEL). */
#define KTERM_VERASE 2

/** @brief Index of the kill-line char (default ^U). */
#define KTERM_VKILL 3

/** @brief Index of the EOF char (default ^D) — what triggers a 0-byte read in
 * canonical mode without closing the fd. */
#define KTERM_VEOF 4

/** @brief Non-canonical read timeout (tenths of a second). */
#define KTERM_VTIME 5

/** @brief Non-canonical minimum bytes per read — paired with VTIME to control
 * blocking behaviour. */
#define KTERM_VMIN 6

/* The KTERM_TC and KTERM_TIOC request codes below are the Linux wire ABI for
 * TTY ioctls. Single source of truth: every driver and syscall path that
 * handles a TTY ioctl reads them from here so a kernel rename can't desync
 * from a userspace expectation. */

/** @brief Read termios (Linux TCGETS). */
#define KTERM_TCGETS 0x5401

/** @brief Set termios immediately (Linux TCSETS). */
#define KTERM_TCSETS 0x5402

/** @brief Set termios after draining output (Linux TCSETSW). */
#define KTERM_TCSETSW 0x5403

/** @brief Set termios after draining output and flushing input (Linux
 * TCSETSF). */
#define KTERM_TCSETSF 0x5404

/** @brief Read window size (Linux TIOCGWINSZ). */
#define KTERM_TIOCGWINSZ 0x5413

/** @brief Write window size (Linux TIOCSWINSZ). */
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

/* The KTERM_WINSIZE_FALLBACK_* values below are the VT100-era 80x25 default,
 * used when the console reports a zero geometry — only happens if a TTY ioctl
 * races boot before fb_console_init wrote a real grid. Letting TUIs lay out
 * something sane is preferable to crashing in their divide-by-row math. */

/** @brief Fallback column count for a degenerate winsize query. */
#define KTERM_WINSIZE_FALLBACK_COLS 80

/** @brief Fallback row count for a degenerate winsize query. */
#define KTERM_WINSIZE_FALLBACK_ROWS 25

/**
 * @brief Reset @p t to the cooked-TTY defaults a freshly forked PID would
 * expect (ICRNL|ONLCR|CS8|CREAD|CLOCAL|ISIG|ICANON|ECHO|IEXTEN, B38400, the
 * standard control-char table).
 *
 * Exists so callers do not duplicate the default bit-soup — keeping the
 * initialiser in one place means a future ABI tweak only edits one file.
 *
 * @param t  Destination termios. Caller-owned, no NULL guard (a NULL here is
 *           a kernel logic bug — fail fast via crash).
 */
void ktermios_init_default(k_termios_t *t);

#endif
