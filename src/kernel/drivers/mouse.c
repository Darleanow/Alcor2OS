/**
 * @file src/kernel/drivers/mouse.c
 * @brief Kernel mouse broker.
 *
 * Owns the cursor position and the event ring served via /dev/mouse. The
 * producer is the PS/2 mouse IRQ handler (via @ref mouse_post_event); consumers
 * are syscalls that drain via @ref mouse_read_block. Single-waiter model: one
 * process can sleep on the ring; additional pollers get -EAGAIN.
 */

#include <alcor2/arch/cpu.h>
#include <alcor2/drivers/console.h>
#include <alcor2/drivers/mouse.h>
#include <alcor2/errno.h>
#include <alcor2/fs/ramfs.h>
#include <alcor2/fs/vfs.h>
#include <alcor2/kstdlib.h>
#include <alcor2/mm/vmm.h>
#include <alcor2/proc/proc.h>
#include <alcor2/proc/sched.h>

#define RING_CAP 256

static struct
{
  alcor_mouse_event_t ring[RING_CAP];
  u16                 head; /* producer (IRQ) */
  u16                 tail; /* consumer (syscall) */
  bool                relative;
  bool                moved; /* set on first real motion; gates the cursor */
  i32                 cursor_x;
  i32                 cursor_y;
  u32                 screen_w;
  u32                 screen_h;
  proc_t             *waiter;
} g;

static inline u16 ring_count(void)
{
  return (u16)((g.head - g.tail) & (RING_CAP - 1));
}

static inline bool ring_empty(void)
{
  return g.head == g.tail;
}

static inline bool ring_full(void)
{
  return ring_count() == RING_CAP - 1;
}

static i64 mouse_dev_read(void *ctx, void *buf, u64 count, u64 offset)
{
  (void)ctx;
  (void)offset;
  if(count < sizeof(alcor_mouse_event_t))
    return -EINVAL;
  if(!vmm_is_user_range(buf, sizeof(alcor_mouse_event_t)))
    return -EFAULT;

  /* Non-blocking: return EAGAIN immediately if the ring is empty.
   * Callers that need blocking reads should use poll()/select() first. */
  cpu_disable_interrupts();
  if(ring_empty()) {
    cpu_enable_interrupts();
    return -EAGAIN;
  }
  alcor_mouse_event_t ev = g.ring[g.tail & (RING_CAP - 1)];
  g.tail                 = (u16)(g.tail + 1);
  cpu_enable_interrupts();

  kmemcpy(buf, &ev, sizeof(ev));
  return (i64)sizeof(ev);
}

static i64 mouse_dev_write(void *ctx, const void *buf, u64 count, u64 offset)
{
  (void)ctx;
  (void)buf;
  (void)count;
  (void)offset;
  return -EROFS;
}

typedef struct
{
  i32 x;
  i32 y;
} mouse_pos_t;

static i64 mouse_dev_ioctl(void *ctx, u64 request, u64 arg)
{
  (void)ctx;
  switch(request) {
  case ALCOR_IOC_MOUSE_SET_RELATIVE: {
    if(!vmm_is_user_range((const void *)arg, sizeof(u32)))
      return -EFAULT;
    u32 v;
    kmemcpy(&v, (const void *)arg, sizeof(v));
    mouse_set_relative(v != 0);
    return 0;
  }
  case ALCOR_IOC_MOUSE_GET_RELATIVE: {
    if(!vmm_is_user_range((void *)arg, sizeof(u32)))
      return -EFAULT;
    u32 v = mouse_is_relative() ? 1u : 0u;
    kmemcpy((void *)arg, &v, sizeof(v));
    return 0;
  }
  case ALCOR_IOC_MOUSE_GET_POS: {
    if(!vmm_is_user_range((void *)arg, sizeof(mouse_pos_t)))
      return -EFAULT;
    mouse_pos_t p;
    mouse_get_cursor(&p.x, &p.y);
    kmemcpy((void *)arg, &p, sizeof(p));
    return 0;
  }
  default:
    return -ENOTTY;
  }
}

/**
 * @brief Report read-readiness for @c /dev/mouse.
 *
 * POLL_IN is set when the event ring has at least one pending event. Writes
 * always fail with @c -EROFS, so POLL_OUT is reported as unavailable.
 *
 * @param ctx     Unused.
 * @param events  Requested events.
 * @return Subset of @p events that are actionable now.
 */
static u32 mouse_dev_poll(void *ctx, u32 events)
{
  (void)ctx;
  u32 ready = 0;
  if((events & POLL_IN) && !ring_empty())
    ready |= POLL_IN;
  return ready;
}

static const ramfs_chardev_ops_t mouse_chardev_ops = {
    .read  = mouse_dev_read,
    .write = mouse_dev_write,
    .ioctl = mouse_dev_ioctl,
    .poll  = mouse_dev_poll,
};

