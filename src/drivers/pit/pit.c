/**
 * @file src/drivers/pit/pit.c
 * @brief 8253/8254 PIT timer driver.
 */

#include <alcor2/arch/idt.h>
#include <alcor2/arch/io.h>
#include <alcor2/arch/pic.h>
#include <alcor2/arch/pit.h>
#include <alcor2/proc/sched.h>

void fb_console_tick(void);

#define PIT_CHANNEL0 0x40
#define PIT_CMD      0x43
#define PIT_FREQ     1193182

/* Plain u64 load/store is atomic on x86_64 (single MOV). An i386 port would
 * need lock-prefixed 64-bit access or a seqcount for tear-free pit_get_ticks.
 */
static volatile u64 ticks           = 0;
static bool         preempt_enabled = false;

static void         pit_irq_handler(u8 irq)
{
  (void)irq;
  ticks++;
  fb_console_tick();
  if(preempt_enabled)
    proc_tick();
}

void pit_init(u32 frequency)
{
  u16 divisor = PIT_FREQ / frequency;

  outb(PIT_CMD, 0x36);
  outb(PIT_CHANNEL0, divisor & 0xFF);
  outb(PIT_CHANNEL0, (divisor >> 8) & 0xFF);

  pic_unmask(IRQ_TIMER);
  irq_register(IRQ_TIMER, pit_irq_handler);
}

void pit_enable_sched(void)
{
  preempt_enabled = true;
}

u64 pit_get_ticks(void)
{
  return ticks;
}
