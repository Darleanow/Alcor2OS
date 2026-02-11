/**
 * @file src/kernel/syscalls.c
 * @brief Generic syscall implementations.
 */

#include <alcor2/console.h>
#include <alcor2/cpu.h>
#include <alcor2/elf.h>
#include <alcor2/errno.h>
#include <alcor2/heap.h>
#include <alcor2/keyboard.h>
#include <alcor2/kstdlib.h>
#include <alcor2/memory_layout.h>
#include <alcor2/pmm.h>
#include <alcor2/proc.h>
#include <alcor2/sched.h>
#include <alcor2/syscall.h>
#include <alcor2/user.h>
#include <alcor2/vfs.h>
#include <alcor2/vmm.h>

// Forward Declarations for Pipe Functions
static i64 pipe_read(int fd, void *buf, u64 count);
static i64 pipe_write(int fd, const void *buf, u64 count);
static int pipe_close(int fd);

// External arch-specific syscalls
extern u64 sys_arch_prctl(u64 code, u64 addr, u64 a3, u64 a4, u64 a5, u64 a6);

/**
 * @brief Read data from a file descriptor.
 *
 * @param fd    File descriptor.
 * @param buf   User buffer to read into.
 * @param count Maximum bytes to read.
 * @return Bytes read, or negative errno.
 */
static u64 sys_read(u64 fd, u64 buf, u64 count, u64 a4, u64 a5, u64 a6)
{
  (void)a4;
  (void)a5;
  (void)a6;

  if(!buf)
    return (u64)-EFAULT;
  if(!vmm_is_user_range((void *)buf, count))
    return (u64)-EFAULT;
  if(count == 0)
    return 0;

  /* stdin - read from keyboard with blocking */
  if(fd == 0) {
    char *user_buf = (char *)buf;
    while(!keyboard_has_data()) {
      cpu_enable_interrupts();
      __asm__ volatile("hlt");
      cpu_disable_interrupts();
    }
    return keyboard_read(user_buf, count);
  }

  /* Check if it's a pipe */
  i64 pipe_result = pipe_read((int)fd, (void *)buf, count);
  if(pipe_result != -ENOENT) {
    return (u64)pipe_result;
  }

  /* Regular file descriptor */
  i64 result = vfs_read((i64)fd, (void *)buf, count);
  return (u64)result;
}

/**
 * @brief Write data to a file descriptor.
 *
 * @param fd    File descriptor.
 * @param buf   User buffer to write from.
 * @param count Number of bytes to write.
 * @return Bytes written, or negative errno.
 */
static u64 sys_write(u64 fd, u64 buf, u64 count, u64 a4, u64 a5, u64 a6)
{
  (void)a4;
  (void)a5;
  (void)a6;

  if(!buf)
    return (u64)-EFAULT;
  if(!vmm_is_user_range((void *)buf, count))
    return (u64)-EFAULT;
  if(count == 0)
    return 0;

  /* stdout/stderr - write to console */
  if(fd == 1 || fd == 2) {
    const char *str = (const char *)buf;
    for(u64 i = 0; i < count; i++) {
      console_putchar(str[i]);
    }
    return count;
  }

  /* Check if it's a pipe */
  i64 pipe_result = pipe_write((int)fd, (const void *)buf, count);
  if(pipe_result != -ENOENT) {
    return (u64)pipe_result;
  }

  /* Regular file descriptor */
  i64 result = vfs_write((i64)fd, (void *)buf, count);
  return (u64)result;
}

/**
 * @brief Open a file or directory.
 *
 * @param path  User pointer to pathname.
 * @param flags Open flags (O_RDONLY, O_CREAT, etc.).
 * @param mode  File mode (unused).
 * @return File descriptor, or negative errno.
 */
static u64 sys_open(u64 path, u64 flags, u64 mode, u64 a4, u64 a5, u64 a6)
{
  (void)mode;
  (void)a4;
  (void)a5;
  (void)a6;

  if(!path)
    return (u64)-EFAULT;
  if(!vmm_is_user_ptr((void *)path))
    return (u64)-EFAULT;

  i64 fd = vfs_open((const char *)path, (u32)flags);
  if(fd < 0)
    return (u64)-ENOENT;
  return (u64)fd;
}

/**
 * @brief Close a file descriptor.
 *
 * @param fd File descriptor to close.
 * @return 0 on success, negative errno on error.
 */
static u64 sys_close(u64 fd, u64 a2, u64 a3, u64 a4, u64 a5, u64 a6)
{
  (void)a2;
  (void)a3;
  (void)a4;
  (void)a5;
  (void)a6;

  if(fd <= 2)
    return 0;

  if(pipe_close((int)fd) == 0) {
    return 0;
  }

  i64 result = vfs_close((i64)fd);
  return (result < 0) ? (u64)-EBADF : 0;
}

