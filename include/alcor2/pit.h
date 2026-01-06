/**
 * @file include/alcor2/pit.h
 * @brief 8253/8254 PIT (Programmable Interval Timer) driver.
 *
 * Configures the PIT for periodic timer interrupts and tracks system ticks.
 */

#ifndef ALCOR2_PIT_H
#define ALCOR2_PIT_H

#include <alcor2/types.h>

/**
 * @brief Initialize the PIT.
 * @param frequency Desired tick frequency in Hz.
 */
void pit_init(u32 frequency);

/**
 * @brief Enable preemptive scheduling on timer tick.
 */
void pit_enable_sched(void);

/**
 * @brief Get the number of PIT ticks since initialization.
 * @return Tick count.
 */
u64 pit_get_ticks(void);

#endif
