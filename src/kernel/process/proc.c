/**
 * @file src/kernel/proc.c
 * @brief Process management with per-process kernel stacks.
 */

#include <alcor2/console.h>
#include <alcor2/cpu.h>
#include <alcor2/elf.h>
#include <alcor2/errno.h>
#include <alcor2/gdt.h>
#include <alcor2/heap.h>
#include <alcor2/memory_layout.h>
#include <alcor2/pmm.h>
#include <alcor2/proc.h>
#include <alcor2/syscall.h>
#include <alcor2/vmm.h>
#include <alcor2/vfs.h>
#include <alcor2/kstdlib.h>

static proc_t  proc_table[PROC_MAX];
static proc_t *current_proc = NULL;
static u64     next_pid     = 1;

/** @brief Current kernel stack for syscall entry. */
u64 current_kernel_rsp = 0;
/** @brief Current process CR3 for address space switching. */
u64 current_proc_cr3 = 0;

/**
 * @brief Initialize the process subsystem.
 *
 * Clears the process table and sets all process slots to FREE state.
 */
void proc_init(void)
{
  /* Clear process table */
  for(int i = 0; i < PROC_MAX; i++) {
    proc_table[i].state = PROC_STATE_FREE;
    proc_table[i].pid   = 0;
  }

  console_print("[PROC] Process subsystem initialized\n");
}

/**
 * @brief Get the currently running process.
 * @return Pointer to current process, or NULL if none.
 */
proc_t *proc_current(void)
{
  return current_proc;
}

/**
 * @brief Get process by PID.
 * @param pid Process ID to find.
 * @return Pointer to process, or NULL if not found.
 */
proc_t *proc_get(u64 pid)
{
  for(int i = 0; i < PROC_MAX; i++) {
    if(proc_table[i].pid == pid && proc_table[i].state != PROC_STATE_FREE) {
      return &proc_table[i];
    }
  }
  return NULL;
}

/**
 * @brief Allocate a free process slot from the process table.
 * @return Pointer to free process slot, or NULL if table is full.
 */
static proc_t *proc_alloc(void)
{
  for(int i = 0; i < PROC_MAX; i++) {
    if(proc_table[i].state == PROC_STATE_FREE) {
      return &proc_table[i];
    }
  }
  return NULL;
}

/**
 * @brief Push a string onto user stack and return the new stack pointer.
 *
 * Must be called while in the process's address space.
 * The string is copied with null terminator and the stack pointer is aligned to
 * 8 bytes.
 *
 * @param sp Current stack pointer.
 * @param str String to push (null-terminated).
 * @return New stack pointer after pushing the string.
 */
static u64 push_string(u64 sp, const char *str)
{
  u64 len = 0;
  while(str[len])
    len++;
  len++; /* Include null terminator */

  sp -= len;
  sp &= ALIGN_8_MASK; /* Align to 8 bytes */

  char *dst = (char *)sp;
  for(u64 i = 0; i < len; i++) {
    dst[i] = str[i];
  }

  return sp;
}