struct linux_stat
{
  u64 st_dev;
  u64 st_ino;
  u64 st_nlink;
  u32 st_mode;
  u32 st_uid;
  u32 st_gid;
  u32 pad0;
  u64 st_rdev;
  i64 st_size;
  i64 st_blksize;
  i64 st_blocks;
  u64 st_atime;
  u64 st_atime_nsec;
  u64 st_mtime;
  u64 st_mtime_nsec;
  u64 st_ctime;
  u64 st_ctime_nsec;
  i64 unused[3];
};

#define S_IFMT  0170000
#define S_IFDIR 0040000
#define S_IFREG 0100000

/**
 * @brief Get file status by path.
 *
 * @param path    User pointer to pathname.
 * @param statbuf User pointer to stat structure.
 * @return 0 on success, negative errno on error.
 */
static u64 sys_stat(u64 path, u64 statbuf, u64 a3, u64 a4, u64 a5, u64 a6)
{
  (void)a3;
  (void)a4;
  (void)a5;
  (void)a6;

  if(!path || !statbuf)
    return (u64)-EFAULT;

  vfs_stat_t vst;
  if(vfs_stat((const char *)path, &vst) < 0) {
    return (u64)-ENOENT;
  }

  struct linux_stat *st = (struct linux_stat *)statbuf;
  kzero(st, sizeof(*st));

  st->st_ino   = 1;
  st->st_nlink = 1;
  st->st_mode =
      (vst.type == VFS_DIRECTORY) ? (S_IFDIR | 0755) : (S_IFREG | 0644);
  st->st_size    = (i64)vst.size;
  st->st_blksize = 4096;
  st->st_blocks  = ((i64)vst.size + 511) / 512;

  return 0;
}

/** @brief Get file status by fd.
 * @param fd      File descriptor.
 * @param statbuf User pointer to stat structure.
 * @return 0 on success, negative errno on error.
 */
static u64 sys_fstat(u64 fd, u64 statbuf, u64 a3, u64 a4, u64 a5, u64 a6)
{
  (void)a3;
  (void)a4;
  (void)a5;
  (void)a6;

  if(!statbuf)
    return (u64)-EFAULT;

  struct linux_stat *st = (struct linux_stat *)statbuf;
  kzero(st, sizeof(*st));

  if(fd <= 2) {
    st->st_mode    = 0020000 | 0666; /* S_IFCHR */
    st->st_blksize = 4096;
    return 0;
  }

  st->st_mode    = S_IFREG | 0644;
  st->st_blksize = 4096;
  return 0;
}

/** @brief Get file status (no symlink follow, delegates to sys_stat). */
static u64 sys_lstat(u64 path, u64 statbuf, u64 a3, u64 a4, u64 a5, u64 a6)
{
  return sys_stat(path, statbuf, a3, a4, a5, a6);
}

/**
 * @brief Reposition file read/write offset.
 *
 * @param fd     File descriptor.
 * @param offset Seek offset.
 * @param whence Seek mode (SEEK_SET, SEEK_CUR, SEEK_END).
 * @return New offset, or negative errno.
 */
static u64 sys_lseek(u64 fd, u64 offset, u64 whence, u64 a4, u64 a5, u64 a6)
{
  (void)a4;
  (void)a5;
  (void)a6;
  i64 result = vfs_seek((i64)fd, (i64)offset, (i32)whence);
  return (u64)result;
}

#define MAP_ANONYMOUS 0x20

/**
 * @brief Map anonymous memory into the process address space.
 *
 * @param addr   Requested address (hint, ignored).
 * @param length Size in bytes.
 * @param prot   Protection flags (ignored).
 * @param flags  Mapping flags (MAP_ANONYMOUS required).
 * @param fd     File descriptor (must be -1 for anon).
 * @param offset File offset (ignored).
 * @return Mapped address, or negative errno.
 */
static u64
    sys_mmap(u64 addr, u64 length, u64 prot, u64 flags, u64 fd, u64 offset)
{
  (void)addr;
  (void)prot;
  (void)offset;

  if(length == 0)
    return (u64)-EINVAL;
  if(!(flags & MAP_ANONYMOUS) && fd != (u64)-1) {
    return (u64)-ENOSYS;
  }

  proc_t *p = proc_current();
  if(!p)
    return (u64)-ENOMEM;

  u64 aligned_len = (length + 0xFFF) & ~0xFFFUL;
  u64 result      = p->heap_break;

  for(u64 off = 0; off < aligned_len; off += 0x1000) {
    u64 phys = (u64)pmm_alloc();
    if(!phys)
      return (u64)-ENOMEM;
    vmm_map(result + off, phys, VMM_PRESENT | VMM_WRITE | VMM_USER);

    u8 *page = (u8 *)(phys + vmm_get_hhdm());
    kzero(page, 0x1000);
  }

  p->heap_break += aligned_len;
  return result;
}

