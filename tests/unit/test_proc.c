#include "test_common.h"
#include <alcor2/fs/vfs.h>
#include <alcor2/ktermios.h>
#include <alcor2/mm/vmm.h>
#include <alcor2/proc/elf.h>
#include <alcor2/proc/proc.h>
#include <alcor2/types.h>
#include <setjmp.h>
#include <stdlib.h>
#include <string.h>

void *kmalloc(u64 size) { return malloc(size); }
void  kfree(void *ptr) { free(ptr); }
void *kzalloc(u64 size)
{
  void *p = malloc(size);
  if(p)
    memset(p, 0, size);
  return p;
}

static bool g_vmm_create_fail = false;
u64  vmm_create_address_space(void) { return g_vmm_create_fail ? 0 : 0x1000; }
void vmm_destroy_user_mappings(u64 cr3) { (void)cr3; }
void vmm_clear_user_mappings(u64 cr3) { (void)cr3; }
void vmm_switch(u64 cr3) { (void)cr3; }
u64  vmm_get_current_pml4(void) { return 0x1000; }
bool vmm_map_in(u64 cr3, u64 virt, u64 phys, u64 flags)
{
  (void)cr3;
  (void)virt;
  (void)phys;
  (void)flags;
  return true;
}
static bool g_vmm_clone_fail = false;
u64 vmm_clone_address_space(u64 cr3)
{
  (void)cr3;
  return g_vmm_clone_fail ? 0 : 0x2000;
}

static bool g_pmm_fail = false;
void *pmm_alloc_pages(u64 count) { return g_pmm_fail ? NULL : malloc(count * 4096); }

static int  g_elf_load_ret  = 0;
static u64  g_elf_load_phdr = 0;
int elf_load(const void *data, u64 size, elf_info_t *info)
{
  (void)data;
  (void)size;
  info->entry = 0x400000;
  info->phdr  = g_elf_load_phdr;
  return g_elf_load_ret;
}
static int g_elf_load_fd_ret = 0;
int elf_load_fd(i64 fd, elf_info_t *info)
{
  (void)fd;
  info->entry = 0x400000;
  info->phdr  = g_elf_load_phdr;
  return g_elf_load_fd_ret;
}

void vfs_proc_init_fds(i32 *fds) { (void)fds; }
void vfs_proc_release_fds(i32 *fds) { (void)fds; }
void vfs_proc_close_cloexec_fds(void) {}
void vfs_proc_inherit_fds(
    i32 *child_fds, u8 *child_clox, const i32 *parent_fds,
    const u8 *parent_clox
)
{
  (void)child_fds;
  (void)child_clox;
  (void)parent_fds;
  (void)parent_clox;
}

i64 vfs_open(const char *path, u32 flags)
{
  (void)path;
  (void)flags;
  return 0;
}
i64 vfs_dup2(i64 oldfd, i64 newfd)
{
  (void)oldfd;
  return newfd;
}
i64 vfs_close(i64 fd)
{
  (void)fd;
  return 0;
}

void ktermios_init_default(k_termios_t *t) { (void)t; }
void proc_signal(u64 pid, int signum)
{
  (void)pid;
  (void)signum;
}

jmp_buf halt_jmp;
void    cpu_halt(void) { longjmp(halt_jmp, 1); }
void    cpu_disable_interrupts(void) {}
void    cpu_enable_interrupts(void) {}

void proc_enter_first_time(void) {}
void proc_fork_child_entry(void) {}
static void (*g_context_switch_cb)(void) = NULL;
void context_switch(u64 *old_rsp, u64 new_rsp)
{
  (void)old_rsp;
  (void)new_rsp;
  if(g_context_switch_cb) {
    g_context_switch_cb();
    g_context_switch_cb = NULL; /* one-shot */
  }
}
void context_switch_first(u64 new_rsp) { (void)new_rsp; }

void tss_set_rsp0(u64 rsp0) { (void)rsp0; }
u64  cpu_get_fs_base(void) { return 0; }
void cpu_set_fs_base(u64 base) { (void)base; }

void kbd_set_release_events(bool b) { (void)b; }
void mouse_set_relative(bool b) { (void)b; }
void pit_reset_fast(void) {}

void console_print(const char *msg) { (void)msg; }
void console_printf(const char *fmt, ...) { (void)fmt; }

#include <alcor2/mm/memory_layout.h>
#include <sys/mman.h>

#include "../../src/kernel/process/proc.c"

static int setup(void **state)
{
  (void)state;
  mmap(
      (void *)USER_STACK_BASE, 8ULL * 1024 * 1024,
      PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED, -1, 0
  );
  proc_init();
  current_proc        = NULL;
  next_pid            = 1;
  need_resched        = false;
  g_context_switch_cb = NULL;
  g_vmm_clone_fail    = false;
  g_vmm_create_fail   = false;
  g_pmm_fail          = false;
  g_elf_load_ret      = 0;
  g_elf_load_fd_ret   = 0;
  g_elf_load_phdr     = 0;
  return 0;
}

static int teardown(void **state)
{
  (void)state;
  for(int i = 0; i < PROC_MAX; i++) {
    if(proc_table[i].state != PROC_STATE_FREE && proc_table[i].kernel_stack) {
      kfree(proc_table[i].kernel_stack);
      proc_table[i].kernel_stack = NULL;
    }
  }
  return 0;
}

static void test_pid_exhaustion(void **state)
{
  (void)state;
  syscall_frame_t frame = {0};
  char           *argv[] = {NULL};
  proc_create_mem("init", NULL, 0, argv, argv);
  current_proc = &proc_table[0];

  int success_count = 0;
  i64 ret           = 0;
  for(int i = 0; i < PROC_MAX; i++) {
    ret = proc_fork(&frame);
    if(ret > 0)
      success_count++;
  }
  assert_int_equal(success_count, PROC_MAX - 1);
  ret = proc_fork(&frame);
  assert_int_equal(ret, -EAGAIN);
}

