/**
 * @file include/alcor2/proc/sched.h
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
  u64            tid;
  char           name[TASK_NAME_MAX];
  task_state_t   state;
  u64            time_slice;
  u64            ticks_remaining;
  void          *stack_base;
  void          *stack_top;
  cpu_context_t *context;
  struct task   *next;
  struct task   *prev;
} task_t;

/**
 * @brief Initialize the scheduler.
 */
void sched_init(void);

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
 * @brief Block the current task and yield to another.
 *
 * Sets current task state to BLOCKED and switches to next ready task.
 * Task will not run until unblocked with sched_unblock().
 */
void sched_block(void);

/**
 * @brief Unblock a blocked task.
 *
 * Sets task state to READY so it can be scheduled again.
 * Safe to call from interrupt context.
 *
 * @param task Task to unblock.
 */
void sched_unblock(task_t *task);

/**
 * @brief Switch CPU context from one task to another.
 * @param old_ctx Pointer to save current context.
 * @param new_ctx Context to load.
 */
extern void context_switch(cpu_context_t **old_ctx, cpu_context_t *new_ctx);

#endif
