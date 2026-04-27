/**
 * @file src/kernel/sys/sys_proc.c
 * @brief Process-oriented syscall implementations.
 */

#include <alcor2/errno.h>
#include <alcor2/kstdlib.h>
#include <alcor2/proc/proc.h>
#include <alcor2/sys/internal.h>
#include <alcor2/fs/vfs.h>
#include <alcor2/mm/vmm.h>

#define MAX_EXEC_ARGS 32
#define MAX_ARG_LEN   256

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
  (void)flags;
  (void)child_stack;
  (void)ptid;
  (void)ctid;
  (void)tls;
  (void)a6;
  return (u64)-ENOSYS;
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

  i64 fd = vfs_open(path, 0);
  if(fd < 0)
    return (u64)-ENOENT;

  static char  arg_storage[MAX_EXEC_ARGS][MAX_ARG_LEN];
  static char *child_argv[MAX_EXEC_ARGS + 1];

  int argc = 0;
  kstrncpy(arg_storage[argc], path, MAX_ARG_LEN);
  child_argv[argc++] = arg_storage[0];

  char **user_argv = (char **)argv;
  if(user_argv) {
    for(int i = 0; user_argv[i] && argc < MAX_EXEC_ARGS; i++) {
      if(!user_cstr_ok((u64)user_argv[i]))
        return (u64)-EFAULT;
      kstrncpy(arg_storage[argc], user_argv[i], MAX_ARG_LEN);
      child_argv[argc] = arg_storage[argc];
      argc++;
    }
  }
  child_argv[argc] = NULL;

  u64 child_pid = proc_create(path, NULL, 0, fd, child_argv);
  vfs_close(fd);
  if(child_pid == 0)
    return (u64)-ENOMEM;

  return (u64)proc_wait(child_pid);
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