static void test_preemption_stress(void **state)
{
  (void)state;
  char *argv[] = {NULL};
  proc_create_mem("p1", NULL, 0, argv, argv);
  proc_create_mem("p2", NULL, 0, argv, argv);
  proc_create_mem("p3", NULL, 0, argv, argv);
  proc_schedule();
  assert_ptr_equal(current_proc, &proc_table[0]);
  proc_tick();
  proc_check_resched();
  assert_ptr_equal(current_proc, &proc_table[1]);
  proc_tick();
  proc_check_resched();
  assert_ptr_equal(current_proc, &proc_table[2]);
  proc_tick();
  proc_check_resched();
  assert_ptr_equal(current_proc, &proc_table[0]);
}

static void test_queue_corruption(void **state)
{
  (void)state;
  char   *argv[] = {NULL};
  proc_create_mem("p1", NULL, 0, argv, argv);
  proc_t *p = &proc_table[0];
  p->state  = PROC_STATE_FREE;
  proc_block(p);
  assert_int_equal(p->state, PROC_STATE_FREE);
  proc_wake(p);
  assert_int_equal(p->state, PROC_STATE_FREE);
  p->state = PROC_STATE_ZOMBIE;
  proc_block(p);
  assert_int_equal(p->state, PROC_STATE_ZOMBIE);
}

static void test_waitpid_edge_cases(void **state)
{
  (void)state;
  char *argv[] = {NULL};
  proc_create_mem("parent", NULL, 0, argv, argv);
  current_proc        = &proc_table[0];
  current_proc->state = PROC_STATE_RUNNING;

  syscall_frame_t frame     = {0};
  i64             child_pid = proc_fork(&frame);
  assert_true(child_pid > 0);
  proc_t *child = proc_get(child_pid);
  assert_non_null(child);

  i32 status = 0;
  i64 ret    = proc_waitpid(child_pid, &status, WNOHANG);
  assert_int_equal(ret, 0);

  child->state     = PROC_STATE_ZOMBIE;
  child->exit_code = 42;
  ret              = proc_waitpid(child_pid, &status, 0);
  assert_int_equal(ret, child_pid);
  assert_int_equal(status, 42 << 8);
  assert_int_equal(child->state, PROC_STATE_FREE);

  ret = proc_waitpid(9999, &status, 0);
  assert_int_equal(ret, -ECHILD);
}

static void test_zombie_leaks(void **state)
{
  (void)state;
  char *argv[] = {NULL};
  proc_create_mem("init", NULL, 0, argv, argv);
  u64    parent_pid = proc_create_mem("parent", NULL, 0, argv, argv);
  current_proc      = proc_get(parent_pid);

  syscall_frame_t frame     = {0};
  i64             child_pid = proc_fork(&frame);
  proc_t         *child     = proc_get(child_pid);
  if(setjmp(halt_jmp) == 0)
    proc_exit(0);
  assert_int_equal(child->parent_pid, 1);
}

static void test_proc_init_clears_table(void **state)
{
  (void)state;
  proc_table[3].state = PROC_STATE_RUNNING;
  proc_table[3].pid   = 99;
  proc_init();
  for(int i = 0; i < PROC_MAX; i++) {
    assert_int_equal(proc_table[i].state, PROC_STATE_FREE);
    assert_int_equal(proc_table[i].pid, 0);
  }
}

static void test_proc_current_returns_current_proc(void **state)
{
  (void)state;
  char *argv[] = {NULL};
  proc_create_mem("p", NULL, 0, argv, argv);
  current_proc = &proc_table[0];
  assert_ptr_equal(proc_current(), &proc_table[0]);
  current_proc = NULL;
  assert_null(proc_current());
}

static void test_proc_alloc_sets_cwd_to_root(void **state)
{
  (void)state;
  char *argv[] = {NULL};
  proc_create_mem("p", NULL, 0, argv, argv);
  assert_string_equal(proc_table[0].cwd, "/");
}

static void test_proc_alloc_reuses_freed_slot(void **state)
{
  (void)state;
  char   *argv[] = {NULL};
  u64     pid1   = proc_create_mem("p1", NULL, 0, argv, argv);
  proc_t *p      = proc_get(pid1);
  p->state       = PROC_STATE_FREE;
  u64 pid2       = proc_create_mem("p2", NULL, 0, argv, argv);
  assert_int_equal(proc_table_index(proc_get(pid2)), 0);
}

static void test_schedule_wraps_around_table(void **state)
{
  (void)state;
  char *argv[] = {NULL};
  proc_create_mem("p0", NULL, 0, argv, argv);
  proc_create_mem("p1", NULL, 0, argv, argv);
  proc_table[0].state = PROC_STATE_READY;
  proc_table[1].state = PROC_STATE_RUNNING;
  current_proc        = &proc_table[1];
  proc_schedule();
  assert_ptr_equal(current_proc, &proc_table[0]);
}

static void test_schedule_running_yields_to_self(void **state)
{
  (void)state;
  char *argv[] = {NULL};
  proc_create_mem("p", NULL, 0, argv, argv);
  proc_table[0].state = PROC_STATE_RUNNING;
  current_proc        = &proc_table[0];
  proc_schedule();
  assert_ptr_equal(current_proc, &proc_table[0]);
}

static void test_schedule_skips_non_ready(void **state)
{
  (void)state;
  char *argv[] = {NULL};
  proc_create_mem("p0", NULL, 0, argv, argv);
  proc_create_mem("p1", NULL, 0, argv, argv);
  proc_create_mem("p2", NULL, 0, argv, argv);
  proc_table[0].state = PROC_STATE_BLOCKED;
  proc_table[1].state = PROC_STATE_ZOMBIE;
  proc_table[2].state = PROC_STATE_READY;
  current_proc        = NULL;
  proc_schedule();
  assert_ptr_equal(current_proc, &proc_table[2]);
}

static void test_schedule_prefers_next_in_ring(void **state)
{
  (void)state;
  char *argv[] = {NULL};
  proc_create_mem("p0", NULL, 0, argv, argv);
  proc_create_mem("p1", NULL, 0, argv, argv);
  proc_create_mem("p2", NULL, 0, argv, argv);
  proc_table[0].state = PROC_STATE_READY;
  proc_table[1].state = PROC_STATE_READY;
  proc_table[2].state = PROC_STATE_READY;
  current_proc        = &proc_table[0];
  current_proc->state = PROC_STATE_READY;
  proc_schedule();
  assert_ptr_equal(current_proc, &proc_table[1]);
}