/** @brief Change memory protection (stub, always succeeds). */
static u64 sys_mprotect(u64 addr, u64 len, u64 prot, u64 a4, u64 a5, u64 a6)
{
  (void)addr;
  (void)len;
  (void)prot;
  (void)a4;
  (void)a5;
  (void)a6;
  return 0;
}

/** @brief Unmap memory (stub, always succeeds). */
static u64 sys_munmap(u64 addr, u64 len, u64 a3, u64 a4, u64 a5, u64 a6)
{
  (void)addr;
  (void)len;
  (void)a3;
  (void)a4;
  (void)a5;
  (void)a6;
  return 0;
}

/**
 * @brief Adjust the program break.
 *
 * @param addr New break address, or 0 to query current break.
 * @return Current or new break address.
 */
static u64 sys_brk(u64 addr, u64 a2, u64 a3, u64 a4, u64 a5, u64 a6)
{
  (void)a2;
  (void)a3;
  (void)a4;
  (void)a5;
  (void)a6;

  proc_t *p = proc_current();
  if(!p)
    return 0;

  if(addr == 0)
    return p->program_break;

  if(addr > p->program_break) {
    u64 old_end = (p->program_break + 0xFFF) & ~0xFFFUL;
    u64 new_end = (addr + 0xFFF) & ~0xFFFUL;

    for(u64 page_addr = old_end; page_addr < new_end; page_addr += 0x1000) {
      u64 phys = (u64)pmm_alloc();
      if(!phys)
        return p->program_break;
      vmm_map(page_addr, phys, VMM_PRESENT | VMM_WRITE | VMM_USER);

      u8 *page_ptr = (u8 *)(phys + vmm_get_hhdm());
      kzero(page_ptr, 0x1000);
    }
    p->program_break = addr;
  }
  return p->program_break;
}

#define TCGETS     0x5401
#define TCSETS     0x5402
#define TIOCGWINSZ 0x5413

/**
 * @brief Handle terminal ioctl requests.
 *
 * @param fd      File descriptor.
 * @param request Ioctl request code.
 * @param arg     Request argument.
 * @return 0 on success.
 */
static u64 sys_ioctl(u64 fd, u64 request, u64 arg, u64 a4, u64 a5, u64 a6)
{
  (void)a4;
  (void)a5;
  (void)a6;

  if(fd <= 2) {
    switch(request) {
    case TIOCGWINSZ: {
      struct
      {
        u16 row, col, x, y;
      } *ws = (void *)arg;
      if(ws) {
        ws->row = 25;
        ws->col = 80;
        ws->x   = 0;
        ws->y   = 0;
      }
      return 0;
    }
    case TCGETS:
    case TCSETS:
      return 0;
    default:
      break;
    }
  }
  return 0;
}

/**
 * @brief Check file accessibility.
 *
 * @param path Path to check.
 * @param mode Access mode (ignored).
 * @return 0 if accessible, negative errno otherwise.
 */
static u64 sys_access(u64 path, u64 mode, u64 a3, u64 a4, u64 a5, u64 a6)
{
  (void)mode;
  (void)a3;
  (void)a4;
  (void)a5;
  (void)a6;
  if(!path)
    return (u64)-EFAULT;
  vfs_stat_t st;
  if(vfs_stat((const char *)path, &st) < 0)
    return (u64)-ENOENT;
  return 0;
}

/* Pipe Implementation */
#define PIPE_BUF_SIZE 4096
typedef struct pipe
{
  u8  buffer[PIPE_BUF_SIZE];
  u64 read_pos;
  u64 write_pos;
  u64 count;
  int read_fd;
  int write_fd;
  int read_open;
  int write_open;
} pipe_t;

#define MAX_PIPES 16
static pipe_t pipes[MAX_PIPES];
static int    pipes_initialized = 0;

/**
 * @brief Find a pipe associated with a file descriptor.
 *
 * @param fd      File descriptor to look up.
 * @param is_read Output: 1 if read end, 0 if write end.
 * @return Pipe pointer, or NULL if not found.
 */
