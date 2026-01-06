/**
 * @file include/alcor2/user.h
 * @brief User-mode (ring-3) support functions.
 *
 * Functions to create processes, switch to user mode, and return from it.
 */

#ifndef ALCOR2_USER_H
#define ALCOR2_USER_H

#include <alcor2/types.h>

/**
 * @brief Jump to user mode and execute the given function.
 * @param entry User mode entry point.
 * @param user_rsp User mode stack pointer.
 * @return Exit code from user task.
 */
u64 user_enter(void *entry, void *user_rsp);

/**
 * @brief Return from user mode to kernel.
 * @param exit_code Exit code to return from user_enter.
 */
NORETURN void user_return(u64 exit_code);

/**
 * @brief Create a user mode task.
 * @param name Task name.
 * @param entry Entry point (must be in user-accessible memory).
 * @return Task ID on success, 0 on failure.
 */
u64 user_task_create(const char *name, void (*entry)(void));

/**
 * @brief Execute an ELF binary in userspace.
 * @param data Pointer to ELF file data.
 * @param size Size of ELF file.
 * @return Exit code from user program.
 */
u64 user_exec_elf(const void *data, u64 size);

#endif
