/**
 * @file src/kernel/sys/sys_proc.c
 * @brief Process syscalls: PID queries, fork, exec, wait, clone, identity.
 *
 * All user pointers (path strings, argv/envp vectors, wait-status buffers)
 * are validated through the VMM before any kernel-side access.
 */

#include <alcor2/errno.h>
#include <alcor2/fs/vfs.h>
#include <alcor2/kstdlib.h>
#include <alcor2/mm/heap.h>
#include <alcor2/mm/vmm.h>
#include <alcor2/proc/proc.h>
#include <alcor2/sys/internal.h>

#define ALCOR_CLONE_VM     0x00000100u
#define ALCOR_CLONE_VFORK  0x00004000u
#define ALCOR_CLONE_THREAD 0x00010000u
#define ALCOR_CSIGNAL      0x000000ffu

#define MAX_EXEC_ARGS      PROC_MAX_ARGV
#define MAX_ARG_LEN        PROC_MAX_ARG_STRLEN

/** @brief Return @c true if @p ptr is a valid, non-NULL user-space pointer. */
static inline bool user_cstr_ok(u64 ptr)
{
  return ptr && vmm_is_user_ptr((void *)ptr);
}

/** @brief Return @c true if @p ptr..@p ptr+size is a valid user-space range. */
static inline bool user_buf_ok(u64 ptr, u64 size)
{
  return ptr && vmm_is_user_range((void *)ptr, size);
}

/**
 * @brief Copy a null-terminated user-space string vector into kernel storage.
 *
 * Reads up to @c MAX_EXEC_ARGS pointers from @p user_vec, validates each with
 * VMM, and copies the string contents into @p store.  Pointer entries are
 * written to @p ptrs (null-terminated).  @p *out_count receives the count.
 *
 * @return 0 on success, @c -EFAULT if any pointer fails validation.
 */
static i64 copy_user_strvec(
    char **user_vec, char store[][MAX_ARG_LEN], char *ptrs[], int *out_count
)
{
  int n = 0;
  if(user_vec) {
    for(; n < MAX_EXEC_ARGS; n++) {
      /* Validate the pointer slot BEFORE dereferencing it. Without this the
       * loop happily reads @c user_vec[n] from kernel memory once @c user_vec
       * crosses USER_SPACE_END mid-array, leaking those bytes into the new
       * process via @c store[n]. */
      if(!user_buf_ok((u64)&user_vec[n], sizeof(char *)))
        return -EFAULT;
      char *p = user_vec[n];
      if(!p)
        break;
      if(!user_cstr_ok((u64)p))
        return -EFAULT;
      kstrncpy(store[n], p, MAX_ARG_LEN);
      store[n][MAX_ARG_LEN - 1] = '\0';
      ptrs[n]                   = store[n];
    }
  }
  ptrs[n]    = NULL;
  *out_count = n;
  return 0;
}

/** @brief Return the calling process's PID (or 1 if no process is running). */
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

/** @brief Return the calling thread's TID — same as PID (no thread support). */
u64 sys_gettid(u64 a1, u64 a2, u64 a3, u64 a4, u64 a5, u64 a6)
{
  return sys_getpid(a1, a2, a3, a4, a5, a6);
}

/** @brief Return the calling process's parent PID. */
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

/**
 * @brief Duplicate the calling process (@c clone).
 *
 * Supports @c CLONE_VM | @c CLONE_VFORK | @c SIGCHLD (musl @c posix_spawn)
 * and the bare @c __clone (faccessat helper).  Full thread creation
 * (@c CLONE_THREAD) returns @c -ENOSYS.
 */
u64 sys_clone(u64 flags, u64 child_stack, u64 ptid, u64 ctid, u64 tls, u64 a6)
{
  (void)ptid;
  (void)ctid;
  (void)tls;
  (void)a6;

  if(flags & ALCOR_CLONE_THREAD)
    return (u64)-ENOSYS;

  if(flags & ~(ALCOR_CLONE_VM | ALCOR_CLONE_VFORK | ALCOR_CSIGNAL))
    return (u64)-EINVAL;

  const syscall_frame_t *frame = syscall_get_current_frame();
  if(!frame)
    return (u64)-EINVAL;

  if(child_stack != 0 && !vmm_is_user_ptr((void *)child_stack))
    return (u64)-EFAULT;

  return (u64)proc_clone(frame, child_stack, (u32)flags);
}

/** @brief Duplicate the calling process (@c fork). */
u64 sys_fork(u64 a1, u64 a2, u64 a3, u64 a4, u64 a5, u64 a6)
{
  (void)a1;
  (void)a2;
  (void)a3;
  (void)a4;
  (void)a5;
  (void)a6;

  const syscall_frame_t *frame = syscall_get_current_frame();
  if(!frame)
    return (u64)-EINVAL;
  return (u64)proc_fork(frame);
}

/**
 * @brief Replace the calling process image with a new ELF at @p pathname.
 *
 * Copies argv and envp into kernel heap buffers, opens the target file via
 * the VFS, loads it into the current process, closes O_CLOEXEC fds, wakes
 * any vfork-blocked parent, and redirects the in-flight syscall return frame
 * to the new entry point.
 */
