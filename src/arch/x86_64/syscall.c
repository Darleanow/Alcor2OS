/**
 * @file syscall.c
 * @brief Alcor2 Linux-compatible Syscall Implementation
 *
 * This file implements a Linux x86_64 compatible syscall interface.
 * Syscall numbers and semantics follow the Linux ABI to ensure
 * compatibility with musl libc and standard POSIX applications.
 *
 * @note Calling convention (System V AMD64):
 *   - Syscall number: RAX
 *   - Arguments: RDI, RSI, RDX, R10, R8, R9
 *   - Return value: RAX (negative values indicate errors as -errno)
 *   - Clobbered by kernel: RCX, R11
 */

#include <alcor2/console.h>
#include <alcor2/cpu.h>
#include <alcor2/elf.h>
#include <alcor2/errno.h>
#include <alcor2/heap.h>
#include <alcor2/keyboard.h>
#include <alcor2/memory_layout.h>
#include <alcor2/pmm.h>
#include <alcor2/proc.h>
#include <alcor2/sched.h>
#include <alcor2/syscall.h>
#include <alcor2/user.h>
#include <alcor2/vfs.h>
#include <alcor2/vmm.h>

/**
 * @brief Zero-fill a memory region
 * @param dst Destination pointer
 * @param n Number of bytes to zero
 */
static void memzero(void *dst, u64 n)
{
  u8 *d = (u8 *)dst;
  while(n--)
    *d++ = 0;
}

/**
 * @brief Copy a string
 * @param dst Destination buffer
 * @param src Source string
 * @param max Maximum bytes to copy (including null terminator)
 * @return Number of bytes copied (excluding null terminator)
 */
static u64 strcopy(char *dst, const char *src, u64 max)
{
  u64 i = 0;
  while(i < max - 1 && src[i]) {
    dst[i] = src[i];
    i++;
  }
  dst[i] = '\0';
  return i;
}

/**
 * @brief Get string length
 * @param s String to measure
 * @return Length of string (excluding null terminator)
 */
static u64 strlen_local(const char *s)
{
  u64 len = 0;
  while(s[len])
    len++;
  return len;
}

// MSR Definitions and Helpers

#define MSR_EFER 0xC0000080
#define MSR_STAR 0xC0000081
#ifndef MSR_LSTAR
  #define MSR_LSTAR 0xC0000082
#endif
#ifndef MSR_SFMASK
  #define MSR_SFMASK 0xC0000084
#endif
#ifndef MSR_FS_BASE
  #define MSR_FS_BASE 0xC0000100
#endif
#ifndef MSR_GS_BASE
  #define MSR_GS_BASE 0xC0000101
#endif

#ifndef EFER_SCE
  #define EFER_SCE (1UL << 0) /* Syscall Enable */
#endif

/**
 * @brief Read Model Specific Register
 * @param msr MSR address
 * @return 64-bit MSR value
 */
static inline u64 rdmsr(u32 msr)
{
  u32 lo, hi;
  __asm__ volatile("rdmsr" : "=a"(lo), "=d"(hi) : "c"(msr));
  return ((u64)hi << 32) | lo;
}

/**
 * @brief Write Model Specific Register
 * @param msr MSR address
 * @param value 64-bit value to write
 */
static inline void wrmsr(u32 msr, u64 value)
{
  __asm__ volatile(
      "wrmsr" ::"a"((u32)value), "d"((u32)(value >> 32)), "c"(msr)
  );
}


// arch_prctl Codes

#define ARCH_SET_GS 0x1001
#define ARCH_SET_FS 0x1002
#define ARCH_GET_FS 0x1003
#define ARCH_GET_GS 0x1004

// mmap Flags

#define MAP_SHARED    0x01
#define MAP_PRIVATE   0x02
#define MAP_FIXED     0x10
#define MAP_ANONYMOUS 0x20

// Forward Declarations for Pipe Functions

static i64 pipe_read(int fd, void *buf, u64 count);
static i64 pipe_write(int fd, const void *buf, u64 count);
static int pipe_close(int fd);

/**
 * @brief Read data from a file descriptor
 * @param fd File descriptor (0 = stdin)
 * @param buf User buffer to read into
 * @param count Maximum bytes to read
 * @return Number of bytes read, or negative errno on error
 *
 * For stdin (fd=0), blocks until keyboard input is available.
 * For file descriptors, reads from the VFS.
 */