u64 proc_create(
    const char *name, void *elf_data, u64 elf_size, char *const argv[]
)
{
  proc_t *p = proc_alloc();
  if(!p) {
    console_print("[PROC] No free process slots\n");
    return 0;
  }

  /* Create new address space for this process */
  p->cr3 = vmm_create_address_space();
  if(!p->cr3) {
    console_print("[PROC] Failed to create address space\n");
    return 0;
  }

  /* Allocate kernel stack */
  p->kernel_stack = kmalloc(PROC_KERNEL_STACK);
  if(!p->kernel_stack) {
    console_print("[PROC] Failed to allocate kernel stack\n");
    return 0;
  }
  p->kernel_stack_top = (void *)((u64)p->kernel_stack + PROC_KERNEL_STACK);

  /* Allocate user stack */
  /* Allocate one extra page to cover the stack_top address itself */
  u64   stack_pages     = (PROC_USER_STACK / 4096) + 1;
  void *user_stack_phys = pmm_alloc_pages(stack_pages);
  if(!user_stack_phys) {
    kfree(p->kernel_stack);
    vmm_destroy_user_mappings(p->cr3);
    console_print("[PROC] Failed to allocate user stack\n");
    return 0;
  }

  /* User stack at fixed address (each process has own address space now) */
  u64 user_stack_base = USER_STACK_BASE;
  u64 stack_top       = USER_STACK_TOP;

  /* Map user stack in process's address space (including the page at stack_top)
   */
  for(u64 off = 0; off < stack_pages * 4096; off += 4096) {
    vmm_map_in(
        p->cr3, user_stack_base + off, (u64)user_stack_phys + off,
        VMM_PRESENT | VMM_WRITE | VMM_USER
    );
  }
  p->user_stack     = (void *)user_stack_base;
  p->user_stack_top = (void *)stack_top;

  /* Load ELF into process's address space */
  /* Temporarily switch to process's address space to load ELF */
  u64 old_cr3 = vmm_get_current_pml4();
  vmm_switch(p->cr3);

  elf_info_t elf_info;
  int        elf_result = elf_load(elf_data, elf_size, &elf_info);

  if(elf_result != 0) {
    vmm_switch(old_cr3);
    kfree(p->kernel_stack);
    vmm_destroy_user_mappings(p->cr3);
    console_print("[PROC] Failed to load ELF\n");
    return 0;
  }

  /**
   * Setup argc/argv on user stack according to System V x86-64 ABI.
   *
   * Stack layout from high to low addresses:
   * - argv strings (null-terminated)
   * - padding for alignment
   * - NULL (end of argv array)
   * - argv[argc-1]
   * - ...
   * - argv[0]
   * - argc
   */

  u64 sp = stack_top;

  /* Count arguments */
  int argc = 0;
  if(argv) {
    while(argv[argc])
      argc++;
  }

  /* If no argv provided, create one with just the program name */
  if(argc == 0) {
    /* Push program name string */
    sp            = push_string(sp, name);
    u64 argv0_ptr = sp;

    /* Align to 16 bytes */
    sp &= ~0xFULL;

    /* Push NULL terminator */
    sp -= 8;
    *(u64 *)sp = 0;

    /* Push argv[0] pointer */
    sp -= 8;
    *(u64 *)sp = argv0_ptr;

    /* Push argc */
    sp -= 8;
    *(u64 *)sp = 1;
  } else {
    /* Push all argument strings and save their addresses */
    u64 arg_ptrs[32]; /* Max 32 args */
    for(int i = argc - 1; i >= 0; i--) {
      sp          = push_string(sp, argv[i]);
      arg_ptrs[i] = sp;
    }

    /* Align to 16 bytes for the pointer array */
    sp &= ALIGN_16_MASK;

    /* Adjust for even/odd argc to maintain 16-byte alignment after argc push */
    if((argc + 1) % 2 != 0) {
      sp -= 8; /* Padding */
    }

    /* Push NULL terminator for argv */
    sp -= 8;
    *(u64 *)sp = 0;

    /* Push argv pointers in reverse order */
    for(int i = argc - 1; i >= 0; i--) {
      sp -= 8;
      *(u64 *)sp = arg_ptrs[i];
    }

    /* Push argc */
    sp -= 8;
    *(u64 *)sp = (u64)argc;
  }

  /* Switch back to kernel/current address space */
  vmm_switch(old_cr3);

  /* Initialize process */
  p->pid        = next_pid++;
  p->parent_pid = current_proc ? current_proc->pid : 0;
  kstrncpy(p->name, name, PROC_NAME_MAX);
  p->state           = PROC_STATE_READY;
  p->exit_code       = 0;
  p->waiting_for_pid = 0;
  p->fs_base         = 0;

  /* Initialize per-process memory regions based on ELF layout
   * program_break starts at end of loaded ELF segments (page-aligned)
   * heap_break (for mmap) starts higher to avoid conflicts */
  u64 elf_end_aligned = (elf_info.end + 0xFFF) & ALIGN_16_MASK;
  p->program_break    = elf_end_aligned;
  p->heap_break       = USER_HEAP_START;

  /* Set user context - stack now contains argc/argv */
  p->user_rip    = elf_info.entry;
  p->user_rsp    = sp;
  p->user_rflags = 0x202; /* IF enabled */

  /* Setup initial kernel stack for first switch */
  /* When we switch to this process, we'll iret to userspace */
  u64 *ksp = (u64 *)p->kernel_stack_top;

  /* Build iretq frame on kernel stack */
  *(--ksp) = 0x3B;           /* SS (user data | RPL 3) */
  *(--ksp) = p->user_rsp;    /* RSP */
  *(--ksp) = p->user_rflags; /* RFLAGS */
  *(--ksp) = 0x43;           /* CS (user code | RPL 3) */
  *(--ksp) = p->user_rip;    /* RIP */

  /* Return address for after context_switch pops and rets */
  *(--ksp) = (u64)proc_enter_first_time;

  /* Callee-saved registers for context_switch (order: rbp, rbx, r12, r13, r14,
   * r15) */
  /* context_switch pops: r15, r14, r13, r12, rbx, rbp then ret */
  *(--ksp) = 0; /* rbp - popped last */
  *(--ksp) = 0; /* rbx */
  *(--ksp) = 0; /* r12 */
  *(--ksp) = 0; /* r13 */
  *(--ksp) = 0; /* r14 */
  *(--ksp) = 0; /* r15 - popped first */

  p->saved_rsp = (u64)ksp;

  /*console_printf(
      "[PROC] Created process '%s' (pid=%d, entry=0x%x)\n", name, (int)p->pid,
      (int)elf_info.entry
  );*/

  return p->pid;
}