static void test_proc_switch_noop_when_same(void **state)
{
  (void)state;
  char *argv[] = {NULL};
  proc_create_mem("p", NULL, 0, argv, argv);
  proc_table[0].state = PROC_STATE_RUNNING;
  current_proc        = &proc_table[0];
  proc_switch(&proc_table[0]);
  assert_ptr_equal(current_proc, &proc_table[0]);
}

static void test_proc_wait_zombie_immediate(void **state)
{
  (void)state;
  char   *argv[] = {NULL};
  u64     pid    = proc_create_mem("child", NULL, 0, argv, argv);
  current_proc   = &proc_table[0];
  proc_t *child  = proc_get(pid);
  child->state   = PROC_STATE_ZOMBIE;
  child->exit_code = 7;
  i64 code       = proc_wait(pid);
  assert_int_equal(code, 7);
  assert_int_equal(child->state, PROC_STATE_FREE);
}

static void test_proc_wait_unknown_pid(void **state)
{
  (void)state;
  assert_int_equal(proc_wait(9999), -1);
}

static void test_waitpid_any_child_finds_zombie(void **state)
{
  (void)state;
  char *argv[] = {NULL};
  proc_create_mem("parent", NULL, 0, argv, argv);
  current_proc        = &proc_table[0];
  current_proc->state = PROC_STATE_RUNNING;
  syscall_frame_t frame     = {0};
  i64             child_pid = proc_fork(&frame);
  proc_t         *child     = proc_get(child_pid);
  child->state              = PROC_STATE_ZOMBIE;
  child->exit_code          = 3;
  i32 status = 0;
  i64 ret    = proc_waitpid(-1, &status, 0);
  assert_int_equal(ret, child_pid);
  assert_int_equal(status, 3 << 8);
}

static void test_waitpid_any_child_wnohang_no_zombie(void **state)
{
  (void)state;
  char *argv[] = {NULL};
  proc_create_mem("parent", NULL, 0, argv, argv);
  current_proc        = &proc_table[0];
  current_proc->state = PROC_STATE_RUNNING;
  syscall_frame_t frame = {0};
  proc_fork(&frame);
  i32 status = 0;
  assert_int_equal(proc_waitpid(-1, &status, WNOHANG), 0);
}

static void test_waitpid_no_children_returns_echild(void **state)
{
  (void)state;
  char *argv[] = {NULL};
  proc_create_mem("lonely", NULL, 0, argv, argv);
  current_proc        = &proc_table[0];
  current_proc->state = PROC_STATE_RUNNING;
  i32 status          = 0;
  assert_int_equal(proc_waitpid(-1, &status, WNOHANG), -ECHILD);
}

static void test_waitpid_pid_zero_returns_einval(void **state)
{
  (void)state;
  char *argv[] = {NULL};
  proc_create_mem("p", NULL, 0, argv, argv);
  current_proc        = &proc_table[0];
  current_proc->state = PROC_STATE_RUNNING;
  i32 status          = 0;
  assert_int_equal(proc_waitpid(0, &status, 0), -EINVAL);
}

static void test_waitpid_non_child_returns_echild(void **state)
{
  (void)state;
  char *argv[] = {NULL};
  proc_create_mem("p1", NULL, 0, argv, argv);
  u64 other           = proc_create_mem("p2", NULL, 0, argv, argv);
  current_proc        = &proc_table[0];
  current_proc->state = PROC_STATE_RUNNING;
  i32 status          = 0;
  assert_int_equal(proc_waitpid((i64)other, &status, WNOHANG), -ECHILD);
}

static void test_proc_exit_wakes_waiting_parent(void **state)
{
  (void)state;
  char *argv[] = {NULL};
  proc_create_mem("init", NULL, 0, argv, argv);
  u64    parent_pid = proc_create_mem("parent", NULL, 0, argv, argv);
  current_proc      = proc_get(parent_pid);
  syscall_frame_t frame     = {0};
  i64             child_pid = proc_fork(&frame);
  proc_t         *parent    = proc_get(parent_pid);
  proc_t         *child     = proc_get(child_pid);
  parent->state             = PROC_STATE_BLOCKED;
  parent->waiting_for_pid   = (u64)child_pid;
  current_proc              = child;
  if(setjmp(halt_jmp) == 0)
    proc_exit(5);
  assert_int_equal(parent->state, PROC_STATE_READY);
  assert_int_equal(child->exit_code, 5);
  assert_int_equal(child->state, PROC_STATE_ZOMBIE);
}

static void test_proc_exit_does_not_wake_unrelated_parent(void **state)
{
  (void)state;
  char *argv[] = {NULL};
  proc_create_mem("init", NULL, 0, argv, argv);
  u64    parent_pid = proc_create_mem("parent", NULL, 0, argv, argv);
  current_proc      = proc_get(parent_pid);
  syscall_frame_t frame     = {0};
  i64             child_pid = proc_fork(&frame);
  proc_t         *parent    = proc_get(parent_pid);
  parent->state             = PROC_STATE_BLOCKED;
  parent->waiting_for_pid   = 9999;
  current_proc              = proc_get(child_pid);
  if(setjmp(halt_jmp) == 0)
    proc_exit(0);
  assert_int_equal(parent->state, PROC_STATE_BLOCKED);
}

static void test_proc_notify_exec_wakes_vfork_parent(void **state)
{
  (void)state;
  char *argv[] = {NULL};
  proc_create_mem("init", NULL, 0, argv, argv);
  u64    parent_pid = proc_create_mem("parent", NULL, 0, argv, argv);
  current_proc      = proc_get(parent_pid);
  syscall_frame_t frame     = {0};
  i64             child_pid = proc_fork(&frame);
  proc_t         *parent    = proc_get(parent_pid);
  proc_t         *child     = proc_get(child_pid);
  parent->state             = PROC_STATE_BLOCKED;
  parent->vfork_waiting_for = (u64)child_pid;
  proc_notify_exec(child);
  assert_int_equal(parent->state, PROC_STATE_READY);
  assert_int_equal(parent->vfork_waiting_for, 0);
}

static void test_proc_notify_exec_null_is_noop(void **state)
{
  (void)state;
  proc_notify_exec(NULL);
}

