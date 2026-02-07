/**
 * @file include/alcor2/proc.h
 * @brief Process management and scheduling.
 *
 * Each process has its own address space, kernel stack, and user context.
 * Supports fork, exec, wait, and exit primitives.
 */

#ifndef ALCOR2_PROC_H
#define ALCOR2_PROC_H

#include <alcor2/types.h>

/** @brief Maximum number of processes. */
#define PROC_MAX 16

/** @brief Maximum process name length. */
#define PROC_NAME_MAX 32

/** @brief Kernel stack size per process. */
#define PROC_KERNEL_STACK (8ULL * 1024)

/** @brief User stack size per process. */
#define PROC_USER_STACK (64 * 1024)

/** @brief waitpid option: return immediately if no child has exited. */
#define WNOHANG 1

/**
 * @brief Process states.
 */
typedef enum
{
  PROC_STATE_FREE = 0,
  PROC_STATE_READY,
  PROC_STATE_RUNNING,
  PROC_STATE_BLOCKED,
  PROC_STATE_ZOMBIE
} proc_state_t;

/**
 * @brief Process Control Block.
 */
typedef struct proc
{
  u64          pid;
  u64          parent_pid;
  char         name[PROC_NAME_MAX];
  proc_state_t state;
  i64          exit_code;
  u64          cr3;
  void        *kernel_stack;
  void        *kernel_stack_top;
  void        *user_stack;
  void        *user_stack_top;
  u64          saved_rsp;
  u64          user_rip;
  u64          user_rsp;
  u64          user_rflags;
  u64          fs_base;
  u64          waiting_for_pid;
  u64          program_break;
  u64          heap_break;
} proc_t;

/**
 * @brief Initialize process subsystem.
 */
void proc_init(void);

/**
 * @brief Get the current running process.
 * @return Pointer to current process or NULL.
 */
proc_t *proc_current(void);

/**
 * @brief Create a new process from ELF data.
 * @param name Process name.
 * @param elf_data Pointer to ELF file data.
 * @param elf_size Size of ELF file.
 * @param argv NULL-terminated array of arguments (argv[0] = program name).
 * @return New process's PID, or 0 on failure.
 */
u64 proc_create(
    const char *name, const void *elf_data, u64 elf_size, char *const argv[]
);

/**
 * @brief Exit the current process with the given exit code.
 * @param code Exit code.
 */
void proc_exit(i64 code) __attribute__((noreturn));

/**
 * @brief Wait for a child process to exit.
 * @param pid Child PID to wait for.
 * @return Child's exit code.
 */
i64 proc_wait(u64 pid);

/**
 * @brief Wait for any child or specific child process.
 * @param pid -1 = any child, >0 = specific child.
 * @param status Pointer to store exit status (can be NULL).
 * @param options WNOHANG etc (0 = block).
 * @return Child PID on success, 0 if WNOHANG and no child ready, negative on error.
 */
i64 proc_waitpid(i64 pid, i32 *status, i32 options);

/**
 * @brief Fork the current process.
 * @param syscall_frame Saved syscall frame.
 * @return Child PID in parent, 0 in child, negative on error.
 */
i64 proc_fork(const void *syscall_frame);

/**
 * @brief Switch to a process (called by scheduler or exec).
 * @param next Process to switch to.
 */
void proc_switch(proc_t *next);

/**
 * @brief Schedule the next process to run.
 */
void proc_schedule(void);

/**
 * @brief Get process by PID.
 * @param pid Process ID.
 * @return Pointer to process or NULL.
 */
proc_t *proc_get(u64 pid);

/**
 * @brief Start the first user process (from kernel main).
 * @param elf_data Pointer to ELF data.
 * @param elf_size Size of ELF data.
 * @param name Process name.
 */
void proc_start_first(const void *elf_data, u64 elf_size, const char *name);

/**
 * @brief Entry point for newly created processes (defined in proc.asm).
 */
extern void proc_enter_first_time(void);

#endif