/**
 * @brief External assembly entry point for new processes.
 *
 * Defined in proc.asm. This function performs the initial iretq to enter user
 * mode for newly created processes.
 */
extern void proc_enter_first_time(void);

/**
 * @brief Exit the current process with the given exit code.
 *
 * Marks the process as PROC_STATE_ZOMBIE, wakes up the parent if waiting,
 * and schedules another process. Never returns.
 *
 * @param code Exit code (typically 0 for success, non-zero for error).
 */
void proc_exit(i64 code)
{
  proc_t *p = current_proc;
  if(!p) {
    console_print("[PROC] No current process to exit!\n");
    for(;;)
      cpu_halt();
  }

  console_printf(
      "[PROC] Process %d exited with code %d\n", (int)p->pid, (int)code
  );

  p->exit_code = code;
  p->state     = PROC_STATE_ZOMBIE;
  
  /* Clean up open file descriptors */
  vfs_close_for_pid(p->pid);

  /* Wake up parent if it's waiting */
  proc_t *parent = proc_get(p->parent_pid);
  if(parent && parent->state == PROC_STATE_BLOCKED) {
    /* Parent is waiting for this specific child OR any child */
    if(parent->waiting_for_pid == p->pid || parent->waiting_for_pid == 0) {
      parent->state = PROC_STATE_READY;
    }
  }

  /* Schedule another process */
  proc_schedule();

  /* Should never reach here */
  for(;;)
    cpu_halt();
}

/**
 * @brief Wait for a specific child process to exit.
 *
 * If the child is already a zombie, immediately returns its exit code.
 * Otherwise blocks the current process until the child exits.
 *
 * @param pid Child process ID to wait for.
 * @return Child's exit code on success, -1 on error.
 */
i64 proc_wait(u64 pid)
{
  proc_t *child = proc_get(pid);
  if(!child) {
    return -1;
  }

  /* If child is already zombie, get exit code and free */
  if(child->state == PROC_STATE_ZOMBIE) {
    i64 code     = child->exit_code;
    child->state = PROC_STATE_FREE;
    vmm_destroy_user_mappings(child->cr3);
    kfree(child->kernel_stack);
    return code;
  }

  /* Block until child exits */
  current_proc->state           = PROC_STATE_BLOCKED;
  current_proc->waiting_for_pid = pid;

  proc_schedule();

  /* Woken up, child should be zombie now */
  child = proc_get(pid);
  if(child && child->state == PROC_STATE_ZOMBIE) {
    i64 code     = child->exit_code;
    child->state = PROC_STATE_FREE;
    vmm_destroy_user_mappings(child->cr3);
    kfree(child->kernel_stack);
    return code;
  }

  return -1;
}

/**
 * @brief Schedule the next ready process to run.
 *
 * Performs simple round-robin scheduling. Searches for the next
 * PROC_STATE_READY process starting from the current process. If no ready
 * process is found, halts the CPU.
 */
