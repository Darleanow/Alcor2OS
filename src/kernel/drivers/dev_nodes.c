/**
 * @file src/kernel/drivers/dev_nodes.c
 * @brief /dev/null, /dev/zero, and /dev/tty character devices.
 *
 * Registered as ramfs chardevs during kernel init.
 * /dev/null:  reads return EOF, writes are silently discarded.
 * /dev/zero:  reads fill the buffer with zeroes, writes are silently discarded.
 * /dev/tty:   reads delegate to the keyboard line discipline, writes delegate
 *             to fb_console, ioctls handle termios and FB_CONSOLE controls.
 */

#include <alcor2/arch/pit.h>
#include <alcor2/drivers/console.h>
#include <alcor2/drivers/fb_console.h>
#include <alcor2/drivers/fb_user.h>
#include <alcor2/errno.h>
#include <alcor2/fs/ramfs.h>
#include <alcor2/fs/vfs.h>
#include <alcor2/kbd.h>
#include <alcor2/kstdlib.h>
#include <alcor2/ktermios.h>
#include <alcor2/mm/vmm.h>
#include <alcor2/proc/proc.h>
#include <alcor2/types.h>

/**
 * @brief Read from /dev/null — always returns 0 (EOF).
 *
 * @param ctx     Unused.
 * @param buf     Destination buffer (untouched).
 * @param count   Requested byte count (ignored).
 * @param offset  File offset (ignored).
 * @return Always 0.
 */
static i64 null_read(void *ctx, void *buf, u64 count, u64 offset)
{
  (void)ctx;
  (void)buf;
  (void)count;
  (void)offset;
  return 0;
}

/**
 * @brief Write to /dev/null — silently discards all data.
 *
 * @param ctx     Unused.
 * @param buf     Source buffer (ignored).
 * @param count   Number of bytes to "write".
 * @param offset  File offset (ignored).
 * @return @p count (pretends all bytes were written).
 */
static i64 null_write(void *ctx, const void *buf, u64 count, u64 offset)
{
  (void)ctx;
  (void)buf;
  (void)offset;
  return (i64)count;
}

/**
 * @brief Read from /dev/zero — fills the buffer with zero bytes.
 *
 * @param ctx     Unused.
 * @param buf     Destination buffer; filled with 0x00.
 * @param count   Number of bytes to produce.
 * @param offset  File offset (ignored).
 * @return @p count.
 */
static i64 zero_read(void *ctx, void *buf, u64 count, u64 offset)
{
  (void)ctx;
  (void)offset;
  kzero(buf, count);
  return (i64)count;
}

/**
 * @brief Read from /dev/tty — delegates to the keyboard line discipline.
 *
 * @param ctx     Unused.
 * @param buf     Destination buffer.
 * @param count   Maximum bytes to read.
 * @param offset  File offset (ignored for a TTY).
 * @return Bytes read, or 0 on EOF.
 */
static i64 tty_read(void *ctx, void *buf, u64 count, u64 offset)
{
  (void)ctx;
  (void)offset;
  proc_t *p = proc_current();
  if(p)
    return (i64)kbd_read_for_process(p, (char *)buf, count);
  return (i64)kbd_read_translated((char *)buf, count);
}

/**
 * @brief Write to /dev/tty — delegates to fb_console.
 *
 * @param ctx     Unused.
 * @param buf     Source buffer.
 * @param count   Number of bytes to write.
 * @param offset  File offset (ignored for a TTY).
 * @return @p count.
 */
static i64 tty_write(void *ctx, const void *buf, u64 count, u64 offset)
{
  (void)ctx;
  (void)offset;
  fb_console_write(buf, (size_t)count);
  return (i64)count;
}

/**
 * @brief Helper to safely copy @p n bytes from kernel to a user-space pointer.
 *
 * @param uptr  User-space destination address.
 * @param src   Kernel source pointer.
 * @param n     Byte count.
 * @return 0 on success, @c -EFAULT if the range is invalid.
 */