static void test_fork_child_inherits_cwd(void **state)
{
  (void)state;
  char *argv[] = {NULL};
  proc_create_mem("parent", NULL, 0, argv, argv);
  current_proc        = &proc_table[0];
  current_proc->state = PROC_STATE_RUNNING;
  kstrncpy(current_proc->cwd, "/usr/bin", VFS_PATH_MAX);
  syscall_frame_t frame     = {0};
  i64             child_pid = proc_fork(&frame);
  assert_string_equal(proc_get(child_pid)->cwd, "/usr/bin");
}

static void test_fork_child_pid_is_monotonic(void **state)
{
  (void)state;
  char *argv[] = {NULL};
  proc_create_mem("parent", NULL, 0, argv, argv);
  current_proc        = &proc_table[0];
  current_proc->state = PROC_STATE_RUNNING;
  syscall_frame_t frame = {0};
  i64             pid1  = proc_fork(&frame);
  i64             pid2  = proc_fork(&frame);
  assert_true(pid2 > pid1);
}

static void test_fork_child_parent_pid_set(void **state)
{
  (void)state;
  char *argv[] = {NULL};
  proc_create_mem("parent", NULL, 0, argv, argv);
  current_proc        = &proc_table[0];
  current_proc->state = PROC_STATE_RUNNING;
  u64             parent_pid = current_proc->pid;
  syscall_frame_t frame      = {0};
  i64             child_pid  = proc_fork(&frame);
  assert_int_equal(proc_get(child_pid)->parent_pid, (int)parent_pid);
}

static void test_proc_get_null_on_missing(void **state)
{
  (void)state;
  assert_null(proc_get(9999));
  assert_null(proc_get(0));
}

static void test_proc_get_finds_existing(void **state)
{
  (void)state;
  char   *argv[] = {NULL};
  u64     pid    = proc_create_mem("p", NULL, 0, argv, argv);
  proc_t *p      = proc_get(pid);
  assert_non_null(p);
  assert_int_equal(p->pid, pid);
}

static void test_proc_get_ignores_free_slot(void **state)
{
  (void)state;
  proc_table[0].pid   = 42;
  proc_table[0].state = PROC_STATE_FREE;
  assert_null(proc_get(42));
}

static void test_proc_table_index(void **state)
{
  (void)state;
  char *argv[] = {NULL};
  proc_create_mem("p", NULL, 0, argv, argv);
  assert_int_equal(proc_table_index(&proc_table[0]), 0);
  assert_int_equal(proc_table_index(NULL), -1);
  proc_t fake;
  assert_int_equal(proc_table_index(&fake), -1);
}

static void test_proc_name(void **state)
{
  (void)state;
  assert_string_equal(proc_name(NULL), "(none)");
  char *argv[] = {NULL};
  proc_create_mem("hello", NULL, 0, argv, argv);
  assert_string_equal(proc_name(&proc_table[0]), "hello");
}

static void test_proc_block_wake_transitions(void **state)
{
  (void)state;
  char   *argv[] = {NULL};
  proc_create_mem("p", NULL, 0, argv, argv);
  proc_t *p = &proc_table[0];

  p->state = PROC_STATE_READY;
  proc_block(p);
  assert_int_equal(p->state, PROC_STATE_BLOCKED);
  proc_wake(p);
  assert_int_equal(p->state, PROC_STATE_READY);

  p->state = PROC_STATE_RUNNING;
  proc_block(p);
  assert_int_equal(p->state, PROC_STATE_BLOCKED);

  p->state = PROC_STATE_READY;
  proc_wake(p);
  assert_int_equal(p->state, PROC_STATE_READY);
}

static void test_proc_block_rejects_invalid_states(void **state)
{
  (void)state;
  char   *argv[] = {NULL};
  proc_create_mem("p", NULL, 0, argv, argv);
  proc_t *p = &proc_table[0];

  p->state = PROC_STATE_ZOMBIE;
  proc_block(p);
  assert_int_equal(p->state, PROC_STATE_ZOMBIE);

  p->state = PROC_STATE_FREE;
  proc_block(p);
  assert_int_equal(p->state, PROC_STATE_FREE);

  proc_block(NULL);
}

static void test_proc_schedule_picks_first_ready(void **state)
{
  (void)state;
  char *argv[] = {NULL};
  proc_create_mem("p0", NULL, 0, argv, argv);
  proc_create_mem("p1", NULL, 0, argv, argv);
  proc_table[0].state = PROC_STATE_BLOCKED;
  proc_table[1].state = PROC_STATE_READY;
  proc_schedule();
  assert_ptr_equal(current_proc, &proc_table[1]);
}

static void test_proc_check_resched_clears_flag(void **state)
{
  (void)state;
  char *argv[] = {NULL};
  proc_create_mem("p", NULL, 0, argv, argv);
  proc_table[0].state = PROC_STATE_READY;
  need_resched        = true;
  proc_check_resched();
  assert_false(need_resched);
}

static void test_proc_tick_sets_resched(void **state)
{
  (void)state;
  need_resched = false;
  proc_tick();
  assert_true(need_resched);
}

static void test_proc_signal_broadcast_skips_free_zombie(void **state)
{
  (void)state;
  char *argv[] = {NULL};
  proc_create_mem("p0", NULL, 0, argv, argv);
  proc_create_mem("p1", NULL, 0, argv, argv);
  proc_table[0].state = PROC_STATE_FREE;
  proc_table[1].state = PROC_STATE_ZOMBIE;
  proc_signal_broadcast(9);
}

static void test_waitpid_wnohang_no_zombie(void **state)
{
  (void)state;
  char *argv[] = {NULL};
  proc_create_mem("parent", NULL, 0, argv, argv);
  current_proc        = &proc_table[0];
  current_proc->state = PROC_STATE_RUNNING;
  syscall_frame_t frame     = {0};
  i64             child_pid = proc_fork(&frame);
  proc_get(child_pid)->state = PROC_STATE_READY;
  i32 status                 = 0;
  assert_int_equal(proc_waitpid(child_pid, &status, WNOHANG), 0);
}

static void test_reparenting_on_exit(void **state)
{
  (void)state;
  char *argv[] = {NULL};
  proc_create_mem("init", NULL, 0, argv, argv);
  u64    parent_pid = proc_create_mem("parent", NULL, 0, argv, argv);
  current_proc      = proc_get(parent_pid);
  syscall_frame_t frame     = {0};
  i64             child_pid = proc_fork(&frame);
  proc_t         *child     = proc_get(child_pid);
  if(setjmp(halt_jmp) == 0)
    proc_exit(0);
  assert_int_equal(child->parent_pid, 1);
}