void proc_schedule(void)
{
  cpu_disable_interrupts();

  /* Find next ready process */
  proc_t *next = NULL;

  /* Simple round-robin: start from current+1 */
  int start = current_proc ? (int)(current_proc - proc_table) : 0;

  for(int i = 1; i <= PROC_MAX; i++) {
    int idx = (start + i) % PROC_MAX;
    if(proc_table[idx].state == PROC_STATE_READY) {
      next = &proc_table[idx];
      break;
    }
  }

  if(!next) {
    /* No ready process, halt */
    console_print("[PROC] No runnable process, halting\n");
    for(;;) {
      cpu_enable_interrupts();
      cpu_halt();
    }
  }

  proc_switch(next);
}

/**
 * @brief Low-level context switch function.
 *
 * Defined in context.asm. Saves the current stack pointer to *old_rsp,
 * loads new_rsp into RSP, and performs the context switch.
 *
 * @param old_rsp Pointer to save current RSP (can be NULL for initial switch).
 * @param new_rsp New stack pointer to load.
 */
extern void context_switch(u64 *old_rsp, u64 new_rsp);

/**
 * @brief Switch to a different process.
 *
 * Updates process states, switches address spaces (CR3), updates TSS kernel
 * stack, restores TLS (FS base), and performs the context switch.
 *
 * @param next Process to switch to.
 */
void proc_switch(proc_t *next)
{
  if(next == current_proc) {
    cpu_enable_interrupts();
    return;
  }

  proc_t *prev = current_proc;

  /* Save current FS base (TLS) before switching */
  if(prev) {
    prev->fs_base = cpu_get_fs_base();
  }

  /* Update states */
  if(prev && prev->state == PROC_STATE_RUNNING) {
    prev->state = PROC_STATE_READY;
  }
  next->state  = PROC_STATE_RUNNING;
  current_proc = next;

  /* Set kernel stack for this process */
  tss_set_rsp0((u64)next->kernel_stack_top);
  current_kernel_rsp = (u64)next->kernel_stack_top;

  /* Set CR3 for proc_enter_first_time (used for new processes) */
  current_proc_cr3 = next->cr3;

  /* Switch address space */
  vmm_switch(next->cr3);

  /* Restore FS base (TLS) for new process */
  if(next->fs_base) {
    cpu_set_fs_base(next->fs_base);
  }

  /* Context switch */
  if(prev) {
    context_switch(&prev->saved_rsp, next->saved_rsp);
  } else {
    /* First switch, just load new context - matches context_switch pop order */
    __asm__ volatile("mov %0, %%rsp\n"
                     "pop %%r15\n"
                     "pop %%r14\n"
                     "pop %%r13\n"
                     "pop %%r12\n"
                     "pop %%rbx\n"
                     "pop %%rbp\n"
                     "ret\n"
                     :
                     : "r"(next->saved_rsp)
                     : "memory");
  }

  cpu_enable_interrupts();
}

/**
 * @brief Start the first user process from kernel initialization.
 *
 * Creates a process from the given ELF data, switches to its address space,
 * and jumps to user mode. Never returns.
 *
 * @param elf_data Pointer to ELF file data in memory.
 * @param elf_size Size of the ELF file in bytes.
 * @param name Name for the process.
 */
void proc_start_first(void *elf_data, u64 elf_size, const char *name)
{
  u64 pid = proc_create(name, elf_data, elf_size, NULL);
  if(pid == 0) {
    console_print("[PROC] Failed to create first process\n");
    return;
  }

  proc_t *p = proc_get(pid);
  if(!p) {
    return;
  }

  /* Switch to first process */
  p->state     = PROC_STATE_RUNNING;
  current_proc = p;

  /* Set kernel stack for this process */
  tss_set_rsp0((u64)p->kernel_stack_top);
  current_kernel_rsp = (u64)p->kernel_stack_top;

  /* Switch to process's address space */
  vmm_switch(p->cr3);

  console_print("[PROC] Starting first process...\n");

  /* Jump to process (never returns) - matches context_switch pop order */
  __asm__ volatile("mov %0, %%rsp\n"
                   "pop %%r15\n"
                   "pop %%r14\n"
                   "pop %%r13\n"
                   "pop %%r12\n"
                   "pop %%rbx\n"
                   "pop %%rbp\n"
                   "ret\n"
                   :
                   : "r"(p->saved_rsp)
                   : "memory");
}

/**
 * @brief Fork the current process (create a copy).
 *
 * Creates a child process that is a copy of the current process with its own
 * address space (cloned page tables) and kernel stack. The child returns 0,
 * while the parent receives the child's PID.
 *
 * @param frame_ptr Pointer to syscall_frame_t containing parent's saved
 * registers.
 * @return Child PID to parent process, 0 to child process, negative on error.
 */