static u64 sys_read(u64 fd, u64 buf, u64 count, u64 a4, u64 a5, u64 a6)
{
  (void)a4;
  (void)a5;
  (void)a6;

  if(!buf)
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
    /* It's a pipe - return result (may be bytes read, 0, or error) */
    return (pipe_result < 0) ? (u64)pipe_result : (u64)pipe_result;
  }

  /* Regular file descriptor */
  i64 result = vfs_read((i64)fd, (void *)buf, count);
  return (result < 0) ? (u64)result : (u64)result;
}

/**
 * @brief Write data to a file descriptor
 * @param fd File descriptor (1 = stdout, 2 = stderr)
 * @param buf User buffer containing data
 * @param count Number of bytes to write
 * @return Number of bytes written, or negative errno on error
 */
static u64 sys_write(u64 fd, u64 buf, u64 count, u64 a4, u64 a5, u64 a6)
{
  (void)a4;
  (void)a5;
  (void)a6;

  if(!buf)
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
    /* It's a pipe - return result */
    return (pipe_result < 0) ? (u64)pipe_result : (u64)pipe_result;
  }

  /* Regular file descriptor */
  i64 result = vfs_write((i64)fd, (void *)buf, count);
  return (result < 0) ? (u64)result : (u64)result;
}

/**
 * @brief Open a file or directory
 * @param path Path to the file
 * @param flags Open flags (O_RDONLY, O_WRONLY, O_CREAT, O_DIRECTORY, etc.)
 * @param mode File creation mode (ignored for now)
 * @return File descriptor on success, negative errno on error
 */
static u64 sys_open(u64 path, u64 flags, u64 mode, u64 a4, u64 a5, u64 a6)
{
  (void)mode;
  (void)a4;
  (void)a5;
  (void)a6;

  if(!path)
    return (u64)-EFAULT;

  i64 fd = vfs_open((const char *)path, (u32)flags);
  if(fd < 0)
    return (u64)-ENOENT;
  return (u64)fd;
}

/**
 * @brief Close a file descriptor
 * @param fd File descriptor to close
 * @return 0 on success, negative errno on error
 */
static u64 sys_close(u64 fd, u64 a2, u64 a3, u64 a4, u64 a5, u64 a6)
{
  (void)a2;
  (void)a3;
  (void)a4;
  (void)a5;
  (void)a6;

  /* Don't allow closing stdin/stdout/stderr */
  if(fd <= 2)
    return 0;

  /* Check if it's a pipe */
  if(pipe_close((int)fd) == 0) {
    return 0; /* Pipe closed successfully */
  }

  i64 result = vfs_close((i64)fd);
  return (result < 0) ? (u64)-EBADF : 0;
}

/**
 * @brief Linux stat structure (x86_64)
 */
struct linux_stat
{
  u64 st_dev;
  u64 st_ino;
  u64 st_nlink;
  u32 st_mode;
  u32 st_uid;
  u32 st_gid;
  u32 __pad0;
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
  i64 __unused[3];
};

#define S_IFMT  0170000
#define S_IFDIR 0040000
#define S_IFREG 0100000

/**
 * @brief Get file status by path
 * @param path File path
 * @param statbuf Buffer to fill with stat info
 * @return 0 on success, negative errno on error
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
  memzero(st, sizeof(*st));

  st->st_ino   = 1;
  st->st_nlink = 1;
  st->st_mode =
      (vst.type == VFS_DIRECTORY) ? (S_IFDIR | 0755) : (S_IFREG | 0644);
  st->st_size    = (i64)vst.size;
  st->st_blksize = 4096;
  st->st_blocks  = ((i64)vst.size + 511) / 512;

  return 0;
}

/**
 * @brief Get file status by file descriptor
 * @param fd File descriptor
 * @param statbuf Buffer to fill with stat info
 * @return 0 on success, negative errno on error
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
  memzero(st, sizeof(*st));

  /* For stdin/stdout/stderr, return character device stats */
  if(fd <= 2) {
    st->st_mode    = 0020000 | 0666; /* S_IFCHR | rw-rw-rw- */
    st->st_blksize = 4096;
    return 0;
  }

  /* TODO: implement proper fstat for file descriptors */
  st->st_mode    = S_IFREG | 0644;
  st->st_blksize = 4096;
  return 0;
}

/**
 * @brief Get symbolic link status (same as stat since we don't have symlinks)
 */
static u64 sys_lstat(u64 path, u64 statbuf, u64 a3, u64 a4, u64 a5, u64 a6)
{
  return sys_stat(path, statbuf, a3, a4, a5, a6);
}