static i64 copy_to_user(u64 uptr, const void *src, u64 n)
{
  if(!vmm_is_user_range((void *)uptr, n))
    return -EFAULT;
  kmemcpy((void *)uptr, src, n);
  return 0;
}

/**
 * @brief Helper to safely copy @p n bytes from user-space to a kernel buffer.
 *
 * @param dst   Kernel destination.
 * @param uptr  User-space source address.
 * @param n     Byte count.
 * @return 0 on success, @c -EFAULT if the range is invalid.
 */
static i64 copy_from_user(void *dst, u64 uptr, u64 n)
{
  if(!vmm_is_user_range((void *)uptr, n))
    return -EFAULT;
  kmemcpy(dst, (const void *)uptr, n);
  return 0;
}

/**
 * @brief Forward an ioctl request to fb_console.
 *
 * Handles @c FB_CONSOLE_SET_ATLAS, @c FB_CONSOLE_YIELD, @c FB_CONSOLE_RECLAIM.
 * Used by both @c tty_ioctl and @c fb_ioctl to keep the two surfaces in sync.
 *
 * @param request  ioctl request code.
 * @param arg      User-space pointer to the ioctl argument (only used for
 *                 @c FB_CONSOLE_SET_ATLAS).
 * @return 0 on success, @c -ENOTTY if @p request is not an fb_console control,
 *         negative @c -errno on validation failure.
 */
static i64 fb_console_ioctl_forward(u64 request, u64 arg)
{
  if(request == FB_CONSOLE_SET_ATLAS) {
    if(!vmm_is_user_range((void *)arg, sizeof(fb_console_atlas_t)))
      return -EFAULT;
    fb_console_atlas_t meta;
    kmemcpy(&meta, (void *)arg, sizeof(meta));
    return fb_console_set_atlas(&meta) == 0 ? 0 : -EINVAL;
  }
  if(request == FB_CONSOLE_YIELD) {
    fb_console_yield();
    return 0;
  }
  if(request == FB_CONSOLE_RECLAIM) {
    fb_console_reclaim();
    return 0;
  }
  return -ENOTTY;
}

/**
 * @brief ioctl on /dev/tty — handles termios, winsize, keyboard layout, and
 * fb_console controls.
 *
 * @param ctx      Unused.
 * @param request  ioctl request code.
 * @param arg      User-space pointer to the ioctl argument.
 * @return 0 on success, negative @c -errno on failure.
 */
static i64 tty_ioctl(void *ctx, u64 request, u64 arg)
{
  (void)ctx;
  proc_t *p = proc_current();
  if(!p)
    return -EINVAL;

  switch(request) {
  case KTERM_TIOCGWINSZ: {
    k_winsize_t w;
    fb_console_fill_winsize(&w);
    return copy_to_user(arg, &w, sizeof(w));
  }
  case KTERM_TIOCSWINSZ:
    if(!vmm_is_user_range((void *)arg, sizeof(k_winsize_t)))
      return -EFAULT;
    return 0;
  case KTERM_TCGETS:
    return copy_to_user(arg, &p->termios, sizeof(p->termios));
  case KTERM_TCSETS:
  case KTERM_TCSETSW:
  case KTERM_TCSETSF:
    return copy_from_user(&p->termios, arg, sizeof(p->termios));

  case ALCOR2_IOC_KBD_SET_LAYOUT: {
    u32 lid;
    if(copy_from_user(&lid, arg, sizeof(lid)) < 0)
      return -EFAULT;
    if(lid >= KBD_LAYOUT_COUNT)
      return -EINVAL;
    kbd_set_layout((kbd_layout_t)lid);
    return 0;
  }
  case ALCOR2_IOC_KBD_RELEASE_EVENTS: {
    u32 on;
    if(copy_from_user(&on, arg, sizeof(on)) < 0)
      return -EFAULT;
    kbd_set_release_events(on != 0);
    return 0;
  }
  case ALCOR2_IOC_TIMER_FAST: {
    u32 on;
    if(copy_from_user(&on, arg, sizeof(on)) < 0)
      return -EFAULT;
    if(on)
      pit_request_fast();
    else
      pit_release_fast();
    return 0;
  }

  default:
    break;
  }

  return fb_console_ioctl_forward(request, arg);
}

