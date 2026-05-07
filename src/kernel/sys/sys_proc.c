/**
 * @file src/kernel/sys/sys_proc.c
 * @brief Process syscalls: PID, fork/exec/wait, `clone` (spawn/fork helpers),
 * identities.
 *
 * Validates user pointers (strings, `argv`, `wait4` buffers) via the VMM before
 * access.
 */

#include <alcor2/errno.h>
#include <alcor2/fs/vfs.h>
#include <alcor2/kstdlib.h>
#include <alcor2/mm/vmm.h>
#include <alcor2/proc/proc.h>
#include <alcor2/sys/internal.h>

/* Linux `sched.h` clone flags — subset we implement (musl posix_spawn,
 * faccessat helper). */
#define ALCOR_CLONE_VM     0x00000100u
#define ALCOR_CLONE_VFORK  0x00004000u
#define ALCOR_CLONE_THREAD 0x00010000u
#define ALCOR_CSIGNAL      0x000000ffu

#define MAX_EXEC_ARGS      PROC_MAX_ARGV
#define MAX_ARG_LEN        PROC_MAX_ARG_STRLEN

static inline bool user_cstr_ok(u64 ptr)
{
  return ptr && vmm_is_user_ptr((void *)ptr);
}

static inline bool user_buf_ok(u64 ptr, u64 size)
{
  return ptr && vmm_is_user_range((void *)ptr, size);
}

u64 sys_getpid(u64 a1, u64 a2, u64 a3, u64 a4, u64 a5, u64 a6)
{
  (void)a1;
  (void)a2;
  (void)a3;
  (void)a4;
  (void)a5;
  (void)a6;

  proc_t *p = proc_current();
  return p ? p->pid : 1;
}

u64 sys_gettid(u64 a1, u64 a2, u64 a3, u64 a4, u64 a5, u64 a6)
{
  return sys_getpid(a1, a2, a3, a4, a5, a6);
}

u64 sys_getppid(u64 a1, u64 a2, u64 a3, u64 a4, u64 a5, u64 a6)
{
  (void)a1;
  (void)a2;
  (void)a3;
  (void)a4;
  (void)a5;
  (void)a6;

  proc_t *p = proc_current();
  return p ? p->parent_pid : 0;
}

u64 sys_clone(u64 flags, u64 child_stack, u64 ptid, u64 ctid, u64 tls, u64 a6)
{
  (void)ptid;
  (void)ctid;
  (void)tls;
  (void)a6;

  /* Threads need shared VM + TID plumbing — not supported. */
  if(flags & ALCOR_CLONE_THREAD)
    return (u64)-ENOSYS;

  /* musl: posix_spawn (VM|VFORK|SIGCHLD), faccessat (__clone flags 0). */
  if(flags & ~(ALCOR_CLONE_VM | ALCOR_CLONE_VFORK | ALCOR_CSIGNAL))
    return (u64)-EINVAL;

  syscall_frame_t *frame = syscall_get_current_frame();
  if(!frame)
    return (u64)-EINVAL;

  if(child_stack != 0 && !vmm_is_user_ptr((void *)child_stack))
    return (u64)-EFAULT;

  return (u64)proc_clone(frame, child_stack, (u32)flags);
}

u64 sys_fork(u64 a1, u64 a2, u64 a3, u64 a4, u64 a5, u64 a6)
{
  (void)a1;
  (void)a2;
  (void)a3;
  (void)a4;
  (void)a5;
  (void)a6;

  syscall_frame_t *frame = syscall_get_current_frame();
  if(!frame)
    return (u64)-EINVAL;
  return (u64)proc_fork(frame);
}