/**
 * @brief Reposition file offset
 * @param fd File descriptor
 * @param offset Offset value
 * @param whence SEEK_SET, SEEK_CUR, or SEEK_END
 * @return New file offset, or negative errno on error
 */
static u64 sys_lseek(u64 fd, u64 offset, u64 whence, u64 a4, u64 a5, u64 a6)
{
  (void)a4;
  (void)a5;
  (void)a6;

  i64 result = vfs_seek((i64)fd, (i64)offset, (i32)whence);
  return (u64)result;
}

/**
 * @brief Map memory into process address space
 * @param addr Hint address (ignored, we choose location)
 * @param length Number of bytes to map
 * @param prot Protection flags (ignored, always RW)
 * @param flags MAP_ANONYMOUS, MAP_PRIVATE, etc.
 * @param fd File descriptor for file-backed mapping (-1 for anonymous)
 * @param offset Offset in file (ignored)
 * @return Mapped address, or -1 on error
 *
 * Only anonymous mappings are supported. Pages are allocated on demand
 * and zero-initialized.
 */
static u64
    sys_mmap(u64 addr, u64 length, u64 prot, u64 flags, u64 fd, u64 offset)
{
  (void)addr;
  (void)prot;
  (void)offset;

  if(length == 0)
    return (u64)-EINVAL;

  /* Only support anonymous mappings */
  if(!(flags & MAP_ANONYMOUS) && fd != (u64)-1) {
    return (u64)-ENOSYS;
  }

  proc_t *p = proc_current();
  if(!p)
    return (u64)-ENOMEM;

  /* Align length to page boundary */
  u64 aligned_len = (length + 0xFFF) & ~0xFFFUL;

  /* Allocate from per-process heap region */
  u64 result = p->heap_break;

  /* Map and zero pages via HHDM */
  for(u64 off = 0; off < aligned_len; off += 0x1000) {
    u64 phys = (u64)pmm_alloc();
    if(!phys) {
      return (u64)-ENOMEM;
    }
    vmm_map(result + off, phys, VMM_PRESENT | VMM_WRITE | VMM_USER);

    /* Zero the page via HHDM (kernel cannot write to user pages directly) */
    u8 *page = (u8 *)(phys + vmm_get_hhdm());
    for(u64 i = 0; i < 0x1000; i++) {
      page[i] = 0;
    }
  }

  p->heap_break += aligned_len;
  return result;
}

/**
 * @brief Change memory protection (stub)
 * @return Always 0 (success)
 */
static u64 sys_mprotect(u64 addr, u64 len, u64 prot, u64 a4, u64 a5, u64 a6)
{
  (void)addr;
  (void)len;
  (void)prot;
  (void)a4;
  (void)a5;
  (void)a6;
  return 0; /* Always succeed - we don't enforce protection */
}

/**
 * @brief Unmap memory (stub)
 * @return Always 0 (success)
 *
 * Memory is not actually freed - this is a simple implementation.
 */
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
 * @brief Change data segment size
 * @param addr New program break address, or 0 to query current
 * @return Current program break
 *
 * If addr is 0, returns current break without changing it.
 * If addr > current break, allocates and maps new pages.
 * If addr < current break, does nothing (no shrinking).
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

  /* Query current break */
  if(addr == 0) {
    return p->program_break;
  }

  /* Grow the heap */
  if(addr > p->program_break) {
    u64 old_end = (p->program_break + 0xFFF) & ~0xFFFUL;
    u64 new_end = (addr + 0xFFF) & ~0xFFFUL;

    /* Allocate and map new pages */
    for(u64 page_addr = old_end; page_addr < new_end; page_addr += 0x1000) {
      u64 phys = (u64)pmm_alloc();
      if(!phys) {
        return p->program_break; /* Out of memory, return old break */
      }
      vmm_map(page_addr, phys, VMM_PRESENT | VMM_WRITE | VMM_USER);

      /* Zero the page via HHDM (kernel cannot write to user pages directly) */
      u8 *page_ptr = (u8 *)(phys + vmm_get_hhdm());
      for(u64 i = 0; i < 0x1000; i++)
        page_ptr[i] = 0;
    }

    p->program_break = addr;
  }

  return p->program_break;
}

/* Terminal ioctl codes */
#define TCGETS     0x5401
#define TCSETS     0x5402
#define TIOCGWINSZ 0x5413

/**
 * @brief Device control operations
 * @param fd File descriptor
 * @param request Request code
 * @param arg Request argument
 * @return 0 on success, negative errno on error
 */