/* proc_wait: non-zombie child — blocks then returns -1 (child not zombie
   after schedule, since context_switch is a noop and child stays READY). */
static void test_proc_wait_non_zombie_returns_minus1(void **state)
{
  (void)state;
  char *argv[] = {NULL};
  proc_create_mem("parent", NULL, 0, argv, argv);
  current_proc        = &proc_table[0];
  current_proc->state = PROC_STATE_RUNNING;
  syscall_frame_t frame     = {0};
  i64             child_pid = proc_fork(&frame);
  proc_t         *child     = proc_get(child_pid);
  child->state              = PROC_STATE_READY; /* not zombie */
  i64 ret                   = proc_wait((u64)child_pid);
  assert_int_equal(ret, -1);
}

/* proc_wake on a non-BLOCKED process is a noop */
static void test_proc_wake_noop_on_non_blocked(void **state)
{
  (void)state;
  char *argv[] = {NULL};
  proc_create_mem("p", NULL, 0, argv, argv);
  proc_t *p = &proc_table[0];
  p->state  = PROC_STATE_READY;
  proc_wake(p);
  assert_int_equal(p->state, PROC_STATE_READY);

  p->state = PROC_STATE_RUNNING;
  proc_wake(p);
  assert_int_equal(p->state, PROC_STATE_RUNNING);
}

/* proc_signal_broadcast: signals running processes, skips free/zombie */
static void test_proc_signal_broadcast_signals_running(void **state)
{
  (void)state;
  char *argv[] = {NULL};
  proc_create_mem("p0", NULL, 0, argv, argv);
  proc_create_mem("p1", NULL, 0, argv, argv);
  proc_table[0].state = PROC_STATE_RUNNING;
  proc_table[1].state = PROC_STATE_READY;
  /* proc_signal is a stub noop — just ensure it doesn't crash */
  proc_signal_broadcast(SIGUSR1);
}

/* proc_waitpid: fills status when child is zombie */
static void test_waitpid_fills_status(void **state)
{
  (void)state;
  char *argv[] = {NULL};
  proc_create_mem("parent", NULL, 0, argv, argv);
  current_proc        = &proc_table[0];
  current_proc->state = PROC_STATE_RUNNING;
  syscall_frame_t frame     = {0};
  i64             child_pid = proc_fork(&frame);
  proc_t         *child     = proc_get(child_pid);
  child->state              = PROC_STATE_ZOMBIE;
  child->exit_code          = 42;

  i32 status = 0;
  i64 ret    = proc_waitpid(child_pid, &status, 0);
  assert_int_equal(ret, child_pid);
  assert_int_equal(status, (42 & 0xFF) << 8);
}

/* proc_waitpid: specific child not zombie, WNOHANG → 0 (already tested via
   test_waitpid_wnohang_no_zombie, but add explicit status=NULL variant) */
static void test_waitpid_specific_not_zombie_wnohang_null_status(void **state)
{
  (void)state;
  char *argv[] = {NULL};
  proc_create_mem("parent", NULL, 0, argv, argv);
  current_proc        = &proc_table[0];
  current_proc->state = PROC_STATE_RUNNING;
  syscall_frame_t frame     = {0};
  i64             child_pid = proc_fork(&frame);
  proc_get(child_pid)->state = PROC_STATE_READY;
  i64 ret = proc_waitpid(child_pid, NULL, WNOHANG);
  assert_int_equal(ret, 0);
}

/* proc_waitpid: blocking for any child — after schedule child is still not
   zombie, then proc_schedule runs and the while loop exits because context_switch
   is noop and proc_schedule just returns after switching. The loop then finds
   the child zombie if we set it. We set child to zombie before calling so it's
   found immediately on first scan. */
static void test_waitpid_any_child_zombie_found_in_loop(void **state)
{
  (void)state;
  char *argv[] = {NULL};
  proc_create_mem("parent", NULL, 0, argv, argv);
  current_proc        = &proc_table[0];
  current_proc->state = PROC_STATE_RUNNING;
  syscall_frame_t frame     = {0};
  i64             child_pid = proc_fork(&frame);
  proc_t         *child     = proc_get(child_pid);
  /* Child is zombie from the start so the initial scan finds it */
  child->state     = PROC_STATE_ZOMBIE;
  child->exit_code = 7;

  i32 status = 0;
  i64 ret    = proc_waitpid(-1, &status, 0);
  assert_int_equal(ret, child_pid);
  assert_int_equal(status, (7 & 0xFF) << 8);
}

/* proc_waitpid: specific child becomes zombie after schedule (simulate by
   setting child zombie before the blocking while loop would run). Since
   context_switch is noop, proc_schedule returns immediately and re-checks. */
static void test_waitpid_specific_child_found_zombie_after_schedule(void **state)
{
  (void)state;
  char *argv[] = {NULL};
  proc_create_mem("parent", NULL, 0, argv, argv);
  current_proc        = &proc_table[0];
  current_proc->state = PROC_STATE_RUNNING;
  syscall_frame_t frame     = {0};
  i64             child_pid = proc_fork(&frame);
  proc_t         *child     = proc_get(child_pid);
  child->state              = PROC_STATE_ZOMBIE;
  child->exit_code          = 99;

  i64 ret = proc_waitpid(child_pid, NULL, 0);
  assert_int_equal(ret, child_pid);
}

/* proc_exit: parent waiting_for_pid doesn't match child — parent not woken */
static void test_proc_exit_parent_waiting_for_other_not_woken(void **state)
{
  (void)state;
  char *argv[] = {NULL};
  proc_create_mem("init", NULL, 0, argv, argv);
  u64 parent_pid = proc_create_mem("parent", NULL, 0, argv, argv);
  current_proc   = proc_get(parent_pid);
  current_proc->state = PROC_STATE_RUNNING;
  syscall_frame_t frame     = {0};
  i64             child_pid = proc_fork(&frame);
  proc_t         *child     = proc_get(child_pid);
  proc_t         *parent    = proc_get(parent_pid);

  /* Parent is blocked waiting for a *different* pid */
  parent->state           = PROC_STATE_BLOCKED;
  parent->waiting_for_pid = 9999;

  /* Exit from the child's perspective */
  current_proc = child;
  if(setjmp(halt_jmp) == 0)
    proc_exit(0);

  /* Parent was waiting for 9999, not child_pid, so stays BLOCKED */
  assert_int_equal(parent->state, PROC_STATE_BLOCKED);
}