void mouse_init(void)
{
  kmemset(&g, 0, sizeof(g));
  g.screen_w = 640;
  g.screen_h = 480;
  g.cursor_x = (i32)(g.screen_w / 2);
  g.cursor_y = (i32)(g.screen_h / 2);

  i64 rc = ramfs_chardev_register("/mouse", &mouse_chardev_ops, NULL);
  if(rc < 0)
    console_printf("[mouse] /dev/mouse register failed: %d\n", (int)rc);
}

void mouse_set_screen(u32 width, u32 height)
{
  if(width == 0 || height == 0)
    return;
  cpu_disable_interrupts();
  g.screen_w = width;
  g.screen_h = height;
  g.cursor_x = (i32)(width / 2);
  g.cursor_y = (i32)(height / 2);
  cpu_enable_interrupts();
}

void mouse_post_event(i32 dx, i32 dy, i16 dwheel, u8 buttons)
{
  /* Producer is the IRQ handler (interrupts already off). Update cursor. */
  if(dx != 0 || dy != 0)
    g.moved = true;

  if(!g.relative) {
    i64 nx = (i64)g.cursor_x + dx;
    i64 ny = (i64)g.cursor_y + dy;
    if(nx < 0)
      nx = 0;
    if(ny < 0)
      ny = 0;
    if(nx >= g.screen_w)
      nx = (i64)g.screen_w - 1;
    if(ny >= g.screen_h)
      ny = (i64)g.screen_h - 1;
    g.cursor_x = (i32)nx;
    g.cursor_y = (i32)ny;
  } else {
    g.cursor_x = (i32)(g.screen_w / 2);
    g.cursor_y = (i32)(g.screen_h / 2);
  }

  if(!ring_full()) {
    alcor_mouse_event_t *e = &g.ring[g.head & (RING_CAP - 1)];
    e->dx                  = dx;
    e->dy                  = dy;
    e->dwheel              = dwheel;
    e->buttons             = buttons;
    e->flags               = 0;
    g.head                 = (u16)(g.head + 1);
  }

  if(g.waiter) {
    proc_t *w = g.waiter;
    g.waiter  = NULL;
    proc_wake(w);
  }
}

/**
 * @brief Drop @p me from the waiter slot under the IRQ lock.
 *
 * Pulled out so the signal-bail path in @ref mouse_read_block is a single
 * call instead of an inline cpu_disable/enable bracket repeating the lock
 * structure. Only clears the slot when it still belongs to @p me, a
 * concurrent IRQ may have already woken us and handed it off.
 *
 * @param me  Process that may currently own the waiter slot.
 */
static void mouse_clear_waiter_if(const proc_t *me)
{
  cpu_disable_interrupts();
  if(g.waiter == me)
    g.waiter = NULL;
  cpu_enable_interrupts();
}

/**
 * @brief Block until a mouse event arrives, then deliver it.
 *
 * Three paths inside the loop, in priority order: (1) ring non-empty →
 * pop and return; (2) no proc context or another reader already parked →
 * spin (cannot park); (3) park as the single waiter and yield. After the
 * wake-up, a deliverable signal short-circuits with @c -EINTR so the
 * syscall-return path can run handlers / default actions.
 *
 * @param out  Output event slot.
 * @return 0 on success, @c -EAGAIN with no proc context, @c -EINTR on signal.
 */
i64 mouse_read_block(alcor_mouse_event_t *out)
{
  while(1) {
    cpu_disable_interrupts();
    if(!ring_empty()) {
      *out   = g.ring[g.tail & (RING_CAP - 1)];
      g.tail = (u16)(g.tail + 1);
      cpu_enable_interrupts();
      return 0;
    }

    proc_t *me = proc_current();
    if(!me || g.waiter) {
      cpu_enable_interrupts();
      if(!me)
        return -EAGAIN;
      __asm__ volatile("pause");
      continue;
    }
    g.waiter = me;
    proc_block(me);
    cpu_enable_interrupts();
    proc_schedule();
    if(proc_signal_pending(me)) {
      mouse_clear_waiter_if(me);
      return -EINTR;
    }
  }
}

void mouse_set_relative(bool enabled)
{
  cpu_disable_interrupts();
  g.relative = enabled;
  g.cursor_x = (i32)(g.screen_w / 2);
  g.cursor_y = (i32)(g.screen_h / 2);
  /* Drain the ring on every mode change: stale deltas/clicks must not replay
   * into the next session (e.g. a stuck camera when doom is relaunched). */
  g.head = g.tail;
  cpu_enable_interrupts();
}

bool mouse_is_relative(void)
{
  return g.relative;
}

void mouse_get_cursor(i32 *out_x, i32 *out_y)
{
  if(out_x)
    *out_x = g.cursor_x;
  if(out_y)
    *out_y = g.cursor_y;
}

bool mouse_has_moved(void)
{
  return g.moved;
}