static u64 sys_ioctl(u64 fd, u64 request, u64 arg, u64 a4, u64 a5, u64 a6)
{
  (void)a4;
  (void)a5;
  (void)a6;

  /* Terminal ioctls for stdin/stdout/stderr */
  if(fd <= 2) {
    switch(request) {
    case TIOCGWINSZ: {
      /* Return window size: 80x25 */
      struct
      {
        u16 row;
        u16 col;
        u16 xpixel;
        u16 ypixel;
      } *ws = (void *)arg;
      if(ws) {
        ws->row    = 25;
        ws->col    = 80;
        ws->xpixel = 0;
        ws->ypixel = 0;
      }
      return 0;
    }
    case TCGETS:
    case TCSETS:
      return 0; /* Pretend success */
    }
  }

  return 0;
}

/**
 * @brief Check file accessibility
 * @param path File path
 * @param mode Access mode to check
 * @return 0 if accessible, negative errno otherwise
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
  if(vfs_stat((const char *)path, &st) < 0) {
    return (u64)-ENOENT;
  }
  return 0;
}

/* Pipe buffer size */
#define PIPE_BUF_SIZE 4096

/* Pipe structure */
typedef struct pipe
{
  u8  buffer[PIPE_BUF_SIZE];
  u64 read_pos;   /* Read position in buffer */
  u64 write_pos;  /* Write position in buffer */
  u64 count;      /* Bytes in buffer */
  int read_fd;    /* Read end file descriptor */
  int write_fd;   /* Write end file descriptor */
  int read_open;  /* Is read end open? */
  int write_open; /* Is write end open? */
} pipe_t;

/* Pipe table */
#define MAX_PIPES 16
static pipe_t pipes[MAX_PIPES];
static int    pipes_initialized = 0;

/**
 * @brief Find a pipe by file descriptor
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
 * @brief Allocate a new pipe
 */
static pipe_t *alloc_pipe(void)
{
  if(!pipes_initialized) {
    for(int i = 0; i < MAX_PIPES; i++) {
      pipes[i].read_open  = 0;
      pipes[i].write_open = 0;
    }
    pipes_initialized = 1;
  }

  for(int i = 0; i < MAX_PIPES; i++) {
    if(!pipes[i].read_open && !pipes[i].write_open) {
      pipes[i].read_pos  = 0;
      pipes[i].write_pos = 0;
      pipes[i].count     = 0;
      return &pipes[i];
    }
  }
  return NULL;
}

/**
 * @brief Create a pipe
 * @param pipefd Array of 2 ints: pipefd[0] = read end, pipefd[1] = write end
 * @return 0 on success, -errno on error
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

  int *fds = (int *)pipefd;

  /* Allocate pipe */
  pipe_t *p = alloc_pipe();
  if(!p)
    return (u64)-ENOMEM;

  /* Find two free file descriptors (start from 10 to avoid stdin/stdout/stderr
   * and first few files) */
  proc_t *proc = proc_current();
  if(!proc)
    return (u64)-EINVAL;

  int read_fd = -1, write_fd = -1;

  /* Find free FDs - use high numbers to avoid conflicts */
  for(int fd = 100; fd < 200; fd++) {
    int in_use = 0;
    /* Check if FD is in use as pipe */
    for(int j = 0; j < MAX_PIPES; j++) {
      if((pipes[j].read_open && pipes[j].read_fd == fd) ||
         (pipes[j].write_open && pipes[j].write_fd == fd)) {
        in_use = 1;
        break;
      }
    }
    if(!in_use) {
      if(read_fd == -1) {
        read_fd = fd;
      } else if(write_fd == -1) {
        write_fd = fd;
        break;
      }
    }
  }

  if(read_fd == -1 || write_fd == -1) {
    return (u64)-EMFILE;
  }

  p->read_fd    = read_fd;
  p->write_fd   = write_fd;
  p->read_open  = 1;
  p->write_open = 1;

  fds[0] = read_fd;
  fds[1] = write_fd;

  return 0;
}

/**
 * @brief Check if fd is a pipe and read from it
 * @return Bytes read, 0 on EOF, or negative errno on error. Returns -ENOENT if not a pipe.
 */