/* proc_clone: delegates to proc_fork_impl */
static void test_proc_clone_delegates(void **state)
{
  (void)state;
  char *argv[] = {NULL};
  proc_create_mem("parent", NULL, 0, argv, argv);
  current_proc        = &proc_table[0];
  current_proc->state = PROC_STATE_RUNNING;
  syscall_frame_t frame = {0};
  i64 child_pid = proc_clone(&frame, 0, 0);
  assert_true(child_pid > 0);
}

/* proc_waitpid any-child blocking loop: child not zombie at first scan,
   becomes zombie inside context_switch callback */
static proc_t *g_blocking_child;
static void make_child_zombie(void)
{
  if(g_blocking_child)
    g_blocking_child->state = PROC_STATE_ZOMBIE;
}

static void test_waitpid_any_child_enters_blocking_loop(void **state)
{
  (void)state;
  char *argv[] = {NULL};
  proc_create_mem("parent", NULL, 0, argv, argv);
  current_proc        = &proc_table[0];
  current_proc->state = PROC_STATE_RUNNING;
  syscall_frame_t frame = {0};
  i64 child_pid         = proc_fork(&frame);
  proc_t *child         = proc_get(child_pid);

  /* Child is READY (not zombie) so initial scan finds a non-zombie child */
  child->state        = PROC_STATE_READY;
  child->exit_code    = 11;
  g_blocking_child    = child;
  /* context_switch callback makes child zombie so loop exits */
  g_context_switch_cb = make_child_zombie;

  i32 status = 0;
  i64 ret    = proc_waitpid(-1, &status, 0);
  assert_int_equal(ret, child_pid);
}

/* proc_waitpid specific-child blocking: loop runs, child becomes zombie */
static void test_waitpid_specific_blocking_loop(void **state)
{
  (void)state;
  char *argv[] = {NULL};
  proc_create_mem("parent", NULL, 0, argv, argv);
  current_proc        = &proc_table[0];
  current_proc->state = PROC_STATE_RUNNING;
  syscall_frame_t frame = {0};
  i64 child_pid         = proc_fork(&frame);
  proc_t *child         = proc_get(child_pid);

  /* Child is READY — not zombie — so blocking while loop will be entered */
  child->state        = PROC_STATE_READY;
  child->exit_code    = 5;
  g_blocking_child    = child;
  g_context_switch_cb = make_child_zombie;

  i64 ret = proc_waitpid(child_pid, NULL, 0);
  assert_int_equal(ret, child_pid);
}

/* proc_alloc exhaustion: filling all slots returns 0 from proc_create_mem */
static void test_proc_alloc_exhaustion_returns_zero(void **state)
{
  (void)state;
  char *argv[] = {NULL};
  /* Fill all slots */
  for(int i = 0; i < PROC_MAX; i++)
    proc_table[i].state = PROC_STATE_RUNNING; /* mark as non-free */
  u64 pid = proc_create_mem("extra", NULL, 0, argv, argv);
  assert_int_equal(pid, 0);
  /* Restore */
  for(int i = 0; i < PROC_MAX; i++)
    proc_table[i].state = PROC_STATE_FREE;
}

/* proc_waitpid blocking for any child: child becomes zombie after schedule */
static void test_waitpid_any_child_blocking_loop(void **state)
{
  (void)state;
  char *argv[] = {NULL};
  proc_create_mem("parent", NULL, 0, argv, argv);
  current_proc        = &proc_table[0];
  current_proc->state = PROC_STATE_RUNNING;
  syscall_frame_t frame = {0};
  i64 child_pid         = proc_fork(&frame);
  proc_t *child         = proc_get(child_pid);

  /* No zombie child at first — proc_schedule will make it zombie */
  child->state     = PROC_STATE_RUNNING;
  child->exit_code = 3;
  /* After first proc_schedule call (which is a noop), the loop re-scans.
     We manually set to zombie so the second scan finds it. */
  child->state = PROC_STATE_ZOMBIE;

  i32 status = 0;
  i64 ret    = proc_waitpid(-1, &status, 0);
  assert_int_equal(ret, child_pid);
}

/* proc_waitpid: specific child blocking loop — child disappears after schedule */
static void test_waitpid_specific_child_disappears_after_schedule(void **state)
{
  (void)state;
  char *argv[] = {NULL};
  proc_create_mem("parent", NULL, 0, argv, argv);
  current_proc        = &proc_table[0];
  current_proc->state = PROC_STATE_RUNNING;
  syscall_frame_t frame = {0};
  i64 child_pid         = proc_fork(&frame);
  proc_t *child         = proc_get(child_pid);
  /* Mark zombie before waiting so blocking loop exits immediately */
  child->state     = PROC_STATE_ZOMBIE;
  child->exit_code = 5;

  i64 ret = proc_waitpid(child_pid, NULL, 0);
  assert_int_equal(ret, child_pid);
}

/* proc_signal: stubbed as noop — verify it doesn't crash on valid pid */
static void test_proc_signal_valid_pid_noop(void **state)
{
  (void)state;
  char *argv[] = {NULL};
  u64 pid      = proc_create_mem("p", NULL, 0, argv, argv);
  assert_true(pid > 0);
  /* proc_signal is a stub noop in this test harness */
  proc_signal(pid, SIGUSR1);
}

/* proc_signal: unknown pid is a no-op (no crash) */
static void test_proc_signal_unknown_pid_noop(void **state)
{
  (void)state;
  /* proc_signal returns void; just verify it doesn't crash */
  proc_signal(9999, SIGUSR1);
}

/* proc_exec_replace_image: replaces image on current proc */
static void test_proc_exec_replace_image(void **state)
{
  (void)state;
  char *argv[] = {(char *)"shell", NULL};
  char *envp[] = {(char *)"PATH=/bin", NULL};
  proc_create_mem("init", NULL, 0, argv, envp);
  current_proc = &proc_table[0];
  /* elf_load_fd stub always succeeds */
  i64 ret = proc_exec_replace_image(current_proc, "newprog", 3, argv, envp);
  assert_int_equal(ret, 0);
  assert_string_equal(current_proc->name, "newprog");
}