static pipe_t *find_pipe_by_fd(int fd, int *is_read)
{
  for(int i = 0; i < MAX_PIPES; i++) {
    if(pipes[i].read_open && pipes[i].read_fd == fd) {
      *is_read = 1;
      return &pipes[i];
    }
    if(pipes[i].write_open && pipes[i].write_fd == fd) {
      *is_read = 0;
      return &pipes[i];
    }
  }
  return NULL;
}

/**
 * @brief Allocate a free pipe from the pool.
 * @return Pipe pointer, or NULL if none available.
 */
static pipe_t *alloc_pipe(void)
{
  if(!pipes_initialized) {
    kzero(pipes, sizeof(pipes));
    pipes_initialized = 1;
  }
  for(int i = 0; i < MAX_PIPES; i++) {
    if(!pipes[i].read_open && !pipes[i].write_open) {
      pipes[i].read_pos = pipes[i].write_pos = pipes[i].count = 0;
      return &pipes[i];
    }
  }
  return NULL;
}

/**
 * @brief Create a unidirectional pipe.
 *
 * @param pipefd User pointer to int[2] for read/write fds.
 * @return 0 on success, negative errno on error.
 */
static u64 sys_pipe(u64 pipefd, u64 a2, u64 a3, u64 a4, u64 a5, u64 a6)
{
  (void)a2;
  (void)a3;
  (void)a4;
  (void)a5;
  (void)a6;

  if(!pipefd)
    return (u64)-EFAULT;
  int    *fds = (int *)pipefd;
  pipe_t *p   = alloc_pipe();
  if(!p)
    return (u64)-ENOMEM;

  const proc_t *proc = proc_current();
  if(!proc)
    return (u64)-EINVAL;

  int read_fd = -1, write_fd = -1;
  for(int fd = 100; fd < 200; fd++) {
    int in_use = 0;
    for(int j = 0; j < MAX_PIPES; j++) {
      if((pipes[j].read_open && pipes[j].read_fd == fd) ||
         (pipes[j].write_open && pipes[j].write_fd == fd)) {
        in_use = 1;
        break;
      }
    }
    if(!in_use) {
      if(read_fd == -1)
        read_fd = fd;
      else if(write_fd == -1) {
        write_fd = fd;
        break;
      }
    }
  }

  if(read_fd == -1 || write_fd == -1)
    return (u64)-EMFILE;

  p->read_fd    = read_fd;
  p->write_fd   = write_fd;
  p->read_open  = 1;
  p->write_open = 1;
  fds[0]        = read_fd;
  fds[1]        = write_fd;
  return 0;
}

static i64 pipe_read(int fd, void *buf, u64 count)
{
  int     is_read;
  pipe_t *p = find_pipe_by_fd(fd, &is_read);
  if(!p || !is_read)
    return -ENOENT;
  if(!p->read_open)
    return -EBADF;
  if(p->count == 0 && !p->write_open)
    return 0;

  while(p->count == 0 && p->write_open) {
    __asm__ volatile("pause");
  }

  u64 to_read = (count > p->count) ? p->count : count;
  u8 *dst     = (u8 *)buf;
  for(u64 i = 0; i < to_read; i++) {
    dst[i]      = p->buffer[p->read_pos];
    p->read_pos = (p->read_pos + 1) % PIPE_BUF_SIZE;
  }
  p->count -= to_read;
  return (i64)to_read;
}

static i64 pipe_write(int fd, const void *buf, u64 count)
{
  int     is_read;
  pipe_t *p = find_pipe_by_fd(fd, &is_read);
  if(!p || is_read)
    return -ENOENT;
  if(!p->write_open)
    return -EBADF;
  if(!p->read_open)
    return -EPIPE;

  /* cppcheck-suppress knownConditionTrueFalse */
  while(p->count >= PIPE_BUF_SIZE && p->read_open) {
    __asm__ volatile("pause");
  }

  u64       space    = PIPE_BUF_SIZE - p->count;
  u64       to_write = (count > space) ? space : count;
  const u8 *src      = (const u8 *)buf;
  for(u64 i = 0; i < to_write; i++) {
    p->buffer[p->write_pos] = src[i];
    p->write_pos            = (p->write_pos + 1) % PIPE_BUF_SIZE;
  }
  p->count += to_write;
  return (i64)to_write;
}

/**
 * @brief Close one end of a pipe.
 *
 * @param fd File descriptor to close.
 * @return 0 on success, -ENOENT if not a pipe fd.
 */
static int pipe_close(int fd)
{
  int     is_read;
  pipe_t *p = find_pipe_by_fd(fd, &is_read);
  if(!p)
    return -ENOENT;
  if(is_read)
    p->read_open = 0;
  else
    p->write_open = 0;
  return 0;
}