i64 proc_fork(void *frame_ptr)
{
  syscall_frame_t *frame  = (syscall_frame_t *)frame_ptr;
  proc_t          *parent = current_proc;
  if(!parent) {
    return -1;
  }

  /* Allocate child process slot */
  proc_t *child = proc_alloc();
  if(!child) {
    console_print("[PROC] fork: no free process slot\n");
    return -ENOMEM;
  }

  /* Clone the address space */
  child->cr3 = vmm_clone_address_space(parent->cr3);
  if(!child->cr3) {
    console_print("[PROC] fork: failed to clone address space\n");
    child->state = PROC_STATE_FREE;
    return -ENOMEM;
  }

  /* Allocate kernel stack for child */
  child->kernel_stack = kmalloc(PROC_KERNEL_STACK);
  if(!child->kernel_stack) {
    console_print("[PROC] fork: failed to allocate kernel stack\n");
    vmm_destroy_user_mappings(child->cr3);
    child->state = PROC_STATE_FREE;
    return -ENOMEM;
  }
  child->kernel_stack_top =
      (void *)((u64)child->kernel_stack + PROC_KERNEL_STACK);

  /* Copy user stack info (same virtual addresses, but different physical pages
   * in cloned space) */
  child->user_stack     = parent->user_stack;
  child->user_stack_top = parent->user_stack_top;

  /* Initialize child process */
  child->pid        = next_pid++;
  child->parent_pid = parent->pid;
  kstrncpy(child->name, parent->name, PROC_NAME_MAX);
  child->state           = PROC_STATE_READY;
  child->exit_code       = 0;
  child->waiting_for_pid = 0;
  child->fs_base         = parent->fs_base;

  /* Copy per-process memory state */
  child->program_break = parent->program_break;
  child->heap_break    = parent->heap_break;

  /* Copy user context from syscall frame */
  child->user_rip    = frame->rip;
  child->user_rsp    = frame->rsp;
  child->user_rflags = frame->rflags;

  /* Build kernel stack for child (same as proc_create) */
  u64 *ksp = (u64 *)child->kernel_stack_top;

  /* iretq frame */
  *(--ksp) = 0x3B;               /* SS (user data | RPL 3) */
  *(--ksp) = child->user_rsp;    /* RSP */
  *(--ksp) = child->user_rflags; /* RFLAGS */
  *(--ksp) = 0x43;               /* CS (user code | RPL 3) */
  *(--ksp) = child->user_rip;    /* RIP */

  /* Return address for context_switch */
  extern void proc_enter_first_time(void);
  *(--ksp) = (u64)proc_enter_first_time;

  /* Callee-saved registers (context_switch pops: r15, r14, r13, r12, rbx, rbp)
   */
  *(--ksp) = 0; /* rbp */
  *(--ksp) = 0; /* rbx */
  *(--ksp) = 0; /* r12 */
  *(--ksp) = 0; /* r13 */
  *(--ksp) = 0; /* r14 */
  *(--ksp) = 0; /* r15 */

  child->saved_rsp = (u64)ksp;

  /* Reset ksp and build a different stack layout */
  ksp = (u64 *)child->kernel_stack_top;

  /* Copy parent's syscall frame to child's kernel stack */
  ksp                          = (u64 *)((u64)ksp - sizeof(syscall_frame_t));
  syscall_frame_t *child_frame = (syscall_frame_t *)ksp;

  /* Copy all registers from parent */
  child_frame->r15    = frame->r15;
  child_frame->r14    = frame->r14;
  child_frame->r13    = frame->r13;
  child_frame->r12    = frame->r12;
  child_frame->r11    = frame->r11;
  child_frame->r10    = frame->r10;
  child_frame->r9     = frame->r9;
  child_frame->r8     = frame->r8;
  child_frame->rbp    = frame->rbp;
  child_frame->rdi    = frame->rdi;
  child_frame->rsi    = frame->rsi;
  child_frame->rdx    = frame->rdx;
  child_frame->rcx    = frame->rcx;
  child_frame->rbx    = frame->rbx;
  child_frame->rax    = 0; /* Child returns 0 from fork! */
  child_frame->rip    = frame->rip;
  child_frame->rflags = frame->rflags;
  child_frame->rsp    = frame->rsp;

  /* Now we need the child to return through syscall_return */
  /* proc_fork_child_entry will pop the frame and do sysret */
  extern void proc_fork_child_entry(void);

  /* Return address after context_switch pops callee-saved regs */
  *(--ksp) = (u64)proc_fork_child_entry;

  /* Callee-saved registers */
  *(--ksp) = 0; /* rbp */
  *(--ksp) = 0; /* rbx */
  *(--ksp) = 0; /* r12 */
  *(--ksp) = 0; /* r13 */
  *(--ksp) = 0; /* r14 */
  *(--ksp) = 0; /* r15 */

  child->saved_rsp = (u64)ksp;

  console_printf(
      "[PROC] fork: parent %d -> child %d\n", (int)parent->pid, (int)child->pid
  );

  /* Parent returns child PID */
  return child->pid;
}