/**
 * @brief Read from /dev/fb — returns an alcor_fb_info_t with the framebuffer
 * geometry.
 *
 * The first @c sizeof(alcor_fb_info_t) bytes yield the geometry struct.
 * Reads beyond that return 0 (EOF).
 *
 * @param ctx     Unused.
 * @param buf     Destination buffer.
 * @param count   Maximum bytes to read.
 * @param offset  Byte offset into the virtual info "file".
 * @return Bytes copied, or 0 at EOF.
 */
static i64 fb_read(void *ctx, void *buf, u64 count, u64 offset)
{
  (void)ctx;
  alcor_fb_info_t info;
  fb_user_fill_info(&info);
  u64 total = sizeof(info);
  if(offset >= total)
    return 0;
  u64 avail = total - offset;
  if(count > avail)
    count = avail;
  kmemcpy(buf, (const u8 *)&info + offset, count);
  return (i64)count;
}

/**
 * @brief ioctl on /dev/fb — forwards FB_CONSOLE_YIELD, FB_CONSOLE_RECLAIM,
 * and FB_CONSOLE_SET_ATLAS.
 *
 * @param ctx      Unused.
 * @param request  ioctl request code.
 * @param arg      User-space pointer to the ioctl argument (for SET_ATLAS).
 * @return 0 on success, negative @c -errno on failure.
 */
static i64 fb_ioctl(void *ctx, u64 request, u64 arg)
{
  (void)ctx;
  return fb_console_ioctl_forward(request, arg);
}

static const ramfs_chardev_ops_t null_ops = {
    .read  = null_read,
    .write = null_write,
};

static const ramfs_chardev_ops_t zero_ops = {
    .read  = zero_read,
    .write = null_write,
};

/**
 * @brief Report read/write readiness for @c /dev/tty.
 *
 * POLL_IN consults the keyboard line discipline against the calling process's
 * termios (canonical mode reports ready only once a full line is buffered).
 * POLL_OUT is always available — fb_console writes never block.
 *
 * @param ctx     Unused.
 * @param events  Requested events.
 * @return Subset of @p events that are actionable now.
 */
static u32 tty_poll(void *ctx, u32 events)
{
  (void)ctx;
  u32 ready = 0;
  if(events & POLL_IN) {
    const proc_t *p        = proc_current();
    bool          in_ready = p ? kbd_select_read_ready(p) : kbd_raw_pending();
    if(in_ready)
      ready |= POLL_IN;
  }
  if(events & POLL_OUT)
    ready |= POLL_OUT;
  return ready;
}

static const ramfs_chardev_ops_t tty_ops = {
    .read  = tty_read,
    .write = tty_write,
    .ioctl = tty_ioctl,
    .poll  = tty_poll,
};

static const ramfs_chardev_ops_t fb_ops = {
    .read  = fb_read,
    .ioctl = fb_ioctl,
};

/**
 * @brief Register /dev/null, /dev/zero, /dev/tty, and /dev/fb on the ramfs
 * mounted at /dev.
 */
void dev_nodes_init(void)
{
  static const struct
  {
    const char                *path;
    const ramfs_chardev_ops_t *ops;
  } nodes[] = {
      {"/null", &null_ops},
      {"/zero", &zero_ops},
      {"/tty",  &tty_ops },
      {"/fb",   &fb_ops  },
  };
  for(unsigned i = 0; i < sizeof(nodes) / sizeof(nodes[0]); i++) {
    i64 rc = ramfs_chardev_register(nodes[i].path, nodes[i].ops, NULL);
    if(rc < 0)
      console_printf(
          "[dev] /dev%s register failed: %d\n", nodes[i].path, (int)rc
      );
  }
}