/** @brief Duplicate a file descriptor (stub, returns oldfd). */
static u64 sys_dup(u64 oldfd, u64 a2, u64 a3, u64 a4, u64 a5, u64 a6)
{
  (void)a2;
  (void)a3;
  (void)a4;
  (void)a5;
  (void)a6;
  return oldfd;
}

/** @brief Duplicate fd to a specific number (stub, returns newfd). */
static u64 sys_dup2(u64 oldfd, u64 newfd, u64 a3, u64 a4, u64 a5, u64 a6)
{
  (void)oldfd;
  (void)a3;
  (void)a4;
  (void)a5;
  (void)a6;
  return newfd;
}

/**
 * @brief Sleep for the specified duration.
 *
 * @param req User pointer to timespec (seconds + nanoseconds).
 * @param rem User pointer for remaining time (unused).
 * @return 0 on success, negative errno on error.
 */
static u64 sys_nanosleep(u64 req, u64 rem, u64 a3, u64 a4, u64 a5, u64 a6)
{
  (void)rem;
  (void)a3;
  (void)a4;
  (void)a5;
  (void)a6;
  if(!req)
    return (u64)-EFAULT;
  struct
  {
    i64 sec;
    i64 nsec;
  }  *ts    = (void *)req;
  u64 ms    = (u64)ts->sec * 1000 + (u64)ts->nsec / 1000000;
  u64 ticks = (ms + 9) / 10;
  if(ticks == 0)
    ticks = 1;
  for(u64 i = 0; i < ticks; i++) {
    cpu_enable_interrupts();
    __asm__ volatile("hlt");
    cpu_disable_interrupts();
  }
  return 0;
}

/** @brief Get current process ID. */
static u64 sys_getpid(u64 a1, u64 a2, u64 a3, u64 a4, u64 a5, u64 a6)
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

/** @brief Get thread ID (same as PID, no threading). */
static u64 sys_gettid(u64 a1, u64 a2, u64 a3, u64 a4, u64 a5, u64 a6)
{
  return sys_getpid(a1, a2, a3, a4, a5, a6);
}

/** @brief Get parent process ID. */
static u64 sys_getppid(u64 a1, u64 a2, u64 a3, u64 a4, u64 a5, u64 a6)
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

static syscall_frame_t *current_syscall_frame = NULL;

/** @brief Fork the current process.
 * @return Child PID in parent, 0 in child, negative errno on error.
 */
static u64 sys_fork(u64 a1, u64 a2, u64 a3, u64 a4, u64 a5, u64 a6)
{
  (void)a1;
  (void)a2;
  (void)a3;
  (void)a4;
  (void)a5;
  (void)a6;
  if(!current_syscall_frame)
    return (u64)-EINVAL;
  return (u64)proc_fork(current_syscall_frame);
}

#define MAX_EXEC_ARGS 32
#define MAX_ARG_LEN   256

/**
 * @brief Execute a program, replacing the current process image.
 *
 * @param pathname User pointer to executable path.
 * @param argv     User pointer to argument array.
 * @param envp     User pointer to environment (ignored).
 * @return Does not return on success; negative errno on error.
 */
static u64 sys_execve(u64 pathname, u64 argv, u64 envp, u64 a4, u64 a5, u64 a6)
{
  (void)envp;
  (void)a4;
  (void)a5;
  (void)a6;
  if(!pathname)
    return (u64)-EFAULT;

  const char *path = (const char *)pathname;
  if(!vmm_is_user_ptr((const void *)path))
    return (u64)-EFAULT;

  char **user_argv = (char **)argv;
  if(argv && !vmm_is_user_ptr((const void *)argv))
    return (u64)-EFAULT;

  vfs_stat_t st;
  if(vfs_stat(path, &st) < 0)
    return (u64)-ENOENT;
  if(st.type != VFS_FILE)
    return (u64)-EACCES;

  void *elf_data = kmalloc(st.size + 1);
  if(!elf_data)
    return (u64)-ENOMEM;

  i64 fd = vfs_open(path, 0);
  if(fd < 0) {
    kfree(elf_data);
    return (u64)-ENOENT;
  }
  i64 bytes = vfs_read(fd, elf_data, st.size);
  vfs_close(fd);

  if(bytes != (i64)st.size) {
    kfree(elf_data);
    return (u64)-EIO;
  }

  static char  arg_storage[MAX_EXEC_ARGS][MAX_ARG_LEN];
  static char *child_argv[MAX_EXEC_ARGS + 1];

  int          argc = 0;
  kstrncpy(arg_storage[argc], path, MAX_ARG_LEN);
  child_argv[argc++] = arg_storage[0];

  if(user_argv) {
    for(int i = 0; user_argv[i] && argc < MAX_EXEC_ARGS; i++) {
      kstrncpy(arg_storage[argc], user_argv[i], MAX_ARG_LEN);
      child_argv[argc] = arg_storage[argc];
      argc++;
    }
  }
  child_argv[argc] = NULL;

  u64 child_pid = proc_create(path, elf_data, st.size, child_argv);
  kfree(elf_data);

  if(child_pid == 0)
    return (u64)-ENOMEM;

  i64 exit_code = proc_wait(child_pid);
  return (u64)exit_code;
}

