/**
 * @file src/kernel/proc.c
 * @brief Process management with per-process kernel stacks.
 */

#include <alcor2/arch/cpu.h>
#include <alcor2/arch/gdt.h>
#include <alcor2/drivers/console.h>
#include <alcor2/errno.h>
#include <alcor2/fs/vfs.h>
#include <alcor2/kstdlib.h>
#include <alcor2/ktermios.h>
#include <alcor2/mm/heap.h>
#include <alcor2/mm/memory_layout.h>
#include <alcor2/mm/pmm.h>
#include <alcor2/mm/vmm.h>
#include <alcor2/proc/elf.h>
#include <alcor2/proc/proc.h>
#include <alcor2/proc/signal.h>
#include <alcor2/sys/syscall.h>

/** @brief POSIX @c clone flag: parent blocks until child @c execve or @c _exit.
 * musl @c posix_spawn relies on this so the parent does not run concurrently
 * with the child in the critical region before exec (avoids pipe sync
 * deadlocks).
 */
#define ALCOR_CLONE_VFORK 0x00004000u

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
 * @brief FXSAVE area filled at boot after FPU init — baseline for future
 *        per-process FPU on fork/switch (see cpu_enable_sse).
 */
static u8 g_default_fpu_state[512] __attribute__((aligned(16)));

void      proc_capture_default_fpu(void)
{
  __asm__ volatile("fxsave (%0)" ::"r"(g_default_fpu_state) : "memory");
}

/**
 * @brief Map a live proc_t* to its fixed slot in proc_table.
 */
