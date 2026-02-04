/**
 * @file include/alcor2/sched.h
 * @brief Kernel task scheduler (round-robin).
 *
 * Simple preemptive scheduler for kernel-mode tasks with cooperative yielding.
 */

#ifndef ALCOR2_SCHED_H
#define ALCOR2_SCHED_H

#include <alcor2/types.h>

/** @brief Maximum task name length. */
#define TASK_NAME_MAX 32

/** @brief Stack size per task. */
#define TASK_STACK_SIZE (16ULL * 1024)

/** @brief Maximum number of tasks. */
#define TASK_MAX_COUNT 64

/**
 * @brief Task states.
 */
typedef enum
{
  TASK_STATE_READY,
  TASK_STATE_RUNNING,
  TASK_STATE_BLOCKED,
  TASK_STATE_ZOMBIE
} task_state_t;

/**
 * @brief Saved CPU context (callee-saved registers per System V ABI).
 */
typedef struct
{
  u64 r15;
  u64 r14;
  u64 r13;
  u64 r12;
  u64 rbx;
  u64 rbp;
  u64 rip;
} PACKED cpu_context_t;

/**
 * @brief Task Control Block.
 */
typedef struct task
{
  u64          tid;
  char         name[TASK_NAME_MAX];
  task_state_t state;
  u64          time_slice;
  u64          ticks_remaining;
  void       (*entry)(void *);
  void        *arg;
  void        *stack_base;
  void        *stack_top;
  cpu_context_t *context;
  struct task *next;
  struct task *prev;
} task_t;

/**
 * @brief Task entry point function type.
 * @param arg Argument passed to the task.
 */
typedef void (*task_entry_t)(void *arg);

/**
 * @brief Initialize the scheduler.
 */
void sched_init(void);

/**
 * @brief Create a new kernel task.
 * @param name Task name (for debugging).
 * @param entry Entry point function.
 * @param arg Argument passed to entry function.
 * @return Task ID on success, 0 on failure.
 */
u64 task_create(const char *name, task_entry_t entry, void *arg);

/**
 * @brief Yield CPU to next ready task.
 */
void sched_yield(void);

/**
 * @brief Check if reschedule is needed and perform it.
 */
void sched_check_resched(void);

/**
 * @brief Called by timer IRQ to perform preemptive scheduling.
 */
void sched_tick(void);

/**
 * @brief Terminate the current task.
 */
void task_exit(void);

/**
 * @brief Get the current running task.
 * @return Pointer to current task or NULL.
 */
task_t *sched_current(void);

/**
 * @brief Get scheduler statistics.
 * @param task_count Output: number of tasks.
 * @param switches Output: number of context switches.
 */
void sched_stats(u64 *task_count, u64 *switches);

/**
 * @brief Switch CPU context from one task to another.
 * @param old_ctx Pointer to save current context.
 * @param new_ctx Context to load.
 */
extern void context_switch(cpu_context_t **old_ctx, cpu_context_t *new_ctx);

#endif