/**
 * @brief Terminate the current process.
 *
 * @param status Exit status code.
 */
static u64 sys_exit(u64 status, u64 a2, u64 a3, u64 a4, u64 a5, u64 a6)
{
  (void)a2;
  (void)a3;
  (void)a4;
  (void)a5;
  (void)a6;
  proc_exit((i64)status);
}

/**
 * @brief Wait for a child process to change state.
 *
 * @param pid     Child PID to wait for.
 * @param wstatus User pointer for exit status.
 * @param options Wait options.
 * @param rusage  Resource usage (ignored).
 * @return Child PID, or negative errno.
 */
static u64
    sys_wait4(u64 pid, u64 wstatus, u64 options, u64 rusage, u64 a5, u64 a6)
{
  (void)rusage;
  (void)a5;
  (void)a6;
  return (u64)proc_waitpid((i64)pid, (i32 *)wstatus, (i32)options);
}

/**
 * @brief Get system identification.
 *
 * @param buf User pointer to utsname structure.
 * @return 0 on success, negative errno on error.
 */
static u64 sys_uname(u64 buf, u64 a2, u64 a3, u64 a4, u64 a5, u64 a6)
{
  (void)a2;
  (void)a3;
  (void)a4;
  (void)a5;
  (void)a6;
  if(!buf)
    return (u64)-EFAULT;

  struct utsname
  {
    char sysname[65];
    char nodename[65];
    char release[65];
    char version[65];
    char machine[65];
  } *u = (void *)buf;

  kzero(u, sizeof(*u));
  kstrncpy(u->sysname, "Alcor2", 65);
  kstrncpy(u->nodename, "alcor2", 65);
  kstrncpy(u->release, "0.1.0", 65);
  kstrncpy(u->version, "Alcor2 OS", 65);
  kstrncpy(u->machine, "x86_64", 65);
  return 0;
}

/** @brief File control operations (stub, always returns 0). */
static u64 sys_fcntl(u64 fd, u64 cmd, u64 arg, u64 a4, u64 a5, u64 a6)
{
  (void)fd;
  (void)cmd;
  (void)arg;
  (void)a4;
  (void)a5;
  (void)a6;
  return 0;
}

/**
 * @brief Read directory entries.
 *
 * @param fd    Directory file descriptor.
 * @param dirp  User buffer for dirent structures.
 * @param count Buffer size in bytes.
 * @return Bytes written to buffer, or negative errno.
 */
static u64 sys_getdents(u64 fd, u64 dirp, u64 count, u64 a4, u64 a5, u64 a6)
{
  (void)a4;
  (void)a5;
  (void)a6;
  if(!dirp)
    return (u64)-EFAULT;
  if(!vmm_is_user_range((void *)dirp, count))
    return (u64)-EFAULT;
  if(count < 32)
    return (u64)-EINVAL;
  i64 result = vfs_getdents((i64)fd, (void *)dirp, count);
  return (result < 0) ? (u64)-EBADF : (u64)result;
}

/** @brief Read directory entries (64-bit, delegates to sys_getdents). */
static u64 sys_getdents64(u64 fd, u64 dirp, u64 count, u64 a4, u64 a5, u64 a6)
{
  return sys_getdents(fd, dirp, count, a4, a5, a6);
}

/**
 * @brief Get current working directory.
 *
 * @param buf  User buffer for path.
 * @param size Buffer size.
 * @return Length including null, or negative errno.
 */
static u64 sys_getcwd(u64 buf, u64 size, u64 a3, u64 a4, u64 a5, u64 a6)
{
  (void)a3;
  (void)a4;
  (void)a5;
  (void)a6;
  if(!buf)
    return (u64)-EFAULT;
  if(size == 0)
    return (u64)-EINVAL;
  if(!vmm_is_user_range((void *)buf, size))
    return (u64)-EFAULT;
  const char *cwd = vfs_getcwd();
  u64         len = kstrlen(cwd);
  if(len + 1 > size)
    return (u64)-ERANGE;
  kstrncpy((char *)buf, cwd, size);
  return len + 1;
}

