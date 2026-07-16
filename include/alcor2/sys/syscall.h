/**
 * @file include/alcor2/sys/syscall.h
 * @brief x86_64 syscall interface and definitions.
 *
 * Syscalls use the SYSCALL/SYSRET instructions with System V AMD64 calling
 * convention. Syscall number in RAX, arguments in RDI, RSI, RDX, R10, R8, R9.
 * Return value in RAX. RCX and R11 are clobbered by SYSCALL.
 */

#ifndef ALCOR2_SYSCALL_H
#define ALCOR2_SYSCALL_H

#include <alcor2/arch/cpu.h>
#include <alcor2/types.h>

/*
 * Syscall numbers come from the AlcorMusl contract, staged by make into
 * build/abi: <bits/syscall.h> carries the full table (POSIX numbers plus the
 * Alcor2 band from <bits/alcor_syscall.h>). The kernel defines none itself.
 */
#include <bits/syscall.h>

/**
 * @brief Initialize syscall mechanism (set MSRs).
 */
void syscall_init(void);

/**
 * @brief Syscall dispatcher (called from ASM entry).
 * @param frame Saved syscall frame.
 * @return Syscall return value.
 */
u64 syscall_dispatch(syscall_frame_t *frame);

/**
 * @brief Return the syscall_frame_t for the currently executing syscall.
 *
 * Valid only during a syscall handler invocation. Returns NULL otherwise.
 * Used by signal.c's rt_sigreturn implementation.
 */
syscall_frame_t *syscall_get_current_frame(void);

#endif