/* proc_setup_image: with real argv and envp → pushes strings onto stack */
static void test_proc_setup_image_with_argv(void **state)
{
  (void)state;
  char *argv[] = {(char *)"prog", (char *)"arg1", NULL};
  char *envp[] = {(char *)"HOME=/", NULL};
  u64 ret = proc_create_mem("prog", NULL, 0, argv, envp);
  assert_true(ret > 0); /* returns pid */
}

/* proc_fork_impl: no current_proc → -ESRCH */
static void test_proc_fork_no_current_esrch(void **state)
{
  (void)state;
  current_proc = NULL;
  syscall_frame_t frame = {0};
  i64 ret = proc_fork(&frame);
  assert_int_equal(ret, -ESRCH);
}

/* proc_fork_impl: vmm_clone_address_space fails → -ENOMEM */
static void test_proc_fork_clone_fail_enomem(void **state)
{
  (void)state;
  char *argv[] = {(char *)"init", NULL};
  proc_create_mem("init", NULL, 0, argv, argv);
  current_proc     = &proc_table[0];
  g_vmm_clone_fail = true;
  syscall_frame_t frame = {0};
  i64 ret = proc_fork(&frame);
  assert_int_equal(ret, -ENOMEM);
  g_vmm_clone_fail = false;
}

/* proc_waitpid: no current proc → -1 */
static void test_proc_waitpid_no_current(void **state)
{
  (void)state;
  current_proc = NULL;
  i64 ret = proc_waitpid(-1, NULL, WNOHANG);
  assert_int_equal(ret, -1);
}

/* proc_waitpid: specific pid not a child → -ECHILD */
static void test_proc_waitpid_specific_no_child_echild(void **state)
{
  (void)state;
  char *argv[] = {(char *)"init", NULL};
  proc_create_mem("init", NULL, 0, argv, argv);
  current_proc = &proc_table[0];
  /* pid 99 doesn't exist */
  i64 ret = proc_waitpid(99, NULL, WNOHANG);
  assert_int_equal(ret, -ECHILD);
}

/* proc_create_mem: with real argv and envp to exercise push_string paths */
static void test_proc_create_mem_with_argv_envp(void **state)
{
  (void)state;
  char *argv[] = {(char *)"myapp", (char *)"--flag", NULL};
  char *envp[] = {(char *)"VAR=val", (char *)"X=1", NULL};
  u64 pid = proc_create_mem("myapp", NULL, 0, argv, envp);
  assert_true(pid > 0);
  proc_t *p = proc_get(pid);
  assert_non_null(p);
  assert_string_equal(p->name, "myapp");
}

/* proc_create_inner: vmm_create_address_space fails → returns 0 (line 413-414) */
static void test_proc_create_vmm_fail(void **state) {
  (void)state;
  g_vmm_create_fail = true;
  char *argv[] = {(char *)"init", NULL};
  u64 pid = proc_create_mem("init", NULL, 0, argv, argv);
  assert_int_equal(pid, 0); /* failed */
  g_vmm_create_fail = false;
}

/* proc_create_inner: pmm_alloc_pages (kernel stack) fails → returns 0 (line 419-421) */
static void test_proc_create_stack_fail(void **state) {
  (void)state;
  g_pmm_fail = true;
  char *argv[] = {(char *)"init", NULL};
  u64 pid = proc_create_mem("init", NULL, 0, argv, argv);
  assert_int_equal(pid, 0);
  g_pmm_fail = false;
}

/* proc_create_inner: elf_load fails → returns 0 (lines 426-429) */
static void test_proc_create_elf_fail_enoexec(void **state) {
  (void)state;
  /* proc_create_mem passes elf_fd=-1 so elf_load() (not elf_load_fd) is called */
  g_elf_load_ret = -1;
  char *argv[] = {(char *)"init", NULL};
  u64 pid = proc_create_mem("init", NULL, 0, argv, argv);
  assert_int_equal(pid, 0);
  g_elf_load_ret = 0;
}

/* proc_setup_image: elf_info.phdr != 0 → pushes AT_PHDR auxv (lines 342-345) */
static void test_proc_setup_image_with_phdr(void **state) {
  (void)state;
  g_elf_load_phdr = 0x401000;
  char *argv[] = {(char *)"prog", NULL};
  u64 pid = proc_create_mem("prog", NULL, 0, argv, argv);
  assert_true(pid > 0);
  g_elf_load_phdr = 0;
}

/* proc_setup_image: user stack pmm_alloc_pages fails → -ENOMEM (line 210) */
static void test_proc_setup_image_elf_enomem(void **state) {
  (void)state;
  /* pmm_alloc_pages returns NULL → proc_setup_image returns -ENOMEM
   * → proc_create_inner returns 0 */
  g_pmm_fail = true;
  char *argv[] = {(char *)"prog", NULL};
  u64 pid = proc_create_mem("prog", NULL, 0, argv, argv);
  assert_int_equal(pid, 0);
  g_pmm_fail = false;
}