/**
 * @brief Change current working directory.
 *
 * @param path User pointer to directory path.
 * @return 0 on success, negative errno on error.
 */
static u64 sys_chdir(u64 path, u64 a2, u64 a3, u64 a4, u64 a5, u64 a6)
{
  (void)a2;
  (void)a3;
  (void)a4;
  (void)a5;
  (void)a6;
  if(!path)
    return (u64)-EFAULT;
  i64 result = vfs_chdir((const char *)path);
  return (result < 0) ? (u64)-ENOENT : 0;
}

/**
 * @brief Create a directory.
 *
 * @param pathname User pointer to directory path.
 * @param mode     Directory permissions (ignored).
 * @return 0 on success, negative errno on error.
 */
static u64 sys_mkdir(u64 pathname, u64 mode, u64 a3, u64 a4, u64 a5, u64 a6)
{
  (void)mode;
  (void)a3;
  (void)a4;
  (void)a5;
  (void)a6;
  if(!pathname)
    return (u64)-EFAULT;
  i64 result = vfs_mkdir((const char *)pathname);
  return (result < 0) ? (u64)-ENOENT : 0;
}

/**
 * @brief Remove a file or directory.
 *
 * @param pathname User pointer to path.
 * @return 0 on success, negative errno on error.
 */
static u64 sys_unlink(u64 pathname, u64 a2, u64 a3, u64 a4, u64 a5, u64 a6)
{
  (void)a2;
  (void)a3;
  (void)a4;
  (void)a5;
  (void)a6;
  if(!pathname)
    return (u64)-EFAULT;
  i64 result = vfs_unlink((const char *)pathname);
  return (result < 0) ? (u64)-ENOENT : 0;
}

/** @brief Read symbolic link (stub, returns -EINVAL). */
static u64 sys_readlink(u64 path, u64 buf, u64 bufsiz, u64 a4, u64 a5, u64 a6)
{
  (void)path;
  (void)buf;
  (void)bufsiz;
  (void)a4;
  (void)a5;
  (void)a6;
  return (u64)-EINVAL;
}

/** @brief Get user ID (always 0). */
static u64 sys_getuid(u64 a1, u64 a2, u64 a3, u64 a4, u64 a5, u64 a6)
{
  (void)a1;
  (void)a2;
  (void)a3;
  (void)a4;
  (void)a5;
  (void)a6;
  return 0;
}
/** @brief Get group ID (always 0). */
static u64 sys_getgid(u64 a1, u64 a2, u64 a3, u64 a4, u64 a5, u64 a6)
{
  (void)a1;
  (void)a2;
  (void)a3;
  (void)a4;
  (void)a5;
  (void)a6;
  return 0;
}
/** @brief Get effective user ID (always 0). */
static u64 sys_geteuid(u64 a1, u64 a2, u64 a3, u64 a4, u64 a5, u64 a6)
{
  (void)a1;
  (void)a2;
  (void)a3;
  (void)a4;
  (void)a5;
  (void)a6;
  return 0;
}
/** @brief Get effective group ID (always 0). */
static u64 sys_getegid(u64 a1, u64 a2, u64 a3, u64 a4, u64 a5, u64 a6)
{
  (void)a1;
  (void)a2;
  (void)a3;
  (void)a4;
  (void)a5;
  (void)a6;
  return 0;
}
/** @brief Futex operation (stub, always returns 0). */
static u64
    sys_futex(u64 uaddr, u64 op, u64 val, u64 timeout, u64 uaddr2, u64 val3)
{
  (void)uaddr;
  (void)op;
  (void)val;
  (void)timeout;
  (void)uaddr2;
  (void)val3;
  return 0;
}
/** @brief Set thread ID address (stub, returns current PID). */
static u64
    sys_set_tid_address(u64 tidptr, u64 a2, u64 a3, u64 a4, u64 a5, u64 a6)
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
/**
 * @brief Get clock time (stub, returns zero time).
 *
 * @param clk Clock ID (ignored).
 * @param tp  User pointer to timespec output.
 * @return 0 on success, negative errno on error.
 */
static u64 sys_clock_gettime(u64 clk, u64 tp, u64 a3, u64 a4, u64 a5, u64 a6)
{
  (void)clk;
  (void)a3;
  (void)a4;
  (void)a5;
  (void)a6;
  if(!tp)
    return (u64)-EFAULT;
  struct
  {
    i64 s;
    i64 ns;
  } *ts  = (void *)tp;
  ts->s  = 0;
  ts->ns = 0;
  return 0;
}
/** @brief Yield the CPU voluntarily. */
static u64 sys_sched_yield(u64 a1, u64 a2, u64 a3, u64 a4, u64 a5, u64 a6)
{
  (void)a1;
  (void)a2;
  (void)a3;
  (void)a4;
  (void)a5;
  (void)a6;
  sched_yield();
  return 0;
}