int proc_table_index(const proc_t *p)
{
  if(!p)
    return -1;
  for(unsigned i = 0; i < PROC_MAX; i++) {
    if(&proc_table[i] == p)
      return (int)i;
  }
  return -1;
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
      vfs_proc_init_fds(proc_table[i].fds);
      kzero(proc_table[i].fd_cloexec, sizeof(proc_table[i].fd_cloexec));
      kstrncpy(proc_table[i].cwd, "/", 2);
      ktermios_init_default(&proc_table[i].termios);
      proc_table[i].kbd_edit_len  = 0;
      proc_table[i].kbd_ready_len = 0;
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

/**
 * @brief Create a new process from an ELF binary.
 *
 * Allocates a process slot, sets up the address space, loads the ELF,
 * and prepares it for scheduling.
 *
 * @param name     Process name.
 * @param elf_data Pointer to ELF file data.
 * @param elf_size Size of the ELF data in bytes.
 * @param argv     Null-terminated argument array.
 * @return PID of the new process, or 0 on failure.
 */
/* Allocate a user stack and load an ELF into @p p's address space, then
 * build the System V AMD64 startup stack (argc / argv / envp / auxv) and
 * populate p->user_*, p->program_break, p->heap_break, p->mmap_base.
 *
 * Caller invariant: @p p->cr3 is allocated and (for execve) the user-space
 * portion has already been cleared.
 *
 * Returns 0 on success, -errno on failure with no partial state left in @p p
 * other than possibly-mapped stack pages (the caller decides how to recover).
 */
static int proc_setup_image(
    proc_t *p, const char *name, const void *elf_data, u64 elf_size, i64 elf_fd,
    char *const argv[], char *const envp[]
)
{
  u64   stack_pages     = (PROC_USER_STACK / 4096) + 1;
  void *user_stack_phys = pmm_alloc_pages(stack_pages);
  if(!user_stack_phys)
    return -ENOMEM;

  u64 user_stack_base = USER_STACK_BASE;
  u64 stack_top       = USER_STACK_TOP;

  for(u64 off = 0; off < stack_pages * 4096; off += 4096) {
    vmm_map_in(
        p->cr3, user_stack_base + off, (u64)user_stack_phys + off,
        VMM_PRESENT | VMM_WRITE | VMM_USER
    );
  }
  p->user_stack     = (void *)user_stack_base;
  p->user_stack_top = (void *)stack_top;

  u64 old_cr3 = vmm_get_current_pml4();
  vmm_switch(p->cr3);

  elf_info_t elf_info;
  int        elf_result = (elf_fd >= 0) ? elf_load_fd(elf_fd, &elf_info)
                                        : elf_load(elf_data, elf_size, &elf_info);
  if(elf_result != 0) {
    vmm_switch(old_cr3);
    return -ENOEXEC;
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

  /*
   * Build initial user stack per the System V AMD64 ABI.
   *
   * Layout in memory (low address → high address, stack grows downward):
   *
   *   sp → [argc]
   *         [argv[0]] ... [argv[argc-1]] [NULL]   ← argv array
   *         [NULL]                                ← envp array (empty)
   *         [AT_type0][AT_val0] ...               ← auxv pairs
   *         [AT_NULL=0][0]                        ← auxv terminator
   *         <string data>                         ← argument strings
   *         stack_top (highest address)
   *
   * We push from stack_top downward, so the LAST push ends up at the
   * LOWEST address (= sp).  Order of pushes:
   *   1. argv strings, then env strings  (highest)
   *   2. auxv terminator AT_NULL (val then type — struct layout: type@low,
   * val@high)
   *   3. other auxv entries (AT_PHDR last pushed = lowest among auxv)
   *   4. envp NULL + envp pointers (if any)
   *   5. argv NULL terminator
   *   6. argv pointers  (argv[0] last pushed = just above argc)
   *   7. argc           (lowest address = final sp)
   */

#define AT_NULL_V 0
#define AT_PHDR   3
#define AT_PHENT  4
#define AT_PHNUM  5
#define AT_PAGESZ 6
#define AT_ENTRY  9
#define AT_UID    11
#define AT_EUID   12
#define AT_GID    13
#define AT_EGID   14

  /* Each PUSH_AUX stores: val at higher address, type at lower address,
   * so reading as Elf64_auxv_t {u64 type; u64 val} gives the right layout. */
#define PUSH_AUX(type, val)                                                    \
  do {                                                                         \
    sp -= 8;                                                                   \
    *(u64 *)sp = (u64)(val);                                                   \
    sp -= 8;                                                                   \
    *(u64 *)sp = (u64)(type);                                                  \
  } while(0)

  u64 sp = stack_top;

  /* Count arguments */
  int argc = 0;
  if(argv) {
    while(argc < PROC_MAX_ARGV && argv[argc])
      argc++;
  }
  if(argc == 0)
    argc = 1; /* will synthesise argv[0] = name below */

  /* 1. Push argv strings (highest addresses) */
  u64 arg_ptrs[PROC_MAX_ARGV];
  if(argv && argv[0]) {
    for(int i = argc - 1; i >= 0; i--) {
      sp          = push_string(sp, argv[i]);
      arg_ptrs[i] = sp;
    }
  } else {
    sp          = push_string(sp, name);
    arg_ptrs[0] = sp;
  }

  /* 1b. env strings (name=value), below argv strings on the stack */
  int envc = 0;
  u64 env_ptrs[PROC_MAX_ARGV];
  if(envp) {
    while(envc < PROC_MAX_ARGV && envp[envc]) {
      sp             = push_string(sp, envp[envc]);
      env_ptrs[envc] = sp;
      envc++;
    }
  }

  /* Align to 16 bytes before the pointer/auxv area */
  sp &= ALIGN_16_MASK;

  /* 2. auxv terminator AT_NULL (pushed first = highest auxv address) */
  PUSH_AUX(AT_NULL_V, 0);

  /* 3. auxv entries (last pushed = lowest auxv address, first read by musl) */
  PUSH_AUX(AT_EGID, 0);
  PUSH_AUX(AT_GID, 0);
  PUSH_AUX(AT_EUID, 0);
  PUSH_AUX(AT_UID, 0);
  PUSH_AUX(AT_ENTRY, elf_info.entry);
  PUSH_AUX(AT_PAGESZ, 4096);
  if(elf_info.phdr) {
    PUSH_AUX(AT_PHNUM, elf_info.phnum);
    PUSH_AUX(AT_PHENT, elf_info.phent);
    PUSH_AUX(AT_PHDR, elf_info.phdr);
  }

  /* 4. envp pointers + NULL terminator (musl: envp = argv + argc + 1) */
  sp -= 8;
  *(u64 *)sp = 0;
  for(int i = envc - 1; i >= 0; i--) {
    sp -= 8;
    *(u64 *)sp = env_ptrs[i];
  }

  /* 5. argv NULL terminator */
  sp -= 8;
  *(u64 *)sp = 0;

  /* 6. argv pointers (argv[0] pushed last = just above argc) */
  for(int i = argc - 1; i >= 0; i--) {
    sp -= 8;
    *(u64 *)sp = arg_ptrs[i];
  }

  /* 7. argc (last push = lowest address = final sp) */
  sp -= 8;
  *(u64 *)sp = (u64)argc;

#undef PUSH_AUX
#undef AT_NULL_V
#undef AT_PHDR
#undef AT_PHENT
#undef AT_PHNUM
#undef AT_PAGESZ
#undef AT_ENTRY
#undef AT_UID
#undef AT_EUID
#undef AT_GID
#undef AT_EGID

  vmm_switch(old_cr3);

  u64 elf_end_aligned = (elf_info.end + 0xFFF) & ALIGN_16_MASK;
  p->program_break    = elf_end_aligned;
  p->heap_break       = USER_HEAP_START;
  p->mmap_base        = USER_MMAP_BASE;

  p->user_rip    = elf_info.entry;
  p->user_rsp    = sp;
  p->user_rflags = 0x202; /* IF enabled */

  return 0;
}

u64 proc_create(
    const char *name, const void *elf_data, u64 elf_size, i64 elf_fd,
    char *const argv[], char *const envp[]
)
{
  proc_t *p = proc_alloc();
  if(!p) {
    console_print("[PROC] No free process slots\n");
    return 0;
  }

  p->cr3 = vmm_create_address_space();
  if(!p->cr3) {
    console_print("[PROC] Failed to create address space\n");
    return 0;
  }

  p->kernel_stack = kmalloc(PROC_KERNEL_STACK);
  if(!p->kernel_stack) {
    vmm_destroy_user_mappings(p->cr3);
    console_print("[PROC] Failed to allocate kernel stack\n");
    return 0;
  }
  p->kernel_stack_top = (void *)((u64)p->kernel_stack + PROC_KERNEL_STACK);

  if(proc_setup_image(p, name, elf_data, elf_size, elf_fd, argv, envp) < 0) {
    kfree(p->kernel_stack);
    vmm_destroy_user_mappings(p->cr3);
    console_print("[PROC] Failed to load image\n");
    return 0;
  }

  p->pid        = next_pid++;
  p->parent_pid = current_proc ? current_proc->pid : 0;
  kstrncpy(p->name, name, PROC_NAME_MAX);
  p->state             = PROC_STATE_READY;
  p->exit_code         = 0;
  p->waiting_for_pid   = 0;
  p->vfork_waiting_for = 0;
  p->fs_base           = 0;

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

  return p->pid;
}

/**
 * @brief External assembly entry point for new processes.
 *
 * Defined in proc.asm. This function performs the initial iretq to enter user
 * mode for newly created processes.
 */
extern void proc_enter_first_time(void);

static void proc_vfork_wake_parent(const proc_t *child);

i64         proc_exec_replace_image(
            proc_t *p, const char *name, i64 elf_fd, char *const argv[],
            char *const envp[]
        )
{
  /* We are running on @p p (this is its syscall handler), so p->cr3 IS the
   * current cr3. Wipe the user-space portion before loading the new image. */
  vmm_clear_user_mappings(p->cr3);

  int rc = proc_setup_image(p, name, NULL, 0, elf_fd, argv, envp);
  if(rc < 0)
    return rc;

  /* Old TLS base pointed into the previous address space. Mappings are gone,
   * but %fs MSR is not cleared by loading a new ELF — keep proc_t in sync
   * and let context switch install 0 until arch_prctl(SET_FS) runs.
   *
   * execve returns on the same CPU without context_switch, so clear the MSR
   * now; otherwise the next %fs-relative access uses a stale linear address. */
  p->fs_base = 0;
  cpu_set_fs_base(0);

  kstrncpy(p->name, name, PROC_NAME_MAX);
  kstrncpy(p->exe_path, name, PROC_EXE_PATH_MAX);
  p->exe_path[PROC_EXE_PATH_MAX - 1] = '\0';

  /* POSIX execve: reset every caught signal to SIG_DFL. SIG_IGN stays IGN.
   * Without this, a stale handler from the old image points into freed user
   * memory — first signal delivery faults and the new image dies. */
  for(int i = 1; i < NSIG; i++) {
    if(p->sig_actions[i].sa_handler != SIG_IGN) {
      p->sig_actions[i].sa_handler  = SIG_DFL;
      p->sig_actions[i].sa_flags    = 0;
      p->sig_actions[i].sa_mask     = 0;
      p->sig_actions[i].sa_restorer = 0;
    }
  }
  p->kbd_edit_len  = 0;
  p->kbd_ready_len = 0;
  return 0;
}

/** Wake parent blocked in CLONE_VFORK after @p child exec'd or is exiting. */
static void proc_vfork_wake_parent(const proc_t *child)
{
  if(!child)
    return;
  proc_t *parent = proc_get(child->parent_pid);
  if(!parent || parent->vfork_waiting_for != child->pid)
    return;
  parent->vfork_waiting_for = 0;
  if(parent->state == PROC_STATE_BLOCKED)
    parent->state = PROC_STATE_READY;
}

/**
 * @brief Called by sys_execve after exec succeeds and cloexec fds are closed.
 *
 * Wakes a parent that blocked in CLONE_VFORK. Must be called AFTER
 * vfs_proc_close_cloexec_fds() so the parent's errpipe read gets EOF.
 */
void proc_notify_exec(const proc_t *p)
{
  proc_vfork_wake_parent(p);
}

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

  proc_vfork_wake_parent(p);
  p->exit_code = code;
  p->state     = PROC_STATE_ZOMBIE;

  /* Release per-process fd table; OFT entries close when refcount hits 0 */
  vfs_proc_release_fds(p->fds);

  /* Notify parent via SIGCHLD and wake it if blocked in waitpid */
  proc_t *parent = proc_get(p->parent_pid);
  if(parent) {
    proc_signal(p->parent_pid, SIGCHLD);
    if(parent->state == PROC_STATE_BLOCKED &&
       (parent->waiting_for_pid == p->pid || parent->waiting_for_pid == 0)) {
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
    /* If the current process is still runnable (just yielding cooperatively),
     * let it keep running — no context switch needed. */
    if(current_proc && current_proc->state == PROC_STATE_RUNNING) {
      cpu_enable_interrupts();
      return;
    }

    /* All procs blocked: HLT until an IRQ fires, then re-scan for READY.
     * IRQs (timer, keyboard, ATA completion) can flip a process to READY by
     * waking a sleeper. */
    for(;;) {
      cpu_enable_interrupts();
      __asm__ volatile("hlt");
      cpu_disable_interrupts();

      for(int i = 0; i < PROC_MAX; i++) {
        if(proc_table[i].state == PROC_STATE_READY) {
          proc_switch(&proc_table[i]);
          return;
        }
      }
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

  /* Restore FS base (TLS) for new process. Must run even when 0: otherwise
   * the previous task's %fs leaks across switches (fatal after execve). */
  cpu_set_fs_base(next->fs_base);

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
void proc_start_first(
    const void *elf_data, u64 elf_size, const char *name, const char *exe_path
)
{
  u64 pid = proc_create(name, elf_data, elf_size, ELF_FD_NONE, NULL, NULL);
  if(pid == 0) {
    console_print("[PROC] Failed to create first process\n");
    return;
  }

  proc_t *p = proc_get(pid);
  if(!p) {
    return;
  }

  if(exe_path && exe_path[0]) {
    kstrncpy(p->exe_path, exe_path, PROC_EXE_PATH_MAX);
    p->exe_path[PROC_EXE_PATH_MAX - 1] = '\0';
  } else {
    p->exe_path[0] = '\0';
  }

  /* Switch to first process */
  p->state     = PROC_STATE_RUNNING;
  current_proc = p;

  /* Set kernel stack for this process */
  tss_set_rsp0((u64)p->kernel_stack_top);
  current_kernel_rsp = (u64)p->kernel_stack_top;

  /* Switch to process's address space */
  vmm_switch(p->cr3);

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
 * @brief Fork / clone (no threads): duplicate the calling task.
 *
 * If @p child_stack_arg is 0, the child uses the same user RSP as the parent
 * syscall frame (fork). Otherwise the child resumes at @p child_stack_arg
 * (musl `__clone` after syscall).
 *
 * @param frame_ptr Saved syscall frame.
 * @param child_stack_arg User stack for child, or 0 for parent's RSP.
 * @return Child PID to parent, negative errno on failure.
 */
static i64 proc_fork_impl(
    const syscall_frame_t *frame, u64 child_stack_arg, u32 clone_flags
)
{
  u64     child_rsp = child_stack_arg ? child_stack_arg : frame->rsp;
  proc_t *parent    = current_proc;
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
  child->state             = PROC_STATE_READY;
  child->exit_code         = 0;
  child->waiting_for_pid   = 0;
  child->vfork_waiting_for = 0;
  child->fs_base           = parent->fs_base;

  /* Copy per-process memory state */
  child->program_break = parent->program_break;
  child->heap_break    = parent->heap_break;
  child->mmap_base     = parent->mmap_base;

  /* Inherit parent's open file descriptors and per-fd cloexec bits. */
  vfs_proc_inherit_fds(
      child->fds, child->fd_cloexec, parent->fds, parent->fd_cloexec
  );

  kstrncpy(child->cwd, parent->cwd, VFS_PATH_MAX);
  kstrncpy(child->exe_path, parent->exe_path, PROC_EXE_PATH_MAX);
  child->exe_path[PROC_EXE_PATH_MAX - 1] = '\0';

  kmemcpy(&child->termios, &parent->termios, sizeof(child->termios));
  child->kbd_edit_len  = 0;
  child->kbd_ready_len = 0;

  /* Copy user context from syscall frame */
  child->user_rip    = frame->rip;
  child->user_rsp    = child_rsp;
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
  child_frame->rsp    = child_rsp;

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

  if(clone_flags & ALCOR_CLONE_VFORK) {
    parent->vfork_waiting_for = child->pid;
    parent->state             = PROC_STATE_BLOCKED;
    proc_schedule();
  }

  /* Parent returns child PID */
  return (i64)child->pid;
}

i64 proc_fork(const void *syscall_frame)
{
  return proc_fork_impl((const syscall_frame_t *)syscall_frame, 0, 0);
}

i64 proc_clone(const syscall_frame_t *frame, u64 child_stack, u32 clone_flags)
{
  return proc_fork_impl(frame, child_stack, clone_flags);
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

      /* Block until a child exits, looping against spurious wakeups. */
      while(!child) {
        parent->state           = PROC_STATE_BLOCKED;
        parent->waiting_for_pid = 0;
        proc_schedule();

        for(int i = 0; i < PROC_MAX; i++) {
          if(proc_table[i].parent_pid == parent->pid &&
             proc_table[i].state == PROC_STATE_ZOMBIE) {
            child = &proc_table[i];
            break;
          }
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

      /* Loop in case we are woken by a signal before the child zombifies. */
      while(child->state != PROC_STATE_ZOMBIE) {
        parent->state           = PROC_STATE_BLOCKED;
        parent->waiting_for_pid = (u64)pid;
        proc_schedule();
        child = proc_get((u64)pid);
        if(!child || child->parent_pid != parent->pid)
          return -ECHILD;
      }
    }
  } else {
    /* pid == 0 or pid < -1: wait for process group (not implemented) */
    return -EINVAL;
  }

  if(!child || child->state != PROC_STATE_ZOMBIE) {
    return -ECHILD;
  }

  /* Get exit status */
  i64 child_pid = (i64)child->pid;
  if(status) {
    /* POSIX wait status: exit_code << 8 for normal termination */
    *status = (i32)((child->exit_code & 0xFF) << 8);
  }

  /* Free child */
  child->state = PROC_STATE_FREE;
  kfree(child->kernel_stack);
  vmm_destroy_user_mappings(child->cr3);

  return child_pid;
}
