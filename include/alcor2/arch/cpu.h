/**
 * @file include/alcor2/arch/cpu.h
 * @brief x86_64 CPU control and state management.
 *
 * Low-level operations: halting, interrupts, MSR access for thread-local
 * storage.
 */

#ifndef ALCOR2_CPU_H
#define ALCOR2_CPU_H

#include <alcor2/types.h>

/**
 * @brief Saved registers on syscall entry (x86_64 System V layout).
 *
 * The ASM syscall stub pushes registers in this exact order so the dispatcher
 * receives a pointer to this struct. Offsets are verified by _Static_assert.
 */
typedef struct syscall_frame
{
  u64 r15, r14, r13, r12, r11, r10, r9, r8;
  u64 rbp, rdi, rsi, rdx, rcx, rbx;
  u64 rax;
  u64 rip;
  u64 rflags;
  u64 rsp;
} syscall_frame_t;

_Static_assert(offsetof(syscall_frame_t, rax) == 14 * 8, "syscall_frame rax");
_Static_assert(offsetof(syscall_frame_t, rip) == 15 * 8, "syscall_frame rip");
_Static_assert(
    offsetof(syscall_frame_t, rflags) == 16 * 8, "syscall_frame rflags"
);
_Static_assert(offsetof(syscall_frame_t, rsp) == 17 * 8, "syscall_frame rsp");

/** @name MSR indices and flags for the x86_64 SYSCALL/SYSRET mechanism. */
#define MSR_EFER   0xC0000080
#define MSR_STAR   0xC0000081
#define MSR_LSTAR  0xC0000082
#define MSR_SFMASK 0xC0000084
#define EFER_SCE   (1 << 0)
/** @} */

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

/**
 * @brief Set the GS base MSR.
 * @param addr Linear address for GS segment base.
 */
void cpu_set_gs_base(u64 addr);

/**
 * @brief Get the current GS base MSR value.
 * @return GS base address.
 */
u64 cpu_get_gs_base(void);

#endif