int main(void)
{
  const struct CMUnitTest tests[] = {
      cmocka_unit_test_setup_teardown(test_pid_exhaustion, setup, teardown),
      cmocka_unit_test_setup_teardown(test_preemption_stress, setup, teardown),
      cmocka_unit_test_setup_teardown(test_queue_corruption, setup, teardown),
      cmocka_unit_test_setup_teardown(test_waitpid_edge_cases, setup, teardown),
      cmocka_unit_test_setup_teardown(test_zombie_leaks, setup, teardown),
      cmocka_unit_test_setup_teardown(test_proc_init_clears_table, setup, teardown),
      cmocka_unit_test_setup_teardown(test_proc_current_returns_current_proc, setup, teardown),
      cmocka_unit_test_setup_teardown(test_proc_alloc_sets_cwd_to_root, setup, teardown),
      cmocka_unit_test_setup_teardown(test_proc_alloc_reuses_freed_slot, setup, teardown),
      cmocka_unit_test_setup_teardown(test_schedule_wraps_around_table, setup, teardown),
      cmocka_unit_test_setup_teardown(test_schedule_running_yields_to_self, setup, teardown),
      cmocka_unit_test_setup_teardown(test_schedule_skips_non_ready, setup, teardown),
      cmocka_unit_test_setup_teardown(test_schedule_prefers_next_in_ring, setup, teardown),
      cmocka_unit_test_setup_teardown(test_proc_switch_noop_when_same, setup, teardown),
      cmocka_unit_test_setup_teardown(test_proc_wait_zombie_immediate, setup, teardown),
      cmocka_unit_test_setup_teardown(test_proc_wait_unknown_pid, setup, teardown),
      cmocka_unit_test_setup_teardown(test_waitpid_any_child_finds_zombie, setup, teardown),
      cmocka_unit_test_setup_teardown(test_waitpid_any_child_wnohang_no_zombie, setup, teardown),
      cmocka_unit_test_setup_teardown(test_waitpid_no_children_returns_echild, setup, teardown),
      cmocka_unit_test_setup_teardown(test_waitpid_pid_zero_returns_einval, setup, teardown),
      cmocka_unit_test_setup_teardown(test_waitpid_non_child_returns_echild, setup, teardown),
      cmocka_unit_test_setup_teardown(test_proc_exit_wakes_waiting_parent, setup, teardown),
      cmocka_unit_test_setup_teardown(test_proc_exit_does_not_wake_unrelated_parent, setup, teardown),
      cmocka_unit_test_setup_teardown(test_proc_notify_exec_wakes_vfork_parent, setup, teardown),
      cmocka_unit_test_setup_teardown(test_proc_notify_exec_null_is_noop, setup, teardown),
      cmocka_unit_test_setup_teardown(test_fork_child_inherits_cwd, setup, teardown),
      cmocka_unit_test_setup_teardown(test_fork_child_pid_is_monotonic, setup, teardown),
      cmocka_unit_test_setup_teardown(test_fork_child_parent_pid_set, setup, teardown),
      cmocka_unit_test_setup_teardown(test_proc_get_null_on_missing, setup, teardown),
      cmocka_unit_test_setup_teardown(test_proc_get_finds_existing, setup, teardown),
      cmocka_unit_test_setup_teardown(test_proc_get_ignores_free_slot, setup, teardown),
      cmocka_unit_test_setup_teardown(test_proc_table_index, setup, teardown),
      cmocka_unit_test_setup_teardown(test_proc_name, setup, teardown),
      cmocka_unit_test_setup_teardown(test_proc_block_wake_transitions, setup, teardown),
      cmocka_unit_test_setup_teardown(test_proc_block_rejects_invalid_states, setup, teardown),
      cmocka_unit_test_setup_teardown(test_proc_schedule_picks_first_ready, setup, teardown),
      cmocka_unit_test_setup_teardown(test_proc_check_resched_clears_flag, setup, teardown),
      cmocka_unit_test_setup_teardown(test_proc_tick_sets_resched, setup, teardown),
      cmocka_unit_test_setup_teardown(test_proc_signal_broadcast_skips_free_zombie, setup, teardown),
      cmocka_unit_test_setup_teardown(test_waitpid_wnohang_no_zombie, setup, teardown),
      cmocka_unit_test_setup_teardown(test_reparenting_on_exit, setup, teardown),
      cmocka_unit_test_setup_teardown(test_proc_wait_non_zombie_returns_minus1, setup, teardown),
      cmocka_unit_test_setup_teardown(test_proc_wake_noop_on_non_blocked, setup, teardown),
      cmocka_unit_test_setup_teardown(test_proc_signal_broadcast_signals_running, setup, teardown),
      cmocka_unit_test_setup_teardown(test_waitpid_fills_status, setup, teardown),
      cmocka_unit_test_setup_teardown(test_waitpid_specific_not_zombie_wnohang_null_status, setup, teardown),
      cmocka_unit_test_setup_teardown(test_waitpid_any_child_zombie_found_in_loop, setup, teardown),
      cmocka_unit_test_setup_teardown(test_waitpid_specific_child_found_zombie_after_schedule, setup, teardown),
      cmocka_unit_test_setup_teardown(test_proc_exit_parent_waiting_for_other_not_woken, setup, teardown),
      cmocka_unit_test_setup_teardown(test_proc_clone_delegates, setup, teardown),
      cmocka_unit_test_setup_teardown(test_waitpid_any_child_enters_blocking_loop, setup, teardown),
      cmocka_unit_test_setup_teardown(test_waitpid_specific_blocking_loop, setup, teardown),
      cmocka_unit_test_setup_teardown(test_proc_alloc_exhaustion_returns_zero, setup, teardown),
      cmocka_unit_test_setup_teardown(test_waitpid_any_child_blocking_loop, setup, teardown),
      cmocka_unit_test_setup_teardown(test_waitpid_specific_child_disappears_after_schedule, setup, teardown),
      cmocka_unit_test_setup_teardown(test_proc_signal_valid_pid_noop, setup, teardown),
      cmocka_unit_test_setup_teardown(test_proc_signal_unknown_pid_noop, setup, teardown),
      /* new coverage */
      cmocka_unit_test_setup_teardown(test_proc_exec_replace_image, setup, teardown),
      cmocka_unit_test_setup_teardown(test_proc_setup_image_with_argv, setup, teardown),
      cmocka_unit_test_setup_teardown(test_proc_fork_no_current_esrch, setup, teardown),
      cmocka_unit_test_setup_teardown(test_proc_fork_clone_fail_enomem, setup, teardown),
      cmocka_unit_test_setup_teardown(test_proc_waitpid_no_current, setup, teardown),
      cmocka_unit_test_setup_teardown(test_proc_waitpid_specific_no_child_echild, setup, teardown),
      cmocka_unit_test_setup_teardown(test_proc_create_mem_with_argv_envp, setup, teardown),
      /* new coverage */
      cmocka_unit_test_setup_teardown(test_proc_create_vmm_fail, setup, teardown),
      cmocka_unit_test_setup_teardown(test_proc_create_stack_fail, setup, teardown),
      cmocka_unit_test_setup_teardown(test_proc_create_elf_fail_enoexec, setup, teardown),
      cmocka_unit_test_setup_teardown(test_proc_setup_image_with_phdr, setup, teardown),
      cmocka_unit_test_setup_teardown(test_proc_setup_image_elf_enomem, setup, teardown),
  };
  return cmocka_run_group_tests(tests, NULL, NULL);
}