u64 sys_execve(u64 pathname, u64 argv, u64 envp, u64 a4, u64 a5, u64 a6)
{
  (void)envp;
  (void)a4;
  (void)a5;
  (void)a6;

  if(!user_cstr_ok(pathname))
    return (u64)-EFAULT;
  const char *path = (const char *)pathname;

  if(argv && !user_buf_ok(argv, sizeof(char *)))
    return (u64)-EFAULT;

  vfs_stat_t st;
  if(vfs_stat(path, &st) < 0)
    return (u64)-ENOENT;
  if(st.type != VFS_FILE)
    return (u64)-EACCES;

  /* Snapshot argv into kernel storage now — once we tear down user mappings
   * the user-space argv buffers will be gone. */
  static char  arg_storage[MAX_EXEC_ARGS][MAX_ARG_LEN];
  static char *new_argv[MAX_EXEC_ARGS + 1];
  static char  name_storage[MAX_ARG_LEN];

  int          argc      = 0;
  char       **user_argv = (char **)argv;
  if(user_argv && user_argv[0]) {
    for(int i = 0; user_argv[i] && argc < MAX_EXEC_ARGS; i++) {
      if(!user_cstr_ok((u64)user_argv[i]))
        return (u64)-EFAULT;
      kstrncpy(arg_storage[argc], user_argv[i], MAX_ARG_LEN);
      new_argv[argc] = arg_storage[argc];
      argc++;
    }
  } else {
    /* No argv supplied: synthesise argv[0] from path. */
    kstrncpy(arg_storage[0], path, MAX_ARG_LEN);
    new_argv[0] = arg_storage[0];
    argc        = 1;
  }
  new_argv[argc] = NULL;

  /* Snapshot the basename for the proc's name field — argv[0] may be relative
   * (e.g. "ls"), but we want a stable kernel-side string. */
  kstrncpy(name_storage, path, MAX_ARG_LEN);

  i64 fd = vfs_open(path, 0);
  if(fd < 0)
    return (u64)-ENOENT;

  proc_t *p = proc_current();
  if(!p) {
    vfs_close(fd);
    return (u64)-EINVAL;
  }

  i64 rc = proc_exec_replace_image(p, name_storage, fd, new_argv);
  vfs_close(fd);
  if(rc < 0) {
    /* Old image is gone but a new one couldn't be set up — terminate. */
    proc_exit(127);
  }

  /* Close file descriptors with O_CLOEXEC — POSIX exec semantics.
   * musl posix_spawn's error pipe uses O_CLOEXEC so the parent's read()
   * gets EOF when the child execs successfully. */
  vfs_proc_close_cloexec_fds();

  /* Wake vfork parent AFTER errpipe write-end is closed, so the parent's
   * read on the errpipe gets EOF and knows exec succeeded. */
  proc_notify_exec(p);

  /* Redirect the in-flight syscall return path to the new entry. The asm
   * stub pops rip / rflags / rsp from the frame and SYSRETs. */
  syscall_frame_t *frame = syscall_get_current_frame();
  if(frame) {
    frame->rip    = p->user_rip;
    frame->rsp    = p->user_rsp;
    frame->rflags = p->user_rflags;
  }
  return 0;
}

u64 sys_exit(u64 status, u64 a2, u64 a3, u64 a4, u64 a5, u64 a6)
{
  (void)a2;
  (void)a3;
  (void)a4;
  (void)a5;
  (void)a6;

  proc_exit((i64)status);
}

u64 sys_wait4(u64 pid, u64 wstatus, u64 options, u64 rusage, u64 a5, u64 a6)
{
  (void)rusage;
  (void)a5;
  (void)a6;
  if(wstatus && !user_buf_ok(wstatus, sizeof(i32)))
    return (u64)-EFAULT;
  return (u64)proc_waitpid((i64)pid, (i32 *)wstatus, (i32)options);
}

u64 sys_getuid(u64 a1, u64 a2, u64 a3, u64 a4, u64 a5, u64 a6)
{
  (void)a1;
  (void)a2;
  (void)a3;
  (void)a4;
  (void)a5;
  (void)a6;
  return 0;
}

u64 sys_getgid(u64 a1, u64 a2, u64 a3, u64 a4, u64 a5, u64 a6)
{
  (void)a1;
  (void)a2;
  (void)a3;
  (void)a4;
  (void)a5;
  (void)a6;
  return 0;
}

u64 sys_geteuid(u64 a1, u64 a2, u64 a3, u64 a4, u64 a5, u64 a6)
{
  (void)a1;
  (void)a2;
  (void)a3;
  (void)a4;
  (void)a5;
  (void)a6;
  return 0;
}

u64 sys_getegid(u64 a1, u64 a2, u64 a3, u64 a4, u64 a5, u64 a6)
{
  (void)a1;
  (void)a2;
  (void)a3;
  (void)a4;
  (void)a5;
  (void)a6;
  return 0;
}

u64 sys_set_tid_address(u64 tidptr, u64 a2, u64 a3, u64 a4, u64 a5, u64 a6)
{
  (void)tidptr;
  (void)a2;
  (void)a3;
  (void)a4;
  (void)a5;
  (void)a6;
  proc_t *p = proc_current();
  return p ? p->pid : 1;
}
