/**
 * @file include/alcor2/arch/pit.h
 * @brief 8253/8254 PIT (Programmable Interval Timer) driver.
 *
 * Configures the PIT for periodic timer interrupts and tracks system ticks.
 */

#ifndef ALCOR2_PIT_H
#define ALCOR2_PIT_H

#include <alcor2/types.h>

/** @brief Default tick rate in Hz (idle/system rate). */
#define PIT_TICK_HZ 250u

/** @brief Elevated tick rate for latency-sensitive apps. */
#define PIT_TICK_HZ_FAST 1000u

/** @brief ioctl(0, ALCOR2_IOC_TIMER_FAST, &uint32_t on); _IOW('T', 1, u32).
 *
 * Refcounted vote for ::PIT_TICK_HZ_FAST, auto-released on process exit.
 * Userland half lives in the musl fork's @c <alcor2/timer.h>. */
#define ALCOR2_IOC_TIMER_FAST ((1U << 30) | (0x54U << 8) | 1U | (4U << 16))

/**
 * @brief Program the PIT and install the timer IRQ handler.
 * @param frequency Initial tick rate in Hz.
 */
void pit_init(u32 frequency);

/**
 * @brief Change the tick rate at runtime.
 *
 * Timekeeping is unaffected: ::pit_get_ns advances by the per-tick interval in
 * effect, so the rate may change freely.
 * @param frequency New tick rate in Hz.
 */
void pit_set_frequency(u32 frequency);

/** @brief Current tick rate in Hz. */
u32 pit_get_frequency(void);

/**
 * @brief Add a vote for ::PIT_TICK_HZ_FAST (refcounted).
 *
 * The rate is raised while any vote is outstanding and restored to
 * ::PIT_TICK_HZ when the last is released.
 */
void pit_request_fast(void);

/** @brief Remove one fast-rate vote. */
void pit_release_fast(void);

/** @brief Clear all fast-rate votes; called on process exit. */
void pit_reset_fast(void);

/** @brief Run the scheduler on each tick (enable preemption). */
void pit_enable_sched(void);

/**
 * @brief Raw tick count since init.
 * @return Ticks elapsed. Rate-dependent — use ::pit_get_ns for wall time.
 */
u64 pit_get_ticks(void);

/**
 * @brief Monotonic time since init, continuous across rate changes.
 * @return Nanoseconds elapsed.
 */
u64 pit_get_ns(void);

#endif