static i64 pipe_read(int fd, void *buf, u64 count)
{
  int     is_read;
  pipe_t *p = find_pipe_by_fd(fd, &is_read);
  if(!p || !is_read)
    return -ENOENT;

  if(!p->read_open)
    return -EBADF;

  /* If pipe is empty and write end is closed, return EOF */
  if(p->count == 0 && !p->write_open) {
    return 0;
  }

  /* If pipe is empty, block (busy-wait for now) */
  while(p->count == 0 && p->write_open) {
    /* Yield CPU - simple busy wait with yield */
    __asm__ volatile("pause");
  }

  /* Read from pipe buffer */
  u64 to_read = count;
  if(to_read > p->count)
    to_read = p->count;

  u8 *dst = (u8 *)buf;
  for(u64 i = 0; i < to_read; i++) {
    dst[i]      = p->buffer[p->read_pos];
    p->read_pos = (p->read_pos + 1) % PIPE_BUF_SIZE;
  }
  p->count -= to_read;

  return (i64)to_read;
}

/**
 * @brief Check if fd is a pipe and write to it
 * @return Bytes written, or negative errno on error. Returns -ENOENT if not a pipe.
 */
static i64 pipe_write(int fd, const void *buf, u64 count)
{
  int     is_read;
  pipe_t *p = find_pipe_by_fd(fd, &is_read);
  if(!p || is_read)
    return -ENOENT;

  if(!p->write_open)
    return -EBADF;

  /* If read end is closed, return SIGPIPE error */
  if(!p->read_open) {
    return -EPIPE;
  }

  /* If pipe is full, block (busy-wait for now) */
  while(p->count >= PIPE_BUF_SIZE && p->read_open) {
    __asm__ volatile("pause");
  }

  /* Write to pipe buffer */
  u64 space    = PIPE_BUF_SIZE - p->count;
  u64 to_write = count;
  if(to_write > space)
    to_write = space;

  const u8 *src = (const u8 *)buf;
  for(u64 i = 0; i < to_write; i++) {
    p->buffer[p->write_pos] = src[i];
    p->write_pos            = (p->write_pos + 1) % PIPE_BUF_SIZE;
  }
  p->count += to_write;

  return (i64)to_write;
}

/**
 * @brief Check if fd is a pipe and close it
 * @return 0 if handled, -ENOENT if not a pipe
 */
static int pipe_close(int fd)
{
  int     is_read;
  pipe_t *p = find_pipe_by_fd(fd, &is_read);
  if(!p)
    return -ENOENT;

  if(is_read) {
    p->read_open = 0;
  } else {
    p->write_open = 0;
  }

  return 0;
}

/**
 * @brief Duplicate file descriptor (stub)
 */
static u64 sys_dup(u64 oldfd, u64 a2, u64 a3, u64 a4, u64 a5, u64 a6)
{
  (void)a2;
  (void)a3;
  (void)a4;
  (void)a5;
  (void)a6;
  return oldfd; /* Stub: return same fd */
}

/**
 * @brief Duplicate file descriptor to specific number (stub)
 */
static u64 sys_dup2(u64 oldfd, u64 newfd, u64 a3, u64 a4, u64 a5, u64 a6)
{
  (void)oldfd;
  (void)a3;
  (void)a4;
  (void)a5;
  (void)a6;
  return newfd; /* Stub: pretend success */
}

/**
 * @brief High-resolution sleep
 * @param req Pointer to timespec with requested sleep time
 * @param rem Pointer to timespec for remaining time (ignored)
 * @return 0 on success
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
    i64 tv_sec;
    i64 tv_nsec;
  } *ts = (void *)req;

  /* Convert to milliseconds, then to timer ticks (~10ms each) */
  u64 ms    = (u64)ts->tv_sec * 1000 + (u64)ts->tv_nsec / 1000000;
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

/**
 * @brief Get process ID
 * @return Current process ID
 */
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

/**
 * @brief Get thread ID (same as PID for single-threaded)
 * @return Current thread ID
 */
static u64 sys_gettid(u64 a1, u64 a2, u64 a3, u64 a4, u64 a5, u64 a6)
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

/**
 * @brief Get parent process ID
 * @return Parent process ID
 */
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

/* Saved during syscall_dispatch to allow sys_fork to access the frame */
static syscall_frame_t *current_syscall_frame = NULL;

/**
 * @brief Fork the current process
 * @return Child PID in parent, 0 in child, -errno on error
 */
static u64 sys_fork(u64 a1, u64 a2, u64 a3, u64 a4, u64 a5, u64 a6)
{
  (void)a1;
  (void)a2;
  (void)a3;
  (void)a4;
  (void)a5;
  (void)a6;

  if(!current_syscall_frame) {
    return (u64)-EINVAL;
  }

  i64 result = proc_fork(current_syscall_frame);
  return (u64)result;
}

