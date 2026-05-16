/**
 * @file src/kernel/sched.c
 * @brief Round-robin preemptive scheduler.
 */

#include <alcor2/arch/cpu.h>
#include <alcor2/drivers/console.h>
#include <alcor2/kstdlib.h>
#include <alcor2/mm/heap.h>
#include <alcor2/proc/sched.h>

/** @brief Circular doubly-linked task list. */
static task_t *task_list = NULL;
/** @brief Currently running task. */
static task_t *current_task = NULL;
/** @brief Idle task (never removed). */
static task_t *idle_task = NULL;

static u64     next_tid         = 1;
static u64     task_count_val   = 0;
static u64     context_switches = 0;

/** @brief Time slice in ticks (100ms at 100Hz). */
static const u64 DEFAULT_TIME_SLICE = 10;

/**
 * @brief Add task to circular list.
 * @param task Task to add.
 */
static void task_list_add(task_t *task)
{
  if(task_list == NULL) {
    task->next = task;
    task->prev = task;
    task_list  = task;
  } else {
    task->next            = task_list;
    task->prev            = task_list->prev;
    task_list->prev->next = task;
    task_list->prev       = task;
  }
  task_count_val++;
}

/**
 * @brief Remove a task from the circular list.
 * @param task Task to remove from the list.
 */
static void task_list_remove(task_t *task)
{
  if(task->next == task) {
    /* Last task in list */
    task_list = NULL;
  } else {
    task->prev->next = task->next;
    task->next->prev = task->prev;
    if(task_list == task) {
      task_list = task->next;
    }
  }
  task_count_val--;
}

/**
 * @brief Find the next ready task in round-robin order.
 *
 * Searches the circular task list starting from current_task->next.
 * Returns the idle task if no ready task is found.
 *
 * @return Pointer to next ready task or idle task.
 */
static task_t *find_next_ready(void)
{
  if(current_task == NULL) {
    return idle_task;
  }

  task_t *start = current_task->next;
  task_t *t     = start;

  do {
    if(t->state == TASK_STATE_READY) {
      return t;
    }
    t = t->next;
  } while(t != start);

  /* No ready task found, return idle */
  return idle_task;
}

/**
 * @brief Initialize the scheduler.
 *
 * Creates the idle task from the current boot context.
 */
void sched_init(void)
{
  /* Create idle task from current context */
  idle_task = kzalloc(sizeof(task_t));
  if(idle_task == NULL) {
    console_print("[SCHED] Failed to allocate idle task!\n");
    return;
  }

  idle_task->tid = next_tid++;
  kstrncpy(idle_task->name, "idle", TASK_NAME_MAX);
  idle_task->state           = TASK_STATE_RUNNING;
  idle_task->time_slice      = DEFAULT_TIME_SLICE;
  idle_task->ticks_remaining = DEFAULT_TIME_SLICE;
  idle_task->stack_base      = NULL; /* Uses boot stack */
  idle_task->stack_top       = NULL;
  idle_task->context         = NULL;

  task_list_add(idle_task);
  current_task = idle_task;

  console_print("[SCHED] Initialized\n");
}

/**
 * @brief Voluntarily yield the CPU to the next ready task.
 */
void sched_yield(void)
{
  cpu_disable_interrupts();

  if(current_task == NULL) {
    cpu_enable_interrupts();
    return;
  }

  /* Mark current as ready (if still running) */
  if(current_task->state == TASK_STATE_RUNNING) {
    current_task->state = TASK_STATE_READY;
  }

  /* Find next task */
  task_t *next = find_next_ready();

  if(next == current_task) {
    current_task->state = TASK_STATE_RUNNING;
    cpu_enable_interrupts();
    return;
  }

  /* Switch */
  task_t *prev                  = current_task;
  current_task                  = next;
  current_task->state           = TASK_STATE_RUNNING;
  current_task->ticks_remaining = current_task->time_slice;
  context_switches++;

  context_switch(&prev->context, current_task->context);

  cpu_enable_interrupts();
}

/**
 * @brief Flag indicating that a reschedule is needed.
 *
 * Set by sched_tick() when time slice expires, checked at safe points.
 * Similar to Linux's TIF_NEED_RESCHED.
 */
static volatile bool need_resched = false;

/**
 * @brief Timer tick handler for preemptive scheduling.
 *
 * Called by the PIT IRQ handler. Decrements the current task's remaining
 * time slice and sets need_resched flag when it expires.
 */
void sched_tick(void)
{
  if(current_task == NULL) {
    return;
  }

  if(current_task->ticks_remaining > 0) {
    current_task->ticks_remaining--;
  }

  /* Time slice expired - mark for reschedule (don't switch in IRQ context!) */
  if(current_task->ticks_remaining == 0) {
    need_resched = true;
  }
}

/**
 * @brief Check if reschedule is needed and perform it.
 *
 * Called at safe points such as syscall return to ensure preemptive
 * scheduling happens outside interrupt context.
 */
void sched_check_resched(void)
{
  if(need_resched) {
    need_resched = false;
    sched_yield();
  }
}

/**
 * @brief Terminate the current task.
 *
 * Marks the task as TASK_STATE_ZOMBIE, removes it from the task list,
 * and switches to the next ready task. Never returns.
 */
void task_exit(void)
{
  cpu_disable_interrupts();

  if(current_task == idle_task) {
    /* Idle task cannot exit */
    cpu_enable_interrupts();
    return;
  }

  current_task->state = TASK_STATE_ZOMBIE;

  /* Remove from list and free resources */
  task_list_remove(current_task);

  /* Switch to next task */
  current_task        = find_next_ready();
  current_task->state = TASK_STATE_RUNNING;
  context_switches++;

  /* Switch to new task (never returns for dead task) */
  cpu_context_t *dummy = NULL;
  context_switch(&dummy, current_task->context);

  /* Unreachable */
  for(;;) {
    cpu_halt();
  }
}

/**
 * @brief Get the currently running task.
 * @return Pointer to current task, or NULL if none.
 */
task_t *sched_current(void)
{
  return current_task;
}

/**
 * @brief Block the current task until unblocked.
 *
 * Switches to the next ready task. The blocked task will not be
 * scheduled until sched_unblock() is called on it.
 */
void sched_block(void)
{
  cpu_disable_interrupts();

  if(current_task == NULL || current_task == idle_task) {
    cpu_enable_interrupts();
    return;
  }

  /* Mark as blocked */
  current_task->state = TASK_STATE_BLOCKED;

  /* Find next ready task */
  task_t *next = find_next_ready();

  /* Switch to it */
  task_t *prev                  = current_task;
  current_task                  = next;
  current_task->state           = TASK_STATE_RUNNING;
  current_task->ticks_remaining = current_task->time_slice;
  context_switches++;

  context_switch(&prev->context, current_task->context);

  cpu_enable_interrupts();
}

/**
 * @brief Unblock a previously blocked task.
 *
 * @param task Task to unblock.
 */
void sched_unblock(task_t *task)
{
  if(task == NULL)
    return;

  /* Safe to call from IRQ context - just set state */
  if(task->state == TASK_STATE_BLOCKED) {
    task->state = TASK_STATE_READY;
  }
}
