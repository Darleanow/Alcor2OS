/**
 * @file include/alcor2/cpu.h
 * @brief x86_64 CPU control and state management.
 *
 * Low-level operations: halting, interrupts, MSR access for thread-local storage.
 */

#ifndef ALCOR2_CPU_H
#define ALCOR2_CPU_H

#include <alcor2/types.h>

/**
 * @brief Halt the CPU indefinitely.
 *
 * Disables interrupts and enters an infinite HLT loop.
 */
NORETURN void cpu_halt(void);

/**
 * @brief Pause instruction (hint for spinlocks).
 */
void cpu_pause(void);

/**
 * @brief Disable hardware interrupts (CLI).
 */
void cpu_disable_interrupts(void);

/**
 * @brief Enable hardware interrupts (STI).
 */
void cpu_enable_interrupts(void);

/**
 * @brief Enable SSE/AVX instructions.
 */
void cpu_enable_sse(void);

/**
 * @brief Set the FS base MSR for thread-local storage.
 * @param addr Linear address for FS segment base.
 */
void cpu_set_fs_base(u64 addr);

/**
 * @brief Get the current FS base MSR value.
 * @return FS base address.
 */
u64 cpu_get_fs_base(void);

#endif
