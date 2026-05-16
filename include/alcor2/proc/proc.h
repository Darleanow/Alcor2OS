/**
 * @file include/alcor2/proc/proc.h
 * @brief Process management and scheduling.
 *
 * Each process has its own address space, kernel stack, and user context.
 * Supports fork, exec, wait, and exit primitives.
 */

#ifndef ALCOR2_PROC_H
#define ALCOR2_PROC_H

#include <alcor2/fs/vfs.h>
#include <alcor2/ktermios.h>
#include <alcor2/proc/signal.h>
#include <alcor2/sys/syscall.h>
#include <alcor2/types.h>

/** @brief Maximum number of processes. */
#define PROC_MAX 64

/** @brief Max argv entries for execve / ELF stack build (clang → cc1 needs
 * many). */
#define PROC_MAX_ARGV 128

/** @brief Max length of one argv string / exec path snapshot (bytes). */
#define PROC_MAX_ARG_STRLEN 384

/** @brief Maximum process name length. */
#define PROC_NAME_MAX 32

/** @brief Canonical kbd line edit + ready buffers (stdin discipline). */
#define PROC_KBD_LINE_CAP 256

/** @brief Stored path for @c /proc/self/exe (LLVM, musl @c realpath). */
#define PROC_EXE_PATH_MAX 256

/** @brief Kernel stack size per process. */
#define PROC_KERNEL_STACK (8ULL * 1024)

/** @brief User stack size per process. */
#define PROC_USER_STACK (8ULL * 1024 * 1024)

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
  u64  pid;
  u64  parent_pid;
  char name[PROC_NAME_MAX];
  /** @brief Last successfully executed file (for readlink("/proc/self/exe")).
   */
  char exe_path[PROC_EXE_PATH_MAX];
  /** @brief Current working directory; updated by chdir, inherited on fork. */
  char         cwd[VFS_PATH_MAX];
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
  /** If non-zero, parent is blocked in vfork-style clone until this child execs
   * or exits. */
  u64 vfork_waiting_for;
  u64 waiting_for_pid;
  u64 program_break;
  u64 heap_break;
  u64 mmap_base;

  /** @name Signal state */
  u64           sig_pending;       /**< Bitmask of pending signals */
  u64           sig_mask;          /**< Bitmask of blocked signals */
  k_sigaction_t sig_actions[NSIG]; /**< Per-signal action table */

  /** Emulated TTY attributes (TCGETS/TCSETS on fd 0/1/2 and pipe ends). */
  k_termios_t termios;
  /** @name Keyboard line discipline (ICANON) */
  u32  kbd_edit_len;
  u32  kbd_ready_len;
  char kbd_edit[PROC_KBD_LINE_CAP];
  char kbd_ready[PROC_KBD_LINE_CAP];

  /** @brief Pointer to the saved registers of the currently executing syscall.
   * Required for rt_sigreturn and clone/fork to access user registers. */
  syscall_frame_t *current_frame;

  /** @brief Per-process fd table; each entry is an index into the global
   * open file table, or -1 for closed. Inherited on fork, preserved across
   * exec, released on exit. */
  i32 fds[VFS_MAX_FD];
  /** @brief Per-fd close-on-exec flags. 1 = fd is closed on execve, 0 = not.
   * This is a per-descriptor attribute (not per open-file-description), so
   * dup/dup2 always clears it on the new fd. */
  u8 fd_cloexec[VFS_MAX_FD];
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
 * @brief Index of @p in the kernel process table (0 .. PROC_MAX-1), or -1 if
 *        @p is NULL or not a current table slot.
 *
 * Used for per-slot counters (e.g. syscall preemption) — do not use PID as an
 * array index.
 */
int proc_table_index(const proc_t *p);

/**
 * @brief Create a process from an ELF image held in kernel memory.
 */
u64 proc_create_mem(
    const char *name, const void *elf_data, u64 elf_size, char *const argv[],
    char *const envp[]
);

/**
 * @brief Create a process by loading an ELF from an already-open VFS @p elf_fd.
 *
 * Uses @c vfs_seek / @c vfs_read only (via @c elf_load_fd). Never calls
 * @c vfs_close on @p elf_fd — the caller keeps the fd slot until it closes it.
 *
 * Note: @c proc_exec_replace_image also leaves @p elf_fd open; @c sys_execve
 * closes its temporary @c vfs_open fd after a successful replace. Closing is
 * syscall policy, not part of @c proc_setup_image.
 */
u64 proc_create_fd(
    const char *name, i64 elf_fd, char *const argv[], char *const envp[]
);

/**
 * @brief Timer IRQ hook: request reschedule at next syscall return.
 */
void proc_tick(void);

/**
 * @brief Syscall exit hook: run scheduler if @ref proc_tick flagged preemption.
 */
void proc_check_resched(void);

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
 * @return Child PID on success, 0 if WNOHANG and no child ready, negative on
 * error.
 */
i64 proc_waitpid(i64 pid, i32 *status, i32 options);

/**
 * @brief Fork the current process.
 * @param syscall_frame Saved syscall frame.
 * @return Child PID in parent, 0 in child, negative on error.
 */
i64 proc_fork(const void *syscall_frame);

/**
 * @brief Clone the task like fork, but the
 * child resumes with user RSP @p child_stack when non-zero (musl `posix_spawn`,
 * `faccessat` helper); when zero, same as fork (parent RSP).
 */
i64 proc_clone(const syscall_frame_t *frame, u64 child_stack, u32 clone_flags);

/**
 * @brief Wake a vfork-blocked parent after exec succeeds and cloexec fds are
 *        closed. Call from sys_execve AFTER vfs_proc_close_cloexec_fds().
 */
void proc_notify_exec(const proc_t *p);

/**
 * @brief POSIX-style exec: replace @p p's user image with the ELF read from
 * @p elf_fd. Tears down the current user mappings, loads the new ELF, and
 * builds a fresh user stack with @p argv.
 *
 * On success @p p->user_rip / @p p->user_rsp point at the new entry; the
 * caller is responsible for updating its in-flight syscall frame so the
 * sysret return path lands in the new image. On catastrophic failure the
 * old image has already been destroyed and the process can no longer
 * continue — the caller should @c proc_exit.
 *
 * @return 0 on success, negative -errno on failure.
 */
i64 proc_exec_replace_image(
    proc_t *p, const char *name, i64 elf_fd, char *const argv[],
    char *const envp[]
);

/** After XCR0 setup: snapshot a clean FPU/SSE state (see cpu_enable_sse). */
void proc_capture_default_fpu(void);

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
void proc_start_first(
    const void *elf_data, u64 elf_size, const char *name, const char *exe_path
);

/**
 * @brief Entry point for newly created processes (defined in proc.asm).
 */
extern void proc_enter_first_time(void);

#endif