#define MAX_EXEC_ARGS 32
#define MAX_ARG_LEN   256

/**
 * @brief Execute a program
 * @param pathname Path to executable
 * @param argv Argument array (NULL-terminated)
 * @param envp Environment array (ignored)
 * @return Does not return on success, negative errno on error
 *
 * Currently implemented as "spawn and wait" - creates child process
 * and waits for it, returning its exit code.
 */
static u64 sys_execve(u64 pathname, u64 argv, u64 envp, u64 a4, u64 a5, u64 a6)
{
  (void)envp;
  (void)a4;
  (void)a5;
  (void)a6;

  if(!pathname)
    return (u64)-EFAULT;

  const char *path      = (const char *)pathname;
  char      **user_argv = (char **)argv;

  /* Check file exists */
  vfs_stat_t st;
  if(vfs_stat(path, &st) < 0) {
    return (u64)-ENOENT;
  }
  if(st.type != VFS_FILE) {
    return (u64)-EACCES;
  }

  /* Load ELF data */
  void *elf_data = kmalloc(st.size + 1);
  if(!elf_data) {
    return (u64)-ENOMEM;
  }

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

  /* Build argument list */
  static char  arg_storage[MAX_EXEC_ARGS][MAX_ARG_LEN];
  static char *child_argv[MAX_EXEC_ARGS + 1];

  int          argc = 0;
  strcopy(arg_storage[argc], path, MAX_ARG_LEN);
  child_argv[argc++] = arg_storage[0];

  if(user_argv) {
    for(int i = 0; user_argv[i] && argc < MAX_EXEC_ARGS; i++) {
      strcopy(arg_storage[argc], user_argv[i], MAX_ARG_LEN);
      child_argv[argc] = arg_storage[argc];
      argc++;
    }
  }
  child_argv[argc] = NULL;

  /* Create child process */
  u64 child_pid = proc_create(path, elf_data, st.size, child_argv);
  kfree(elf_data);

  if(child_pid == 0) {
    return (u64)-ENOMEM;
  }

  /* Wait for child and return its exit code */
  i64 exit_code = proc_wait(child_pid);
  return (u64)exit_code;
}

/**
 * @brief Terminate current process
 * @param status Exit status code
 * @return Does not return
 */
static u64 sys_exit(u64 status, u64 a2, u64 a3, u64 a4, u64 a5, u64 a6)
{
  (void)a2;
  (void)a3;
  (void)a4;
  (void)a5;
  (void)a6;
  proc_exit((i64)status);
  __builtin_unreachable();
}

/**
 * @brief Wait for child process
 * @param pid   -1 = any child, >0 = specific child
 * @param wstatus  Pointer to store exit status (can be NULL)
 * @param options  WNOHANG = 1 (don't block)
 * @param rusage   Resource usage (ignored)
 * @return Child PID on success, 0 if WNOHANG and no child ready, -errno on
 * error
 */
static u64
    sys_wait4(u64 pid, u64 wstatus, u64 options, u64 rusage, u64 a5, u64 a6)
{
  (void)rusage;
  (void)a5;
  (void)a6;

  i64 result = proc_waitpid((i64)pid, (i32 *)wstatus, (i32)options);
  return (u64)result;
}

/**
 * @brief Get system identification
 * @param buf Buffer to fill with utsname structure
 * @return 0 on success
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

  /* struct utsname has 5 fields of 65 bytes each */
  struct utsname
  {
    char sysname[65];
    char nodename[65];
    char release[65];
    char version[65];
    char machine[65];
  } *u = (void *)buf;

  memzero(u, sizeof(*u));
  strcopy(u->sysname, "Alcor2", 65);
  strcopy(u->nodename, "alcor2", 65);
  strcopy(u->release, "0.1.0", 65);
  strcopy(u->version, "Alcor2 OS", 65);
  strcopy(u->machine, "x86_64", 65);

  return 0;
}

/**
 * @brief File control operations (stub)
 * @return 0 on success
 */
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
 * @brief Get directory entries
 * @param fd Directory file descriptor (opened with O_DIRECTORY)
 * @param dirp Buffer to fill with dirent structures
 * @param count Size of buffer
 * @return Bytes written, 0 at end of directory, negative errno on error
 *
 * Fills buffer with Linux dirent structures. Each entry contains:
 * - d_ino (8 bytes): inode number
 * - d_off (8 bytes): offset to next entry
 * - d_reclen (2 bytes): length of this record
 * - d_type (1 byte): file type (DT_DIR, DT_REG, etc.)
 * - d_name (variable): null-terminated filename
 */
