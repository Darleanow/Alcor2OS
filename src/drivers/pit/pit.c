/**
 * @file src/drivers/pit/pit.c
 * @brief 8253/8254 PIT timer driver.
 */

#include <alcor2/arch/io.h>
#include <alcor2/arch/pit.h>
#include <alcor2/proc/proc.h>

#define PIT_CHANNEL0 0x40
#define PIT_CMD      0x43
#define PIT_FREQ     1193182

/* Plain u64 load/store is atomic on x86_64 (single MOV). An i386 port would
 * need lock-prefixed 64-bit access or a seqcount for tear-free pit_get_ticks.
 */
static volatile u64 ticks           = 0;
static bool         preempt_enabled = false;

/**
 * @brief Initialize the PIT to generate timer interrupts.
 *
 * Configures the PIT in mode 3 (square wave generator) to produce
 * periodic interrupts at the specified frequency.
 *
 * @param frequency Desired tick frequency in Hz (typically 100Hz).
 */
void pit_init(u32 frequency)
{
  u16 divisor = PIT_FREQ / frequency;

  outb(PIT_CMD, 0x36);
  outb(PIT_CHANNEL0, divisor & 0xFF);
  outb(PIT_CHANNEL0, (divisor >> 8) & 0xFF);
}

/**
 * @brief Enable preemptive scheduling on timer ticks.
 *
 * After calling this, the PIT will invoke the scheduler on each tick,
 * enabling preemptive multitasking.
 */
void pit_enable_sched(void)
{
  preempt_enabled = true;
}

/**
 * @brief PIT interrupt handler (called by IRQ0 handler).
 *
 * Increments the tick counter and invokes the scheduler if scheduling is
 * enabled.
 */
void pit_tick(void)
{
  ticks++;

  if(preempt_enabled) {
    proc_tick();
  }
}

/**
 * @brief Get the number of PIT ticks since initialization.
 * @return Tick count.
 */
u64 pit_get_ticks(void)
{
  return ticks;
}
