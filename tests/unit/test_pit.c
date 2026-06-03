/* Unit tests for src/drivers/pit/pit.c — divisor clamping, frequency
 * tracking, and the fast-mode vote counter. */

#include "test_common.h"

#include <alcor2/arch/pit.h>
#include <alcor2/types.h>

#include <string.h>

#define ALCOR2_IO_H
#define outb(p, v) ((void)(p), (void)(v))
#define inb(p)     ((u8)0)
#define outw(p, v) ((void)(p), (void)(v))
#define inw(p)     ((u16)0)
#define outl(p, v) ((void)(p), (void)(v))
#define inl(p)     ((u32)0)
#define io_wait()  ((void)0)

/* External symbols pulled in by pit.c. */
void fb_console_tick(void) {}
void pic_unmask(u8 irq)
{
  (void)irq;
}
void irq_register(u8 irq, void (*h)(u8))
{
  (void)irq;
  (void)h;
}
void proc_tick(void) {}

#include "../../src/drivers/pit/pit.c"

static int reset(void **state)
{
  (void)state;
  ticks           = 0;
  mono_ns         = 0;
  preempt_enabled = false;
  fast_votes      = 0;
  current_hz      = PIT_TICK_HZ;
  ns_per_tick     = 1000000000u / PIT_TICK_HZ;
  return 0;
}

/* pit_program / frequency tracking */

static void init_sets_requested_frequency(void **state)
{
  (void)state;
  pit_init(PIT_TICK_HZ);
  assert_int_equal(pit_get_frequency(), PIT_TICK_HZ);
}

static void set_frequency_updates_getter(void **state)
{
  (void)state;
  pit_set_frequency(500);
  assert_int_equal(pit_get_frequency(), 500);
}

static void frequency_clamped_below_min(void **state)
{
  (void)state;
  /* Divisor must fit in 16 bits: min valid = PIT_FREQ/65535 ≈ 19. */
  pit_set_frequency(1);
  assert_int_equal(pit_get_frequency(), 19);
}

static void frequency_clamped_above_max(void **state)
{
  (void)state;
  pit_set_frequency(0xFFFFFFFF);
  assert_int_equal(pit_get_frequency(), PIT_FREQ);
}

/* ns_per_tick consistency */

static void ns_per_tick_matches_hz(void **state)
{
  (void)state;
  pit_set_frequency(1000);
  assert_int_equal(ns_per_tick, 1000000000u / 1000u);
}

/* ticks / ns counters */

static void get_ticks_initially_zero(void **state)
{
  (void)state;
  assert_int_equal(pit_get_ticks(), 0);
}

static void get_ns_initially_zero(void **state)
{
  (void)state;
  assert_int_equal(pit_get_ns(), 0);
}

/* fast-mode vote counter */

static void request_fast_switches_to_fast_hz(void **state)
{
  (void)state;
  pit_request_fast();
  assert_int_equal(pit_get_frequency(), PIT_TICK_HZ_FAST);
  assert_int_equal(fast_votes, 1);
}

static void release_fast_restores_normal(void **state)
{
  (void)state;
  pit_request_fast();
  pit_release_fast();
  assert_int_equal(pit_get_frequency(), PIT_TICK_HZ);
  assert_int_equal(fast_votes, 0);
}

static void double_request_stays_fast(void **state)
{
  (void)state;
  pit_request_fast();
  pit_request_fast();
  assert_int_equal(fast_votes, 2);
  pit_release_fast();
  assert_int_equal(pit_get_frequency(), PIT_TICK_HZ_FAST); /* still fast */
  assert_int_equal(fast_votes, 1);
  pit_release_fast();
  assert_int_equal(pit_get_frequency(), PIT_TICK_HZ);
  assert_int_equal(fast_votes, 0);
}

static void release_without_request_is_noop(void **state)
{
  (void)state;
  pit_release_fast(); /* votes=0, must not wrap */
  assert_int_equal(fast_votes, 0);
  assert_int_equal(pit_get_frequency(), PIT_TICK_HZ);
}

static void reset_fast_clears_all_votes(void **state)
{
  (void)state;
  pit_request_fast();
  pit_request_fast();
  pit_reset_fast();
  assert_int_equal(fast_votes, 0);
  assert_int_equal(pit_get_frequency(), PIT_TICK_HZ);
}

static void reset_fast_noop_when_not_fast(void **state)
{
  (void)state;
  pit_reset_fast();
  assert_int_equal(pit_get_frequency(), PIT_TICK_HZ);
}

static void pit_irq_increments_ticks(void **state)
{
  (void)state;
  pit_set_frequency(1000);
  u64 before = pit_get_ticks();
  pit_irq_handler(0);
  assert_int_equal(pit_get_ticks(), before + 1);
  assert_int_equal(pit_get_ns(), ns_per_tick);
}

static void pit_irq_calls_proc_tick_when_preempt_enabled(void **state)
{
  (void)state;
  preempt_enabled = true;
  pit_irq_handler(0);
  assert_int_equal(ticks, 1);
  preempt_enabled = false;
}

static void pit_enable_sched_sets_flag(void **state)
{
  (void)state;
  assert_false(preempt_enabled);
  pit_enable_sched();
  assert_true(preempt_enabled);
}

int main(void)
{
  const struct CMUnitTest tests[] = {
      cmocka_unit_test_setup(init_sets_requested_frequency, reset),
      cmocka_unit_test_setup(set_frequency_updates_getter, reset),
      cmocka_unit_test_setup(frequency_clamped_below_min, reset),
      cmocka_unit_test_setup(frequency_clamped_above_max, reset),
      cmocka_unit_test_setup(ns_per_tick_matches_hz, reset),
      cmocka_unit_test_setup(get_ticks_initially_zero, reset),
      cmocka_unit_test_setup(get_ns_initially_zero, reset),
      cmocka_unit_test_setup(request_fast_switches_to_fast_hz, reset),
      cmocka_unit_test_setup(release_fast_restores_normal, reset),
      cmocka_unit_test_setup(double_request_stays_fast, reset),
      cmocka_unit_test_setup(release_without_request_is_noop, reset),
      cmocka_unit_test_setup(reset_fast_clears_all_votes, reset),
      cmocka_unit_test_setup(reset_fast_noop_when_not_fast, reset),
      cmocka_unit_test_setup(pit_irq_increments_ticks, reset),
      cmocka_unit_test_setup(pit_irq_calls_proc_tick_when_preempt_enabled, reset),
      cmocka_unit_test_setup(pit_enable_sched_sets_flag, reset),
  };
  return cmocka_run_group_tests(tests, NULL, NULL);
}