u64 sys_execve(u64 pathname, u64 argv, u64 envp, u64 a4, u64 a5, u64 a6)
{
  (void)a4;
  (void)a5;
  (void)a6;

  if(!user_cstr_ok(pathname))
    return (u64)-EFAULT;
  const char *path = (const char *)pathname;

  if(argv && !user_buf_ok(argv, sizeof(char *)))
    return (u64)-EFAULT;
  if(envp && !user_buf_ok(envp, sizeof(char *)))
    return (u64)-EFAULT;

  vfs_stat_t st;
  if(vfs_stat(path, &st) < 0)
    return (u64)-ENOENT;
  if(st.type != VFS_FILE)
    return (u64)-EACCES;

  /* All five scratch buffers are freed at the single @c out: label so the
   * function has one cleanup path instead of five copies of five kfrees. */
  char(*arg_storage)[MAX_ARG_LEN] = kmalloc((u64)MAX_EXEC_ARGS * MAX_ARG_LEN);
  char **new_argv = kmalloc((u64)(MAX_EXEC_ARGS + 1) * sizeof(char *));
  char(*env_storage)[MAX_ARG_LEN] = kmalloc((u64)MAX_EXEC_ARGS * MAX_ARG_LEN);
  char **new_envp     = kmalloc((u64)(MAX_EXEC_ARGS + 1) * sizeof(char *));
  char  *name_storage = kmalloc(MAX_ARG_LEN);

  u64    rc_u = 0;
  i64    fd   = -1;

  if(!arg_storage || !new_argv || !env_storage || !new_envp || !name_storage) {
    rc_u = (u64)-ENOMEM;
    goto out;
  }

  int argc = 0, envc = 0;

  i64 rc_argv = copy_user_strvec((char **)argv, arg_storage, new_argv, &argc);
  if(rc_argv < 0) {
    rc_u = (u64)rc_argv;
    goto out;
  }
  if(argc == 0) {
    kstrncpy(arg_storage[0], path, MAX_ARG_LEN);
    arg_storage[0][MAX_ARG_LEN - 1] = '\0';
    new_argv[0]                     = arg_storage[0];
    new_argv[1]                     = NULL;
    argc                            = 1;
  }

  i64 rc_envp = copy_user_strvec((char **)envp, env_storage, new_envp, &envc);
  if(rc_envp < 0) {
    rc_u = (u64)rc_envp;
    goto out;
  }

  kstrncpy(name_storage, path, MAX_ARG_LEN);
  name_storage[MAX_ARG_LEN - 1] = '\0';

  fd = vfs_open(path, 0);
  if(fd < 0) {
    rc_u = (u64)-ENOENT;
    goto out;
  }

  proc_t *p = proc_current();
  if(!p) {
    rc_u = (u64)-EINVAL;
    goto out;
  }

  i64 rc = proc_exec_replace_image(p, name_storage, fd, new_argv, new_envp);
  vfs_close(fd);
  fd = -1;
  if(rc < 0)
    proc_exit(127);

  /* Close O_CLOEXEC fds, then wake any vfork-blocked parent. */
  vfs_proc_close_cloexec_fds();
  proc_notify_exec(p);

  /* Redirect the in-flight sysret to the new image entry. */
  syscall_frame_t *frame = syscall_get_current_frame();
  if(frame) {
    frame->rip    = p->user_rip;
    frame->rsp    = p->user_rsp;
    frame->rflags = p->user_rflags;
  }

out:
  if(fd >= 0)
    vfs_close(fd);
  kfree(arg_storage);
  kfree(new_argv);
  kfree(env_storage);
  kfree(new_envp);
  kfree(name_storage);
  return rc_u;
}

/** @brief Terminate the calling process with @p status. */
u64 sys_exit(u64 status, u64 a2, u64 a3, u64 a4, u64 a5, u64 a6)
{
  (void)a2;
  (void)a3;
  (void)a4;
  (void)a5;
  (void)a6;
  proc_exit((i64)status);
}

/** @brief Terminate all threads in the group — equivalent to ::sys_exit here.
 */
u64 sys_exit_group(u64 status, u64 a2, u64 a3, u64 a4, u64 a5, u64 a6)
{
  return sys_exit(status, a2, a3, a4, a5, a6);
}

/**
 * @brief Wait for a child process to change state (@c wait4).
 *
 * Delegates to ::proc_waitpid.  @p wstatus is written only when non-NULL and
 * the call returns a valid child PID.  @p rusage is ignored.
 */
u64 sys_wait4(u64 pid, u64 wstatus, u64 options, u64 rusage, u64 a5, u64 a6)
{
  (void)rusage;
  (void)a5;
  (void)a6;

  i32  kstatus     = 0;
  i32 *kstatus_ptr = wstatus ? &kstatus : NULL;

  i64  ret = proc_waitpid((i64)pid, kstatus_ptr, (i32)options);
  if(ret > 0 && wstatus) {
    if(!user_buf_ok(wstatus, sizeof(i32)))
      return (u64)-EFAULT;
    *(i32 *)wstatus = kstatus;
  }

  return (u64)ret;
}

/** @brief Return the calling process's real user ID (always 0 — root). */
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

/** @brief Return the calling process's real group ID (always 0 — root). */
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

/** @brief Return the calling process's effective user ID (always 0 — root). */
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

/** @brief Return the calling process's effective group ID (always 0 — root). */
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

/** @brief Store the clear-child-tid address (no-op); returns the current TID.
 */
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
