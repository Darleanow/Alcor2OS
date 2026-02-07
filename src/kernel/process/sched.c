/**
 * @file src/kernel/sched.c
 * @brief Round-robin preemptive scheduler.
 */

#include <alcor2/console.h>
#include <alcor2/cpu.h>
#include <alcor2/heap.h>
#include <alcor2/kstdlib.h>
#include <alcor2/sched.h>

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
 * @brief Task wrapper function.
 *
 * This is the initial entry point for all tasks. It enables interrupts,
 * calls the task's entry function with its argument, then calls task_exit().
 */
static void task_wrapper(void)
{
  /* Enable interrupts for this task */
  cpu_enable_interrupts();

  /* Get current task and call its entry function */
  task_t *self = sched_current();
  if(self && self->entry) {
    self->entry(self->arg);
  }

  task_exit();
}

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

// cppcheck-suppress unusedFunction
u64 task_create(const char *name, task_entry_t entry, void *arg)
{
  /* Allocate task structure */
  task_t *task = kzalloc(sizeof(task_t));
  if(task == NULL) {
    return 0;
  }

  /* Allocate stack */
  void *stack = kmalloc(TASK_STACK_SIZE);
  if(stack == NULL) {
    kfree(task);
    return 0;
  }

  /* Initialize task */
  task->tid = next_tid++;
  kstrncpy(task->name, name, TASK_NAME_MAX);
  task->state           = TASK_STATE_READY;
  task->time_slice      = DEFAULT_TIME_SLICE;
  task->ticks_remaining = DEFAULT_TIME_SLICE;
  task->entry           = entry;
  task->arg             = arg;
  task->stack_base      = stack;
  task->stack_top       = (void *)((u8 *)stack + TASK_STACK_SIZE);

  /* Setup initial stack frame */
  u64 *sp = (u64 *)task->stack_top;

  /* Setup initial context (what context_switch expects) */
  /* Return address - where task starts executing */
  *(--sp) = (u64)task_wrapper; /* rip */
  *(--sp) = 0;                 /* rbp */
  *(--sp) = 0;                 /* rbx */
  *(--sp) = 0;                 /* r12 */
  *(--sp) = 0;                 /* r13 */
  *(--sp) = 0;                 /* r14 */
  *(--sp) = 0;                 /* r15 */

  task->context = (cpu_context_t *)sp;

  /* Add to task list */
  task_list_add(task);

  console_printf("[SCHED] Task '%s' created (tid=%d)\n", name, (int)task->tid);

  return task->tid;
}

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
// cppcheck-suppress unusedFunction
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
 * @brief Get scheduler statistics.
 * @param count Output pointer for number of tasks (can be NULL).
 * @param switches Output pointer for number of context switches (can be NULL).
 */
// cppcheck-suppress unusedFunction
void sched_stats(u64 *task_count, u64 *switches)
{
  if(task_count)
    *task_count = task_count_val;
  if(switches)
    *switches = context_switches;
}