static u64 sys_getdents(u64 fd, u64 dirp, u64 count, u64 a4, u64 a5, u64 a6)
{
  (void)a4;
  (void)a5;
  (void)a6;

  if(!dirp)
    return (u64)-EFAULT;
  if(count < 32)
    return (u64)-EINVAL;

  i64 result = vfs_getdents((i64)fd, (void *)dirp, count);
  if(result < 0)
    return (u64)-EBADF;
  return (u64)result;
}

/**
 * @brief Get directory entries (64-bit version, same as getdents)
 */
static u64 sys_getdents64(u64 fd, u64 dirp, u64 count, u64 a4, u64 a5, u64 a6)
{
  return sys_getdents(fd, dirp, count, a4, a5, a6);
}

/**
 * @brief Get current working directory
 * @param buf Buffer to fill with path
 * @param size Size of buffer
 * @return Length of path (including null), or negative errno on error
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

  const char *cwd = vfs_getcwd();
  u64         len = strlen_local(cwd);

  if(len + 1 > size) {
    return (u64)-ERANGE;
  }

  strcopy((char *)buf, cwd, size);
  return len + 1;
}

/**
 * @brief Change working directory
 * @param path New directory path
 * @return 0 on success, negative errno on error
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
 * @brief Create a directory
 * @param pathname Path for new directory
 * @param mode Permissions (ignored)
 * @return 0 on success, negative errno on error
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
 * @brief Remove a file
 * @param pathname Path to file
 * @return 0 on success, negative errno on error
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

/**
 * @brief Read symbolic link (not supported)
 * @return -EINVAL (no symlinks)
 */
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

/**
 * @brief Get user ID
 * @return 0 (root)
 */
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

/**
 * @brief Get group ID
 * @return 0 (root)
 */
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

/**
 * @brief Get effective user ID
 * @return 0 (root)
 */
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

/**
 * @brief Get effective group ID
 * @return 0 (root)
 */
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
/**
 * @brief Set architecture-specific thread state
 * @param code Operation code (ARCH_SET_FS, ARCH_SET_GS, etc.)
 * @param addr Address to set/get
 * @return 0 on success, negative errno on error
 *
 * Used by musl to set up TLS (Thread Local Storage) via FS register.
 */
static u64 sys_arch_prctl(u64 code, u64 addr, u64 a3, u64 a4, u64 a5, u64 a6)
{
  (void)a3;
  (void)a4;
  (void)a5;
  (void)a6;

  proc_t *p = proc_current();

  switch(code) {
  case ARCH_SET_FS:
    wrmsr(MSR_FS_BASE, addr);
    if(p)
      p->fs_base = addr;
    return 0;

  case ARCH_SET_GS:
    wrmsr(MSR_GS_BASE, addr);
    return 0;

  case ARCH_GET_FS:
    if(!addr)
      return (u64)-EFAULT;
    *(u64 *)addr = rdmsr(MSR_FS_BASE);
    return 0;

  case ARCH_GET_GS:
    if(!addr)
      return (u64)-EFAULT;
    *(u64 *)addr = rdmsr(MSR_GS_BASE);
    return 0;

  default:
    return (u64)-EINVAL;
  }
}

/**
 * @brief Fast userspace locking (stub for single-threaded)
 * @return 0
 */
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

/**
 * @brief Set pointer to thread ID
 * @param tidptr Pointer to store TID
 * @return Current thread ID
 */
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
 * @brief Get time from clock
 * @param clk_id Clock identifier
 * @param tp Pointer to timespec structure
 * @return 0 on success
 *
 * Currently returns 0 time - no RTC support yet.
 */
static u64 sys_clock_gettime(u64 clk_id, u64 tp, u64 a3, u64 a4, u64 a5, u64 a6)
{
  (void)clk_id;
  (void)a3;
  (void)a4;
  (void)a5;
  (void)a6;

  if(!tp)
    return (u64)-EFAULT;

  struct
  {
    i64 tv_sec;
    i64 tv_nsec;
  } *ts       = (void *)tp;
  ts->tv_sec  = 0;
  ts->tv_nsec = 0;
  return 0;
}

/**
 * @brief Yield the processor
 * @return Always 0
 */
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
 * @brief Write data from multiple buffers
 * @param fd File descriptor
 * @param iov Array of iovec structures
 * @param iovcnt Number of iovec structures
 * @return Total bytes written, or negative errno on error
 */