struct iovec
{
  void *iov_base;
  u64   iov_len;
};
/**
 * @brief Write from scatter/gather vectors.
 *
 * @param fd     File descriptor.
 * @param iov    User pointer to iovec array.
 * @param iovcnt Number of iovec elements.
 * @return Total bytes written, or negative errno.
 */
static u64 sys_writev(u64 fd, u64 iov, u64 iovcnt, u64 a4, u64 a5, u64 a6)
{
  (void)a4;
  (void)a5;
  (void)a6;
  if(!iov)
    return (u64)-EFAULT;
  const struct iovec *vec   = (const struct iovec *)iov;
  u64                 total = 0;
  for(u64 i = 0; i < iovcnt; i++) {
    if(vec[i].iov_base && vec[i].iov_len > 0) {
      u64 result = sys_write(fd, (u64)vec[i].iov_base, vec[i].iov_len, 0, 0, 0);
      if((i64)result < 0)
        return result;
      total += result;
    }
  }
  return total;
}

typedef u64         (*syscall_fn_t)(u64, u64, u64, u64, u64, u64);

static syscall_fn_t syscall_table[SYS_MAX] = {
    [SYS_READ]            = sys_read,
    [SYS_WRITE]           = sys_write,
    [SYS_OPEN]            = sys_open,
    [SYS_CLOSE]           = sys_close,
    [SYS_STAT]            = sys_stat,
    [SYS_FSTAT]           = sys_fstat,
    [SYS_LSTAT]           = sys_lstat,
    [SYS_LSEEK]           = sys_lseek,
    [SYS_IOCTL]           = sys_ioctl,
    [20]                  = sys_writev,
    [SYS_ACCESS]          = sys_access,
    [SYS_PIPE]            = sys_pipe,
    [SYS_DUP]             = sys_dup,
    [SYS_DUP2]            = sys_dup2,
    [SYS_FCNTL]           = sys_fcntl,
    [SYS_READLINK]        = sys_readlink,
    [SYS_MMAP]            = sys_mmap,
    [SYS_MPROTECT]        = sys_mprotect,
    [SYS_MUNMAP]          = sys_munmap,
    [SYS_BRK]             = sys_brk,
    [SYS_GETPID]          = sys_getpid,
    [SYS_FORK]            = sys_fork,
    [SYS_EXECVE]          = sys_execve,
    [SYS_EXIT]            = sys_exit,
    [SYS_WAIT4]           = sys_wait4,
    [SYS_UNAME]           = sys_uname,
    [SYS_GETPPID]         = sys_getppid,
    [SYS_GETUID]          = sys_getuid,
    [SYS_GETGID]          = sys_getgid,
    [SYS_GETEUID]         = sys_geteuid,
    [SYS_GETEGID]         = sys_getegid,
    [SYS_GETTID]          = sys_gettid,
    [SYS_SET_TID_ADDRESS] = sys_set_tid_address,
    [SYS_EXIT_GROUP]      = sys_exit,
    [SYS_SCHED_YIELD]     = sys_sched_yield,
    [SYS_NANOSLEEP]       = sys_nanosleep,
    [SYS_CLOCK_GETTIME]   = sys_clock_gettime,
    [SYS_FUTEX]           = sys_futex,
    [SYS_GETDENTS]        = sys_getdents,
    [SYS_GETCWD]          = sys_getcwd,
    [SYS_CHDIR]           = sys_chdir,
    [SYS_MKDIR]           = sys_mkdir,
    [SYS_UNLINK]          = sys_unlink,
    [SYS_GETDENTS64]      = sys_getdents64,
    [SYS_ARCH_PRCTL]      = sys_arch_prctl
};

/**
 * @brief Dispatch a syscall from the saved register frame.
 *
 * @param frame Pointer to saved syscall registers.
 * @return Syscall return value.
 */
// cppcheck-suppress unusedFunction
u64 syscall_dispatch(syscall_frame_t *frame)
{
  u64 num = frame->rax;
  if(num >= SYS_MAX || syscall_table[num] == NULL) {
    return (u64)-ENOSYS;
  }
  current_syscall_frame = frame;
  u64 result            = syscall_table[num](
      frame->rdi, frame->rsi, frame->rdx, frame->r10, frame->r8, frame->r9
  );
  current_syscall_frame = NULL;
  return result;
}