/**
 * @brief Entry point for forked child processes.
 *
 * Defined in proc.asm. This function pops the saved syscall frame from the
 * kernel stack and performs sysret to return to user mode with RAX=0.
 */
extern void proc_fork_child_entry(void);

/**
 * @brief Wait for child process(es) to change state.
 *
 * Implements waitpid syscall semantics. Can wait for a specific child (pid >
 * 0), any child (pid == -1), or return immediately if no child is ready
 * (WNOHANG).
 *
 * @param pid Process ID: -1 for any child, >0 for specific child.
 * @param status Pointer to store child's exit status (can be NULL).
 * @param options Wait options (e.g., WNOHANG for non-blocking).
 * @return Child PID on success, 0 if WNOHANG and no child ready, negative on
 * error.
 */
i64 proc_waitpid(i64 pid, i32 *status, i32 options)
{
  proc_t *parent = current_proc;
  if(!parent)
    return -1;

  /* Find a child process */
  proc_t *child = NULL;

  if(pid == -1) {
    /* Wait for any child */
    for(int i = 0; i < PROC_MAX; i++) {
      if(proc_table[i].parent_pid == parent->pid &&
         proc_table[i].state != PROC_STATE_FREE) {
        if(proc_table[i].state == PROC_STATE_ZOMBIE) {
          child = &proc_table[i];
          break;
        }
      }
    }

    /* No zombie child found */
    if(!child) {
      /* Check if we have any children at all */
      int has_children = 0;
      for(int i = 0; i < PROC_MAX; i++) {
        if(proc_table[i].parent_pid == parent->pid &&
           proc_table[i].state != PROC_STATE_FREE) {
          has_children = 1;
          break;
        }
      }

      if(!has_children) {
        return -ECHILD;
      }

      if(options & WNOHANG) {
        return 0; /* No child ready yet */
      }

      /* Block until a child exits */
      parent->state           = PROC_STATE_BLOCKED;
      parent->waiting_for_pid = 0; /* Any child */
      proc_schedule();

      /* Woken up - find zombie child */
      for(int i = 0; i < PROC_MAX; i++) {
        if(proc_table[i].parent_pid == parent->pid &&
           proc_table[i].state == PROC_STATE_ZOMBIE) {
          child = &proc_table[i];
          break;
        }
      }
    }
  } else if(pid > 0) {
    /* Wait for specific child */
    child = proc_get(pid);
    if(!child || child->parent_pid != parent->pid) {
      return -ECHILD;
    }

    if(child->state != PROC_STATE_ZOMBIE) {
      if(options & WNOHANG) {
        return 0;
      }

      parent->state           = PROC_STATE_BLOCKED;
      parent->waiting_for_pid = pid;
      proc_schedule();

      child = proc_get(pid);
    }
  } else {
    /* pid == 0 or pid < -1: wait for process group (not implemented) */
    return -EINVAL;
  }

  if(!child || child->state != PROC_STATE_ZOMBIE) {
    return -ECHILD;
  }

  /* Get exit status */
  i64 child_pid = child->pid;
  if(status) {
    /* Linux status format: exit_code << 8 for normal exit */
    *status = (i32)((child->exit_code & 0xFF) << 8);
  }

  /* Free child */
  child->state = PROC_STATE_FREE;
  kfree(child->kernel_stack);
  vmm_destroy_user_mappings(child->cr3);

  return child_pid;
}