static u64 sys_writev(u64 fd, u64 iov, u64 iovcnt, u64 a4, u64 a5, u64 a6)
{
  (void)a4;
  (void)a5;
  (void)a6;

  if(!iov)
    return (u64)-EFAULT;

  struct iovec *vec   = (struct iovec *)iov;
  u64           total = 0;

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

// Syscall Table
typedef u64         (*syscall_fn_t)(u64, u64, u64, u64, u64, u64);

static syscall_fn_t syscall_table[SYS_MAX] = {
    /* I/O */
    [SYS_READ]  = sys_read,
    [SYS_WRITE] = sys_write,
    [SYS_OPEN]  = sys_open,
    [SYS_CLOSE] = sys_close,
    [SYS_STAT]  = sys_stat,
    [SYS_FSTAT] = sys_fstat,
    [SYS_LSTAT] = sys_lstat,
    [SYS_LSEEK] = sys_lseek,
    [SYS_IOCTL] = sys_ioctl,
    [20]        = sys_writev,
    /* SYS_WRITEV */ // TODO: Add sys_writev
    [SYS_ACCESS]   = sys_access,
    [SYS_PIPE]     = sys_pipe,
    [SYS_DUP]      = sys_dup,
    [SYS_DUP2]     = sys_dup2,
    [SYS_FCNTL]    = sys_fcntl,
    [SYS_READLINK] = sys_readlink,

    /* Memory */
    [SYS_MMAP]     = sys_mmap,
    [SYS_MPROTECT] = sys_mprotect,
    [SYS_MUNMAP]   = sys_munmap,
    [SYS_BRK]      = sys_brk,

    /* Process */
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

    /* Scheduling */
    [SYS_SCHED_YIELD]   = sys_sched_yield,
    [SYS_NANOSLEEP]     = sys_nanosleep,
    [SYS_CLOCK_GETTIME] = sys_clock_gettime,
    [SYS_FUTEX]         = sys_futex,

    /* Filesystem */
    [SYS_GETDENTS]   = sys_getdents,
    [SYS_GETCWD]     = sys_getcwd,
    [SYS_CHDIR]      = sys_chdir,
    [SYS_MKDIR]      = sys_mkdir,
    [SYS_UNLINK]     = sys_unlink,
    [SYS_GETDENTS64] = sys_getdents64,

    /* Architecture */
    [SYS_ARCH_PRCTL] = sys_arch_prctl,
};

/**
 * @brief Main syscall dispatcher
 * @param frame Pointer to saved register frame
 * @return Syscall result (placed in RAX)
 *
 * Called from syscall_entry assembly stub. Looks up handler in table
 * and dispatches to appropriate function.
 */
u64 syscall_dispatch(syscall_frame_t *frame)
{
  u64 num = frame->rax;

  if(num >= SYS_MAX || syscall_table[num] == NULL) {
    return (u64)-ENOSYS;
  }

  /* Save frame pointer for syscalls that need it (like fork) */
  current_syscall_frame = frame;

  u64 result = syscall_table[num](
      frame->rdi, frame->rsi, frame->rdx, frame->r10, frame->r8, frame->r9
  );

  current_syscall_frame = NULL;
  return result;
}

// Syscall Initialization
extern void syscall_entry(void);

/**
 * @brief Initialize syscall mechanism
 *
 * Sets up MSRs for SYSCALL/SYSRET instructions:
 * - EFER.SCE: Enable syscall extension
 * - STAR: Segment selectors for kernel/user
 * - LSTAR: Syscall entry point
 * - SFMASK: Flags to clear on syscall
 */
void syscall_init(void)
{
  /* Enable syscall extension */
  u64 efer = rdmsr(MSR_EFER);
  efer |= EFER_SCE;
  wrmsr(MSR_EFER, efer);

  /* Set up segment selectors
   * STAR[31:0]  = reserved
   * STAR[47:32] = kernel CS (0x28 = GDT entry 5)
   * STAR[63:48] = user CS base (0x30 = GDT entry 6, actual CS = 0x33)
   */
  u64 star = ((u64)0x28 << 32) | ((u64)0x30 << 48);
  wrmsr(MSR_STAR, star);

  /* Set syscall entry point */
  wrmsr(MSR_LSTAR, (u64)syscall_entry);

  /* Clear IF (interrupt flag) on syscall entry */
  wrmsr(MSR_SFMASK, 0x200);

  console_print("[SYSCALL] Initialized\n");
}
