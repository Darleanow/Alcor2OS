/**
 * @file src/kernel/sys/pipe.c
 * @brief Minimal anonymous pipe implementation for syscall layer.
 */

#include <alcor2/errno.h>
#include <alcor2/kstdlib.h>
#include <alcor2/proc/proc.h>
#include <alcor2/sys/internal.h>
#include <alcor2/mm/vmm.h>

#define PIPE_BUF_SIZE 4096
#define MAX_PIPES     16
#define PIPE_FD_MIN   100
#define PIPE_FD_MAX   200

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

static pipe_t pipes[MAX_PIPES];
static int    pipes_initialized = 0;

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

static pipe_t *alloc_pipe(void)
{
  if(!pipes_initialized) {
    kzero(pipes, sizeof(pipes));
    pipes_initialized = 1;
  }

  for(int i = 0; i < MAX_PIPES; i++) {
    if(!pipes[i].read_open && !pipes[i].write_open) {
      pipes[i].read_pos = 0;
      pipes[i].write_pos = 0;
      pipes[i].count = 0;
      return &pipes[i];
    }
  }
  return NULL;
}

u64 sys_pipe(u64 pipefd, u64 a2, u64 a3, u64 a4, u64 a5, u64 a6)
{
  (void)a2;
  (void)a3;
  (void)a4;
  (void)a5;
  (void)a6;

  if(!pipefd)
    return (u64)-EFAULT;
  if(!vmm_is_user_range((void *)pipefd, sizeof(int) * 2))
    return (u64)-EFAULT;

  pipe_t *p = alloc_pipe();
  if(!p)
    return (u64)-ENOMEM;

  if(!proc_current())
    return (u64)-EINVAL;

  int read_fd = -1;
  int write_fd = -1;

  for(int fd = PIPE_FD_MIN; fd < PIPE_FD_MAX; fd++) {
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
      else {
        write_fd = fd;
        break;
      }
    }
  }

  if(read_fd == -1 || write_fd == -1)
    return (u64)-EMFILE;

  p->read_fd = read_fd;
  p->write_fd = write_fd;
  p->read_open = 1;
  p->write_open = 1;

  int *fds = (int *)pipefd;
  fds[0] = read_fd;
  fds[1] = write_fd;
  return 0;
}

i64 pipe_read(int fd, void *buf, u64 count)
{
  int     is_read;
  pipe_t *p = find_pipe_by_fd(fd, &is_read);
  if(!p || !is_read)
    return -ENOENT;
  if(!p->read_open)
    return -EBADF;
  if(p->count == 0 && !p->write_open)
    return 0;

  while(p->count == 0 && p->write_open)
    __asm__ volatile("pause");

  u64 to_read = count > p->count ? p->count : count;
  u8 *dst     = (u8 *)buf;
  for(u64 i = 0; i < to_read; i++) {
    dst[i]      = p->buffer[p->read_pos];
    p->read_pos = (p->read_pos + 1) % PIPE_BUF_SIZE;
  }
  p->count -= to_read;
  return (i64)to_read;
}

i64 pipe_write(int fd, const void *buf, u64 count)
{
  int     is_read;
  pipe_t *p = find_pipe_by_fd(fd, &is_read);
  if(!p || is_read)
    return -ENOENT;
  if(!p->write_open)
    return -EBADF;
  if(!p->read_open)
    return -EPIPE;

  while(p->count >= PIPE_BUF_SIZE && p->read_open)
    __asm__ volatile("pause");

  u64       space    = PIPE_BUF_SIZE - p->count;
  u64       to_write = count > space ? space : count;
  const u8 *src      = (const u8 *)buf;

  for(u64 i = 0; i < to_write; i++) {
    p->buffer[p->write_pos] = src[i];
    p->write_pos            = (p->write_pos + 1) % PIPE_BUF_SIZE;
  }
  p->count += to_write;
  return (i64)to_write;
}

int pipe_close(int fd)
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
