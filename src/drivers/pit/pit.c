/**
 * @file src/drivers/pit/pit.c
 * @brief 8253/8254 PIT timer driver with a runtime-adjustable tick rate.
 *
 * The tick rate can be raised on demand (e.g. by a game needing fine-grained
 * sleeps) and lowered again, without disturbing timekeeping: a monotonic
 * nanosecond counter is advanced by the per-tick interval in effect at each
 * interrupt, so @ref pit_get_ns stays continuous across frequency changes.
 */

#include <alcor2/arch/idt.h>
#include <alcor2/arch/io.h>
#include <alcor2/arch/pic.h>
#include <alcor2/arch/pit.h>
#include <alcor2/proc/sched.h>

void fb_console_tick(void);

#define PIT_CHANNEL0 0x40
#define PIT_CMD      0x43
#define PIT_FREQ     1193182u

/* Plain u64 load/store is atomic on x86_64 (single MOV). An i386 port would
 * need lock-prefixed 64-bit access or a seqcount for tear-free reads. */
static volatile u64 ticks           = 0;
static volatile u64 mono_ns         = 0;
static u32          current_hz      = PIT_TICK_HZ;
static u32          ns_per_tick     = 1000000000u / PIT_TICK_HZ;
static bool         preempt_enabled = false;

static void         pit_irq_handler(u8 irq)
{
  (void)irq;
  ticks++;
  mono_ns += ns_per_tick;
  fb_console_tick();
  if(preempt_enabled)
    proc_tick();
}

/** @brief Program PIT channel 0 for @p frequency Hz (clamped to a sane range). */
static void pit_program(u32 frequency)
{
  if(frequency < 19u)
    frequency = 19u; /* divisor must fit in 16 bits: PIT_FREQ/65535 ≈ 18.2 */
  if(frequency > PIT_FREQ)
    frequency = PIT_FREQ;

  u16 divisor = (u16)(PIT_FREQ / frequency);

  outb(PIT_CMD, 0x36);
  outb(PIT_CHANNEL0, divisor & 0xFF);
  outb(PIT_CHANNEL0, (divisor >> 8) & 0xFF);

  current_hz  = frequency;
  ns_per_tick = 1000000000u / frequency;
}

void pit_init(u32 frequency)
{
  pit_program(frequency);
  pic_unmask(IRQ_TIMER);
  irq_register(IRQ_TIMER, pit_irq_handler);
}

void pit_set_frequency(u32 frequency)
{
  pit_program(frequency);
}

u32 pit_get_frequency(void)
{
  return current_hz;
}

static u32 fast_votes = 0;

void       pit_request_fast(void)
{
  if(fast_votes++ == 0)
    pit_program(PIT_TICK_HZ_FAST);
}

void pit_release_fast(void)
{
  if(fast_votes == 0)
    return;
  if(--fast_votes == 0)
    pit_program(PIT_TICK_HZ);
}

void pit_reset_fast(void)
{
  if(fast_votes != 0) {
    fast_votes = 0;
    pit_program(PIT_TICK_HZ);
  }
}

void pit_enable_sched(void)
{
  preempt_enabled = true;
}

u64 pit_get_ticks(void)
{
  return ticks;
}

u64 pit_get_ns(void)
{
  return mono_ns;
}
