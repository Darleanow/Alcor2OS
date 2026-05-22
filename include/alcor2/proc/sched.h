/**
 * @file include/alcor2/proc/sched.h
 * @brief Minimal scheduler API for drivers and low-level kernel code.
 *
 * Drivers must not include proc.h (which pulls in VFS, termios, etc.).
 * This header exposes only what hardware interrupt handlers need.
 */

#ifndef ALCOR2_PROC_SCHED_H
#define ALCOR2_PROC_SCHED_H

/* Forward declaration only — drivers must not dereference proc_t fields. */
typedef struct proc proc_t;

/** @brief Timer tick — request reschedule at next syscall return. */
void proc_tick(void);

/** @brief Run the scheduler; switch to the next ready process. */
void proc_schedule(void);

/** @brief Return the currently running process, or NULL in early boot. */
proc_t *proc_current(void);

/** @brief Block @p p (set state to BLOCKED). Call before proc_schedule(). */
void proc_block(proc_t *p);

/** @brief Wake @p p if it is blocked (set state to READY). Safe from IRQ. */
void proc_wake(proc_t *p);

#endif /* ALCOR2_PROC_SCHED_H */
