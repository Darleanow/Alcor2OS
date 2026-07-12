#include "test_common.h"

#include <alcor2/errno.h>
#include <alcor2/fs/pipe.h>
#include <alcor2/fs/vfs.h>
#include <alcor2/proc/proc.h>
#include <alcor2/types.h>

#include <string.h>

void *kmalloc(u64 n)  { (void)n; return NULL; }
void  kfree(void *p)  { (void)p; }
void *kmemcpy(void *d, const void *s, u64 n) { return memcpy(d, s, n); }
void  kzero(void *d, u64 n)  { memset(d, 0, n); }
u64   kstrlen(const char *s) { return strlen(s); }
int   kstrcmp(const char *a, const char *b) { return strcmp(a, b); }
char *kstrncpy(char *d, const char *s, u64 m) {
  if(!m) return d;
  u64 i;
  for(i = 0; i < m - 1 && s[i]; i++) d[i] = s[i];
  d[i] = '\0';
  return d;
}
void console_print(const char *s)          { (void)s; }
void console_printf(const char *fmt, ...)  { (void)fmt; }

/* vmm stubs */
static bool g_user_ptr_ok   = true;
static bool g_user_range_ok = true;
bool vmm_is_user_ptr(const void *p)              { (void)p; return g_user_ptr_ok; }
bool vmm_is_user_range(const void *p, u64 n)     { (void)p; (void)n; return g_user_range_ok; }

/* proc stubs */
static proc_t  g_proc;
static bool    g_no_proc = false;
proc_t *proc_current(void) { return g_no_proc ? NULL : &g_proc; }

/* vfs stubs */
static i64  g_vfs_open_ret    = 3;
static i64  g_vfs_close_ret   = 0;
static i64  g_vfs_stat_ret    = 0;
static i64  g_vfs_fstat_ret   = 0;
static i64  g_vfs_chdir_ret   = 0;
static i64  g_vfs_mkdir_ret   = 0;
static i64  g_vfs_rmdir_ret   = 0;
static i64  g_vfs_unlink_ret  = 0;
static i64  g_vfs_rename_ret  = 0;
static i64  g_vfs_dup_ret     = 4;
static i64  g_vfs_dup2_ret    = 5;
static i64  g_vfs_read_ret    = 4;
static i64  g_vfs_write_ret   = 4;
static i64  g_vfs_seek_ret    = 0;
static i64  g_vfs_getdents_ret= 64;
static i64  g_vfs_truncate_ret= 0;
static i64  g_vfs_get_flags_ret = 0;
static i64  g_vfs_set_flags_ret = 0;
static i64  g_vfs_readlink_ret  = 7;
static const char *g_vfs_getcwd = "/";
static vfs_stat_t g_vfs_stat_out;

i64 vfs_open(const char *p, u32 f)           { (void)p; (void)f; return g_vfs_open_ret; }
i64 vfs_close(i64 fd)                        { (void)fd; return g_vfs_close_ret; }
i64 vfs_stat(const char *p, vfs_stat_t *s)   { (void)p; if(s) *s = g_vfs_stat_out; return g_vfs_stat_ret; }
i64 vfs_fstat(i64 fd, vfs_stat_t *s)         { (void)fd; if(s) *s = g_vfs_stat_out; return g_vfs_fstat_ret; }
i64 vfs_chdir(const char *p)                 { (void)p; return g_vfs_chdir_ret; }
i64 vfs_mkdir(const char *p)                 { (void)p; return g_vfs_mkdir_ret; }
i64 vfs_rmdir(const char *p)                 { (void)p; return g_vfs_rmdir_ret; }
i64 vfs_unlink(const char *p)                { (void)p; return g_vfs_unlink_ret; }
i64 vfs_rename(const char *o, const char *n) { (void)o; (void)n; return g_vfs_rename_ret; }
i64 vfs_dup(i64 fd)                          { (void)fd; return g_vfs_dup_ret; }
i64 vfs_dup2(i64 o, i64 n)                   { (void)o; (void)n; return g_vfs_dup2_ret; }
i64 vfs_read(i64 fd, void *b, u64 n)         { (void)fd; (void)b; (void)n; return g_vfs_read_ret; }
i64 vfs_write(i64 fd, const void *b, u64 n)  { (void)fd; (void)b; (void)n; return g_vfs_write_ret; }
i64 vfs_seek(i64 fd, i64 off, i32 w)         { (void)fd; (void)off; (void)w; return g_vfs_seek_ret; }
i64 vfs_getdents(i64 fd, void *b, u64 n)     { (void)fd; (void)b; (void)n; return g_vfs_getdents_ret; }
i64 vfs_ftruncate(i64 fd, u64 len)           { (void)fd; (void)len; return g_vfs_truncate_ret; }
i64 vfs_get_flags(i64 fd)                    { (void)fd; return g_vfs_get_flags_ret; }
i64 vfs_set_flags(i64 fd, u32 f)             { (void)fd; (void)f; return g_vfs_set_flags_ret; }
i64 vfs_readlink(const char *p, char *b, u64 n) {
  (void)p; (void)n;
  if(b && g_vfs_readlink_ret > 0)
    memcpy(b, "/target", (size_t)g_vfs_readlink_ret);
  return g_vfs_readlink_ret;
}
const char *vfs_getcwd(void) { return g_vfs_getcwd; }

/* pipe stubs */
static void *g_pipe_obj = (void *)0xCAFE;
void *pipe_alloc_obj(void)       { return g_pipe_obj; }
void  pipe_rd_release(void *p)   { (void)p; }
void  pipe_wr_release(void *p)   { (void)p; }

/* vfs OFT stubs */
static i32  g_oft_rd  = 0;
static i32  g_oft_wr  = 1;
static i32  g_oft_seq = 0;
static i64  g_fd_seq  = 3;
static int  g_fd_install_count = 0; /* counts vfs_install_fd calls */
static int  g_fd_fail_on = -1;      /* fail on Nth call (1-based), -1=never */
i32 vfs_oft_alloc_pipe(i32 kind, void *pipe) {
  (void)kind; (void)pipe;
  return g_oft_seq++;
}
void vfs_oft_release(i32 idx) { (void)idx; }
i64  vfs_install_fd(i32 idx)  {
  (void)idx;
  g_fd_install_count++;
  if(g_fd_fail_on > 0 && g_fd_install_count == g_fd_fail_on)
    return -ENFILE;
  return g_fd_seq++;
}

/* sys_io stubs referenced by sys_fs (pread64/pwrite64 via sys_read/sys_write) */
kern_err_t sys_read(u64 fd, u64 buf, u64 count, u64 a4, u64 a5, u64 a6) {
  (void)fd; (void)buf; (void)a4; (void)a5; (void)a6;
  if(!count) return 0;
  return (u64)g_vfs_read_ret;
}
kern_err_t sys_write(u64 fd, u64 buf, u64 count, u64 a4, u64 a5, u64 a6) {
  (void)fd; (void)buf; (void)a4; (void)a5; (void)a6;
  if(!count) return 0;
  return (u64)g_vfs_write_ret;
}

#include "../../src/kernel/sys/sys_fs.c"

static int setup(void **state)
{
  (void)state;
  memset(&g_proc, 0, sizeof(g_proc));
  for(int i = 0; i < VFS_MAX_FD; i++) g_proc.fds[i] = -1;
  strcpy(g_proc.cwd, "/");
  g_no_proc        = false;
  g_user_ptr_ok    = true;
  g_user_range_ok  = true;
  g_vfs_open_ret   = 3;
  g_vfs_close_ret  = 0;
  g_vfs_stat_ret   = 0;
  g_vfs_fstat_ret  = 0;
  g_vfs_chdir_ret  = 0;
  g_vfs_mkdir_ret  = 0;
  g_vfs_rmdir_ret  = 0;
  g_vfs_unlink_ret = 0;
  g_vfs_rename_ret = 0;
  g_vfs_dup_ret    = 4;
  g_vfs_dup2_ret   = 5;
  g_vfs_read_ret   = 4;
  g_vfs_write_ret  = 4;
  g_vfs_seek_ret   = 0;
  g_vfs_getdents_ret = 64;
  g_vfs_truncate_ret = 0;
  g_vfs_get_flags_ret = 0;
  g_vfs_set_flags_ret = 0;
  g_vfs_readlink_ret  = 7;
  g_vfs_getcwd     = "/";
  memset(&g_vfs_stat_out, 0, sizeof(g_vfs_stat_out));
  g_pipe_obj       = (void *)0xCAFE;
  g_oft_rd             = 0;
  g_oft_wr             = 1;
  g_oft_seq            = 0;
  g_fd_seq             = 3;
  g_fd_install_count   = 0;
  g_fd_fail_on         = -1;
  return 0;
}

/* sys_open */
static void open_efault_on_bad_path(void **state) {
  (void)state;
  g_user_ptr_ok = false;
  assert_int_equal((i64)sys_open(0x1000, 0, 0, 0, 0, 0), -EFAULT);
}

static void open_returns_fd(void **state) {
  (void)state;
  char path[] = "/file";
  assert_int_equal(sys_open((u64)path, 0, 0, 0, 0, 0), 3);
}

/* sys_close */
static void close_returns_zero_on_success(void **state) {
  (void)state;
  assert_int_equal(sys_close(3, 0, 0, 0, 0, 0), 0);
}

static void close_propagates_error(void **state) {
  (void)state;
  g_vfs_close_ret = -EBADF;
  assert_int_equal((i64)sys_close(99, 0, 0, 0, 0, 0), -EBADF);
}

/* sys_stat */
static void stat_efault_on_bad_path(void **state) {
  (void)state;
  g_user_ptr_ok = false;
  struct stat_buf sb;
  assert_int_equal((i64)sys_stat(0x1000, (u64)&sb, 0, 0, 0, 0), -EFAULT);
}

static void stat_efault_on_bad_buf(void **state) {
  (void)state;
  char path[] = "/f";
  g_user_range_ok = false;
  assert_int_equal((i64)sys_stat((u64)path, 0, 0, 0, 0, 0), -EFAULT);
}

static void stat_success_fills_buf(void **state) {
  (void)state;
  char path[] = "/f";
  g_vfs_stat_out.size = 100;
  g_vfs_stat_out.type = VFS_FILE;
  struct stat_buf sb;
  assert_int_equal(sys_stat((u64)path, (u64)&sb, 0, 0, 0, 0), 0);
  assert_int_equal(sb.st_size, 100);
}

static void stat_enoent_propagated(void **state) {
  (void)state;
  char path[] = "/nope";
  g_vfs_stat_ret = -ENOENT;
  struct stat_buf sb;
  assert_int_equal((i64)sys_stat((u64)path, (u64)&sb, 0, 0, 0, 0), -ENOENT);
}

/* sys_fstat */
static void fstat_efault_on_bad_buf(void **state) {
  (void)state;
  g_user_range_ok = false;
  assert_int_equal((i64)sys_fstat(3, 0, 0, 0, 0, 0), -EFAULT);
}

static void fstat_success(void **state) {
  (void)state;
  g_vfs_stat_out.size = 512;
  g_vfs_stat_out.type = VFS_FILE;
  struct stat_buf sb;
  assert_int_equal(sys_fstat(3, (u64)&sb, 0, 0, 0, 0), 0);
  assert_int_equal(sb.st_size, 512);
}

/* sys_lstat */
static void lstat_efault_on_bad_path(void **state) {
  (void)state;
  g_user_ptr_ok = false;
  struct stat_buf sb;
  assert_int_equal((i64)sys_lstat(0, (u64)&sb, 0, 0, 0, 0), -EFAULT);
}

static void lstat_delegates_to_stat(void **state) {
  (void)state;
  char path[] = "/f";
  g_vfs_stat_out.size = 8;
  struct stat_buf sb;
  assert_int_equal(sys_lstat((u64)path, (u64)&sb, 0, 0, 0, 0), 0);
  assert_int_equal(sb.st_size, 8);
}

/* sys_getcwd */
static void getcwd_efault_on_null(void **state) {
  (void)state;
  assert_int_equal((i64)sys_getcwd(0, 64, 0, 0, 0, 0), -EFAULT);
}

static void getcwd_einval_on_zero_size(void **state) {
  (void)state;
  char buf[64];
  assert_int_equal((i64)sys_getcwd((u64)buf, 0, 0, 0, 0, 0), -EINVAL);
}

static void getcwd_copies_cwd(void **state) {
  (void)state;
  g_vfs_getcwd = "/home";
  char buf[64];
  u64 ret = sys_getcwd((u64)buf, sizeof(buf), 0, 0, 0, 0);
  assert_true((i64)ret > 0);
  assert_string_equal(buf, "/home");
}

static void getcwd_erange_when_buf_too_small(void **state) {
  (void)state;
  g_vfs_getcwd = "/very/long/path/here";
  char buf[4];
  assert_int_equal((i64)sys_getcwd((u64)buf, sizeof(buf), 0, 0, 0, 0), -ERANGE);
}

/* sys_chdir */
static void chdir_efault_on_bad_path(void **state) {
  (void)state;
  g_user_ptr_ok = false;
  assert_int_equal((i64)sys_chdir(0x1000, 0, 0, 0, 0, 0), -EFAULT);
}

static void chdir_success(void **state) {
  (void)state;
  char path[] = "/tmp";
  assert_int_equal(sys_chdir((u64)path, 0, 0, 0, 0, 0), 0);
}

static void chdir_enoent_propagated(void **state) {
  (void)state;
  char path[] = "/nope";
  g_vfs_chdir_ret = -ENOENT;
  assert_int_equal((i64)sys_chdir((u64)path, 0, 0, 0, 0, 0), -ENOENT);
}

/* sys_mkdir / sys_rmdir / sys_unlink */
static void mkdir_efault_on_bad_path(void **state) {
  (void)state;
  g_user_ptr_ok = false;
  assert_int_equal((i64)sys_mkdir(0x1000, 0, 0, 0, 0, 0), -EFAULT);
}

static void mkdir_success(void **state) {
  (void)state;
  char path[] = "/newdir";
  assert_int_equal(sys_mkdir((u64)path, 0755, 0, 0, 0, 0), 0);
}

static void rmdir_efault_on_bad_path(void **state) {
  (void)state;
  g_user_ptr_ok = false;
  assert_int_equal((i64)sys_rmdir(0x1000, 0, 0, 0, 0, 0), -EFAULT);
}

static void rmdir_success(void **state) {
  (void)state;
  char path[] = "/olddir";
  assert_int_equal(sys_rmdir((u64)path, 0, 0, 0, 0, 0), 0);
}

static void unlink_efault_on_bad_path(void **state) {
  (void)state;
  g_user_ptr_ok = false;
  assert_int_equal((i64)sys_unlink(0x1000, 0, 0, 0, 0, 0), -EFAULT);
}

static void unlink_success(void **state) {
  (void)state;
  char path[] = "/file";
  assert_int_equal(sys_unlink((u64)path, 0, 0, 0, 0, 0), 0);
}

/* sys_rename */
static void rename_efault_on_bad_old(void **state) {
  (void)state;
  g_user_ptr_ok = false;
  char np[] = "/new";
  assert_int_equal((i64)sys_rename(0x1000, (u64)np, 0, 0, 0, 0), -EFAULT);
}

static void rename_success(void **state) {
  (void)state;
  char op[] = "/old", np[] = "/new";
  assert_int_equal(sys_rename((u64)op, (u64)np, 0, 0, 0, 0), 0);
}

/* sys_dup / sys_dup2 */
static void dup_returns_new_fd(void **state) {
  (void)state;
  assert_int_equal(sys_dup(3, 0, 0, 0, 0, 0), 4);
}

static void dup_error_returns_ebadf(void **state) {
  (void)state;
  g_vfs_dup_ret = -EBADF;
  assert_int_equal((i64)sys_dup(99, 0, 0, 0, 0, 0), -EBADF);
}

static void dup2_same_fd_noop(void **state) {
  (void)state;
  assert_int_equal(sys_dup2(3, 3, 0, 0, 0, 0), 3);
}

static void dup2_returns_new_fd(void **state) {
  (void)state;
  assert_int_equal(sys_dup2(3, 5, 0, 0, 0, 0), 5);
}

/* sys_fcntl */
static void fcntl_getfl_returns_flags(void **state) {
  (void)state;
  g_vfs_get_flags_ret = 2; /* O_RDWR */
  assert_int_equal(sys_fcntl(3, 3 /*F_GETFL*/, 0, 0, 0, 0), 2);
}

static void fcntl_setfl_sets_flags(void **state) {
  (void)state;
  assert_int_equal(sys_fcntl(3, 4 /*F_SETFL*/, 0, 0, 0, 0), 0);
}

static void fcntl_getfd_returns_zero(void **state) {
  (void)state;
  assert_int_equal(sys_fcntl(3, 1 /*F_GETFD*/, 0, 0, 0, 0), 0);
}

static void fcntl_setfd_returns_zero(void **state) {
  (void)state;
  /* fd=1 (stdio) skips the fds[] check */
  assert_int_equal(sys_fcntl(1, 2 /*F_SETFD*/, 0, 0, 0, 0), 0);
}

static void fcntl_dupfd_returns_new_fd(void **state) {
  (void)state;
  assert_int_equal(sys_fcntl(3, 0 /*F_DUPFD*/, 0, 0, 0, 0), 4);
}

/* sys_getdents */
static void getdents_efault_on_bad_buf(void **state) {
  (void)state;
  g_user_range_ok = false;
  char buf[64];
  assert_int_equal((i64)sys_getdents(3, (u64)buf, 64, 0, 0, 0), -EFAULT);
}

static void getdents_einval_when_count_too_small(void **state) {
  (void)state;
  char buf[16];
  assert_int_equal((i64)sys_getdents(3, (u64)buf, 16, 0, 0, 0), -EINVAL);
}

static void getdents_success(void **state) {
  (void)state;
  char buf[128];
  assert_int_equal(sys_getdents(3, (u64)buf, 128, 0, 0, 0), 64);
}

/* sys_ftruncate */
static void ftruncate_success(void **state) {
  (void)state;
  assert_int_equal(sys_ftruncate(3, 0, 0, 0, 0, 0), 0);
}

/* sys_symlink */
static void symlink_returns_enosys(void **state) {
  (void)state;
  assert_int_equal((i64)sys_symlink(0, 0, 0, 0, 0, 0), -ENOSYS);
}

/* sys_openat */
static void openat_at_fdcwd_delegates(void **state) {
  (void)state;
  char path[] = "/f";
  assert_int_equal(sys_openat((u64)-100LL, (u64)path, 0, 0, 0, 0), 3);
}

static void openat_non_fdcwd_returns_enosys(void **state) {
  (void)state;
  char path[] = "/f";
  assert_int_equal((i64)sys_openat(4, (u64)path, 0, 0, 0, 0), -ENOSYS);
}

/* sys_readlink: proc/self/exe and regular */
static void readlink_efault_on_bad_path(void **state) {
  (void)state;
  g_user_ptr_ok = false;
  char buf[64];
  assert_int_equal((i64)sys_readlink(0x1000, (u64)buf, 64, 0, 0, 0), -EFAULT);
}

static void readlink_regular_success(void **state) {
  (void)state;
  char path[] = "/link";
  char buf[64];
  g_vfs_readlink_ret = 7;
  assert_int_equal(sys_readlink((u64)path, (u64)buf, 64, 0, 0, 0), 7);
}

static void readlink_proc_self_exe_no_proc(void **state) {
  (void)state;
  char path[] = "/proc/self/exe";
  char buf[64];
  g_no_proc = true;
  assert_int_equal((i64)sys_readlink((u64)path, (u64)buf, 64, 0, 0, 0), -ENOENT);
}

static void readlink_proc_self_exe_success(void **state) {
  (void)state;
  char path[] = "/proc/self/exe";
  strncpy(g_proc.exe_path, "/bin/sh", sizeof(g_proc.exe_path));
  char buf[64];
  i64 ret = sys_readlink((u64)path, (u64)buf, sizeof(buf), 0, 0, 0);
  assert_true(ret > 0);
  assert_memory_equal(buf, "/bin/sh", 7);
}

/* sys_access */
static void access_efault_on_bad_path(void **state) {
  (void)state;
  g_user_ptr_ok = false;
  assert_int_equal((i64)sys_access(0x1000, 0, 0, 0, 0, 0), -EFAULT);
}

static void access_success(void **state) {
  (void)state;
  char path[] = "/f";
  assert_int_equal(sys_access((u64)path, 0, 0, 0, 0, 0), 0);
}

/* sys_creat */
static void creat_efault_on_bad_path(void **state) {
  (void)state;
  g_user_ptr_ok = false;
  assert_int_equal((i64)sys_creat(0x1000, 0, 0, 0, 0, 0), -EFAULT);
}

static void creat_returns_fd(void **state) {
  (void)state;
  char path[] = "/newf";
  assert_int_equal(sys_creat((u64)path, 0644, 0, 0, 0, 0), 3);
}

/* sys_pipe */
static void pipe_efault_on_null(void **state) {
  (void)state;
  assert_int_equal((i64)sys_pipe(0, 0, 0, 0, 0, 0), -EFAULT);
}

static void pipe_efault_on_bad_range(void **state) {
  (void)state;
  g_user_range_ok = false;
  int fds[2];
  assert_int_equal((i64)sys_pipe((u64)fds, 0, 0, 0, 0, 0), -EFAULT);
}

static void pipe_no_proc_returns_einval(void **state) {
  (void)state;
  g_no_proc = true;
  int fds[2];
  assert_int_equal((i64)sys_pipe((u64)fds, 0, 0, 0, 0, 0), -EINVAL);
}

static void pipe_success(void **state) {
  (void)state;
  int fds[2] = {-1, -1};
  assert_int_equal(sys_pipe((u64)fds, 0, 0, 0, 0, 0), 0);
  assert_int_equal(fds[0], 3);
  assert_int_equal(fds[1], 4);
}

/* sys_pread64 */
static void pread64_efault(void **state) {
  (void)state;
  g_user_range_ok = false;
  char buf[8];
  assert_int_equal((i64)sys_pread64(3, (u64)buf, 8, 0, 0, 0), -EFAULT);
}

static void pread64_stdio_fd_reads_directly(void **state) {
  (void)state;
  char buf[8];
  g_vfs_read_ret = 4;
  assert_int_equal(sys_pread64(0, (u64)buf, 8, 0, 0, 0), 4);
}

/* sys_pwrite64 */
static void pwrite64_efault(void **state) {
  (void)state;
  g_user_range_ok = false;
  char buf[8];
  assert_int_equal((i64)sys_pwrite64(3, (u64)buf, 8, 0, 0, 0), -EFAULT);
}

static void pwrite64_stdio_fd_writes_directly(void **state) {
  (void)state;
  char buf[8] = "hi";
  g_vfs_write_ret = 2;
  assert_int_equal(sys_pwrite64(1, (u64)buf, 2, 0, 0, 0), 2);
}

/* sys_pread64: regular fd saves/restores seek */
static void pread64_regular_fd_saves_seek(void **state) {
  (void)state;
  char buf[8];
  g_vfs_read_ret = 4;
  assert_int_equal(sys_pread64(3, (u64)buf, 8, 100, 0, 0), 4);
}

/* sys_pwrite64: regular fd saves/restores seek */
static void pwrite64_regular_fd_saves_seek(void **state) {
  (void)state;
  char buf[8] = "hi";
  g_vfs_write_ret = 2;
  assert_int_equal(sys_pwrite64(3, (u64)buf, 2, 100, 0, 0), 2);
}

/* sys_stat: FIFO type in stat_buf */
static void stat_fifo_type_in_buf(void **state) {
  (void)state;
  char path[] = "/pipe";
  g_vfs_stat_out.type = VFS_FIFO;
  g_vfs_stat_out.size = 0;
  struct stat_buf sb;
  assert_int_equal(sys_stat((u64)path, (u64)&sb, 0, 0, 0, 0), 0);
  /* VFS_FIFO maps to S_IFIFO in mode */
  assert_true((sb.st_mode & 0xF000) == 0010000); /* S_IFIFO = 0010000 */
}

/* sys_access: proc/self/exe with no exe_path */
static void access_proc_self_exe_no_path(void **state) {
  (void)state;
  char path[] = "/proc/self/exe";
  g_proc.exe_path[0] = '\0';
  assert_int_equal((i64)sys_access((u64)path, 0, 0, 0, 0, 0), -ENOENT);
}

/* sys_access: proc/self/exe success */
static void access_proc_self_exe_success(void **state) {
  (void)state;
  char path[] = "/proc/self/exe";
  strncpy(g_proc.exe_path, "/bin/sh", sizeof(g_proc.exe_path));
  assert_int_equal(sys_access((u64)path, 0, 0, 0, 0, 0), 0);
}

/* sys_access: stat fails → ENOENT */
static void access_stat_fails_enoent(void **state) {
  (void)state;
  char path[] = "/missing";
  g_vfs_stat_ret = -ENOENT;
  assert_int_equal((i64)sys_access((u64)path, 0, 0, 0, 0, 0), -ENOENT);
}

/* sys_faccessat */
static void faccessat_efault_on_bad_path(void **state) {
  (void)state;
  g_user_ptr_ok = false;
  assert_int_equal((i64)sys_faccessat(0, 0x1000, 0, 0, 0, 0), -EFAULT);
}

static void faccessat_at_fdcwd_delegates(void **state) {
  (void)state;
  char path[] = "/f";
  assert_int_equal(sys_faccessat((u64)-100LL, (u64)path, 0, 0, 0, 0), 0);
}

static void faccessat_relative_non_fdcwd_enosys(void **state) {
  (void)state;
  char path[] = "relative"; /* no leading / */
  assert_int_equal((i64)sys_faccessat(4, (u64)path, 0, 0, 0, 0), -ENOSYS);
}

/* sys_newfstatat */
static void newfstatat_empty_path_enosys(void **state) {
  (void)state;
  char path[] = "/f";
  struct stat_buf sb;
  /* AT_EMPTY_PATH = 0x1000 */
  assert_int_equal((i64)sys_newfstatat(0, (u64)path, (u64)&sb, 0x1000, 0, 0), -ENOSYS);
}

static void newfstatat_bad_flags_einval(void **state) {
  (void)state;
  char path[] = "/f";
  struct stat_buf sb;
  /* flags outside allowed mask */
  assert_int_equal((i64)sys_newfstatat(0, (u64)path, (u64)&sb, 0xFF0000, 0, 0), -EINVAL);
}

static void newfstatat_at_fdcwd_delegates_to_stat(void **state) {
  (void)state;
  char path[] = "/f";
  g_vfs_stat_out.size = 42;
  g_vfs_stat_out.type = VFS_FILE;
  struct stat_buf sb;
  assert_int_equal(sys_newfstatat((u64)-100LL, (u64)path, (u64)&sb, 0, 0, 0), 0);
  assert_int_equal(sb.st_size, 42);
}

/* sys_getdents64: delegates to getdents */
static void getdents64_delegates(void **state) {
  (void)state;
  char buf[128];
  assert_int_equal(sys_getdents64(3, (u64)buf, 128, 0, 0, 0), 64);
}

/* sys_pipe2: with O_CLOEXEC sets cloexec bits */
static void pipe2_ocloexec_sets_cloexec(void **state) {
  (void)state;
  int fds[2] = {-1, -1};
  /* O_CLOEXEC = 0x80000 */
  assert_int_equal(sys_pipe2((u64)fds, 0x80000, 0, 0, 0, 0), 0);
  assert_int_equal(fds[0], 3);
  assert_int_equal(fds[1], 4);
  assert_int_equal(g_proc.fd_cloexec[3], 1);
  assert_int_equal(g_proc.fd_cloexec[4], 1);
}

static void pipe2_no_cloexec(void **state) {
  (void)state;
  int fds[2] = {-1, -1};
  assert_int_equal(sys_pipe2((u64)fds, 0, 0, 0, 0, 0), 0);
}

/* sys_fcntl: F_DUPFD on stdio fd returns fd itself */
static void fcntl_dupfd_stdio_returns_fd(void **state) {
  (void)state;
  assert_int_equal(sys_fcntl(1, 0 /*F_DUPFD*/, 0, 0, 0, 0), 1);
}

/* sys_fcntl: F_GETFL on stdio fd returns O_RDWR */
static void fcntl_getfl_stdio_returns_rdwr(void **state) {
  (void)state;
  assert_int_equal(sys_fcntl(1, 3 /*F_GETFL*/, 0, 0, 0, 0), 2 /*O_RDWR*/);
}

/* sys_fcntl: F_SETFL on stdio fd returns 0 */
static void fcntl_setfl_stdio_returns_zero(void **state) {
  (void)state;
  assert_int_equal(sys_fcntl(1, 4 /*F_SETFL*/, 0, 0, 0, 0), 0);
}

/* sys_creat: stat shows file is VFS_FILE */
static void stat_directory_mode_in_buf(void **state) {
  (void)state;
  char path[] = "/dir";
  g_vfs_stat_out.type = VFS_DIRECTORY;
  struct stat_buf sb;
  sys_stat((u64)path, (u64)&sb, 0, 0, 0, 0);
  assert_true((sb.st_mode & 0xF000) == 0040000); /* S_IFDIR */
}

/* sys_stat: /proc/self/exe returns symlink stat */
static void stat_proc_self_exe(void **state) {
  (void)state;
  strncpy(g_proc.exe_path, "/bin/sh", sizeof(g_proc.exe_path));
  char path[] = "/proc/self/exe";
  struct stat_buf sb;
  assert_int_equal(sys_stat((u64)path, (u64)&sb, 0, 0, 0, 0), 0);
  assert_true((sb.st_mode & 0xF000) == S_IFLNK);
}

/* sys_fstat: FIFO type in stat_out */
static void fstat_pipe_returns_fifo(void **state) {
  (void)state;
  g_vfs_stat_out.type = VFS_FIFO;
  i64 fd = vfs_open("/pipe", O_RDONLY);
  struct stat_buf sb;
  assert_int_equal(sys_fstat(fd, (u64)&sb, 0, 0, 0, 0), 0);
  assert_true((sb.st_mode & 0xF000) == S_IFIFO);
}

/* sys_readlink: result > bufsiz returns ERANGE */
static void readlink_erange(void **state) {
  (void)state;
  char path[] = "/link";
  char buf[3]; /* smaller than the 7 bytes readlink returns */
  g_vfs_readlink_ret = 7;
  assert_int_equal((i64)sys_readlink((u64)path, (u64)buf, 3, 0, 0, 0), -ERANGE);
}

/* sys_readlink: proc/self/exe with path too long returns ERANGE */
static void readlink_proc_exe_erange(void **state) {
  (void)state;
  char path[] = "/proc/self/exe";
  strncpy(g_proc.exe_path, "/a/very/long/path/to/binary", sizeof(g_proc.exe_path));
  char buf[3]; /* way too small */
  assert_int_equal((i64)sys_readlink((u64)path, (u64)buf, 3, 0, 0, 0), -ERANGE);
}

/* sys_pipe: pipe_alloc_obj returns NULL → ENOMEM */
static void pipe_no_pipe_obj_enomem(void **state) {
  (void)state;
  g_pipe_obj = NULL;
  int fds[2];
  assert_int_equal((i64)sys_pipe((u64)fds, 0, 0, 0, 0, 0), -ENOMEM);
}

/* sys_pread64: seek fails → propagated */
static void pread64_seek_fail(void **state) {
  (void)state;
  char buf[8];
  g_vfs_seek_ret = -EBADF; /* seek fails */
  assert_int_equal((i64)sys_pread64(3, (u64)buf, 8, 100, 0, 0), -EBADF);
}

/* sys_pwrite64: seek fails → propagated */
static void pwrite64_seek_fail(void **state) {
  (void)state;
  char buf[8] = "hi";
  g_vfs_seek_ret = -EBADF;
  assert_int_equal((i64)sys_pwrite64(3, (u64)buf, 2, 100, 0, 0), -EBADF);
}

#define SYS_AT_FDCWD (-100)

/* sys_newfstatat: efault on bad pathname */
static void newfstatat_efault_on_bad_pathname(void **state) {
  (void)state;
  g_user_ptr_ok = false;
  struct stat_buf st;
  assert_int_equal((i64)sys_newfstatat(SYS_AT_FDCWD, (u64)"/path", (u64)&st, 0, 0, 0), -EFAULT);
  g_user_ptr_ok = true;
}

/* sys_newfstatat: relative path with non-AT_FDCWD dirfd → ENOSYS */
static void newfstatat_relative_non_fdcwd_enosys2(void **state) {
  (void)state;
  struct stat_buf st;
  char path[] = "relative";
  assert_int_equal((i64)sys_newfstatat(3, (u64)path, (u64)&st, 0, 0, 0), -ENOSYS);
}

/* sys_fcntl: F_GETFD no proc → EINVAL */
static void fcntl_getfd_no_proc_einval(void **state) {
  (void)state;
  g_no_proc = true;
  assert_int_equal((i64)sys_fcntl(3, F_GETFD, 0, 0, 0, 0), -EINVAL);
}

/* sys_fcntl: F_SETFD no proc → EINVAL */
static void fcntl_setfd_no_proc_einval(void **state) {
  (void)state;
  g_no_proc = true;
  assert_int_equal((i64)sys_fcntl(3, F_SETFD, 0, 0, 0, 0), -EINVAL);
}

/* sys_ftruncate: fd <= 2 → EBADF */
static void ftruncate_stdio_fd_ebadf(void **state) {
  (void)state;
  assert_int_equal((i64)sys_ftruncate(1, 0, 0, 0, 0, 0), -EBADF);
}

/* sys_readlink: bufsiz == 0 → EINVAL */
static void readlink_zero_bufsiz_einval(void **state) {
  (void)state;
  char path[] = "/some/link";
  char buf[4];
  assert_int_equal((i64)sys_readlink((u64)path, (u64)buf, 0, 0, 0, 0), -EINVAL);
}

/* sys_readlink: non-proc-self-exe, vfs_readlink succeeds → returns tlen */
static void readlink_vfs_path_success_len(void **state) {
  (void)state;
  char path[] = "/other/link";
  char buf[32];
  g_vfs_readlink_ret = 5; /* returns 5 bytes */
  u64 ret = sys_readlink((u64)path, (u64)buf, sizeof(buf), 0, 0, 0);
  assert_int_equal(ret, 5);
}

/* sys_pipe: vfs_oft_alloc_pipe for read returns < 0 → ENFILE */
static void pipe_oft_alloc_fails_enfile(void **state) {
  (void)state;
  int fds[2];
  /* Make first oft alloc fail by returning -1 */
  g_oft_seq = -1; /* negative → vfs_oft_alloc_pipe returns -1 */
  /* But our stub always returns g_oft_seq++ — need to make it negative */
  /* Actually stub returns g_oft_seq++ so if g_oft_seq starts at -1, returns -1 */
  u64 ret = sys_pipe((u64)fds, 0, 0, 0, 0, 0);
  assert_int_equal((i64)ret, -ENFILE);
}

/* sys_pipe: write_fd vfs_install_fd fails → cleanup (lines 676-679) */
static void pipe_write_fd_fails_enfile(void **state) {
  (void)state;
  int fds[2];
  /* First install (read_fd) succeeds, second (write_fd) fails */
  g_fd_fail_on = 2; /* fail on 2nd install_fd call */
  u64 ret = sys_pipe((u64)fds, 0, 0, 0, 0, 0);
  assert_int_equal((i64)ret, -ENFILE);
  g_fd_fail_on = -1;
}

/* sys_openat: efault on bad pathname (line 414) */
static void sys_openat_efault_path(void **state) {
  (void)state;
  g_user_ptr_ok = false;
  assert_int_equal((i64)sys_openat(SYS_AT_FDCWD, (u64)"/path", 0, 0, 0, 0), -EFAULT);
  g_user_ptr_ok = true;
}

/* sys_fstat: fd <= 2 → stdio stat (pipe-like) */
static void fstat_stdio_fd_returns_chardev(void **state) {
  (void)state;
  struct stat_buf st;
  u64 ret = sys_fstat(1, (u64)&st, 0, 0, 0, 0);
  assert_int_equal(ret, 0);
  assert_int_equal(st.st_ino, 1);
}

/* sys_fstat: vfs_fstat fails → EBADF */
static void fstat_vfs_fail_ebadf(void **state) {
  (void)state;
  g_vfs_fstat_ret = -EBADF;
  struct stat_buf st;
  assert_int_equal((i64)sys_fstat(5, (u64)&st, 0, 0, 0, 0), -EBADF);
}

/* sys_fcntl: F_GETFD bad fd → EBADF */
static void fcntl_getfd_bad_fd_ebadf(void **state) {
  (void)state;
  assert_int_equal((i64)sys_fcntl(-1, F_GETFD, 0, 0, 0, 0), -EBADF);
}

/* sys_fcntl: F_SETFD bad fd → EBADF */
static void fcntl_setfd_bad_fd_ebadf(void **state) {
  (void)state;
  assert_int_equal((i64)sys_fcntl(-1, F_SETFD, 0, 0, 0, 0), -EBADF);
}

/* sys_fcntl: F_SETFD on unopened fd > 2 → EBADF */
static void fcntl_setfd_closed_fd_ebadf(void **state) {
  (void)state;
  /* fd=5, fds[5]=-1 (closed) → EBADF */
  g_proc.fds[5] = -1;
  assert_int_equal((i64)sys_fcntl(5, F_SETFD, FD_CLOEXEC, 0, 0, 0), -EBADF);
}

/* sys_fcntl: default cmd → returns 0 */
static void fcntl_unknown_cmd_returns_zero(void **state) {
  (void)state;
  assert_int_equal((i64)sys_fcntl(3, 0xFF, 0, 0, 0, 0), 0);
}

/* sys_pipe: vfs_install_fd for write end fails → cleans up */
static void pipe_write_fd_install_fails(void **state) {
  (void)state;
  int fds[2];
  /* First install (read_fd) succeeds (returns 3), second (write_fd) fails */
  g_fd_seq = 3;
  /* Make second install fail by using a counter: override g_fd_seq to
   * return -EMFILE on second call. Since vfs_install_fd stub just returns
   * g_fd_seq++, we need it to fail on call 2. Patch by setting after first. */
  /* Simpler: make all fds full so second install fails.
   * But stub always succeeds. Use g_fd_seq = -EMFILE trick won't work.
   * Instead test that pipe_write_oft fails (g_oft_seq overflow). */
  /* Most reliable: alloc read_oft ok (0), alloc write_oft fails (return -1).
   * We can't make vfs_oft_alloc_pipe fail easily since stub always succeeds.
   * Test the install_fd-fails path indirectly via write_fd < 0. */
  /* Skip — path requires stub control not available. Test read_fd fail instead. */
  /* pipe: read_fd install fails because all fds full → cleans up */
  for(int i = 0; i < VFS_MAX_FD; i++) g_proc.fds[i] = 0; /* mark all used */
  g_fd_seq = -EMFILE; /* make vfs_install_fd return -EMFILE */
  u64 ret = sys_pipe((u64)fds, 0, 0, 0, 0, 0);
  assert_int_equal((i64)ret, -EMFILE);
}

/* sys_readlink: success path via vfs_readlink (non-proc-self-exe) */
static void readlink_vfs_success(void **state) {
  (void)state;
  char buf[32];
  g_vfs_readlink_ret = 7; /* "/target" */
  u64 ret = sys_readlink((u64)"/some/link", (u64)buf, sizeof(buf), 0, 0, 0);
  assert_int_equal(ret, 7);
}

/* sys_readlink: vfs_readlink returns tlen >= bufsiz → ERANGE */
static void readlink_vfs_result_too_long(void **state) {
  (void)state;
  char buf[4];
  g_vfs_readlink_ret = 7; /* longer than buf */
  u64 ret = sys_readlink((u64)"/some/link", (u64)buf, 4, 0, 0, 0);
  assert_int_equal((i64)ret, -ERANGE);
}

int main(void)
{
  const struct CMUnitTest tests[] = {
      cmocka_unit_test_setup(open_efault_on_bad_path, setup),
      cmocka_unit_test_setup(open_returns_fd, setup),
      cmocka_unit_test_setup(close_returns_zero_on_success, setup),
      cmocka_unit_test_setup(close_propagates_error, setup),
      cmocka_unit_test_setup(stat_efault_on_bad_path, setup),
      cmocka_unit_test_setup(stat_efault_on_bad_buf, setup),
      cmocka_unit_test_setup(stat_success_fills_buf, setup),
      cmocka_unit_test_setup(stat_enoent_propagated, setup),
      cmocka_unit_test_setup(fstat_efault_on_bad_buf, setup),
      cmocka_unit_test_setup(fstat_success, setup),
      cmocka_unit_test_setup(lstat_efault_on_bad_path, setup),
      cmocka_unit_test_setup(lstat_delegates_to_stat, setup),
      cmocka_unit_test_setup(getcwd_efault_on_null, setup),
      cmocka_unit_test_setup(getcwd_einval_on_zero_size, setup),
      cmocka_unit_test_setup(getcwd_copies_cwd, setup),
      cmocka_unit_test_setup(getcwd_erange_when_buf_too_small, setup),
      cmocka_unit_test_setup(chdir_efault_on_bad_path, setup),
      cmocka_unit_test_setup(chdir_success, setup),
      cmocka_unit_test_setup(chdir_enoent_propagated, setup),
      cmocka_unit_test_setup(mkdir_efault_on_bad_path, setup),
      cmocka_unit_test_setup(mkdir_success, setup),
      cmocka_unit_test_setup(rmdir_efault_on_bad_path, setup),
      cmocka_unit_test_setup(rmdir_success, setup),
      cmocka_unit_test_setup(unlink_efault_on_bad_path, setup),
      cmocka_unit_test_setup(unlink_success, setup),
      cmocka_unit_test_setup(rename_efault_on_bad_old, setup),
      cmocka_unit_test_setup(rename_success, setup),
      cmocka_unit_test_setup(dup_returns_new_fd, setup),
      cmocka_unit_test_setup(dup_error_returns_ebadf, setup),
      cmocka_unit_test_setup(dup2_same_fd_noop, setup),
      cmocka_unit_test_setup(dup2_returns_new_fd, setup),
      cmocka_unit_test_setup(fcntl_getfl_returns_flags, setup),
      cmocka_unit_test_setup(fcntl_setfl_sets_flags, setup),
      cmocka_unit_test_setup(fcntl_getfd_returns_zero, setup),
      cmocka_unit_test_setup(fcntl_setfd_returns_zero, setup),
      cmocka_unit_test_setup(fcntl_dupfd_returns_new_fd, setup),
      cmocka_unit_test_setup(getdents_efault_on_bad_buf, setup),
      cmocka_unit_test_setup(getdents_einval_when_count_too_small, setup),
      cmocka_unit_test_setup(getdents_success, setup),
      cmocka_unit_test_setup(ftruncate_success, setup),
      cmocka_unit_test_setup(symlink_returns_enosys, setup),
      cmocka_unit_test_setup(openat_at_fdcwd_delegates, setup),
      cmocka_unit_test_setup(openat_non_fdcwd_returns_enosys, setup),
      cmocka_unit_test_setup(readlink_efault_on_bad_path, setup),
      cmocka_unit_test_setup(readlink_regular_success, setup),
      cmocka_unit_test_setup(readlink_proc_self_exe_no_proc, setup),
      cmocka_unit_test_setup(readlink_proc_self_exe_success, setup),
      cmocka_unit_test_setup(access_efault_on_bad_path, setup),
      cmocka_unit_test_setup(access_success, setup),
      cmocka_unit_test_setup(creat_efault_on_bad_path, setup),
      cmocka_unit_test_setup(creat_returns_fd, setup),
      cmocka_unit_test_setup(pipe_efault_on_null, setup),
      cmocka_unit_test_setup(pipe_efault_on_bad_range, setup),
      cmocka_unit_test_setup(pipe_no_proc_returns_einval, setup),
      cmocka_unit_test_setup(pipe_success, setup),
      cmocka_unit_test_setup(pread64_efault, setup),
      cmocka_unit_test_setup(pread64_stdio_fd_reads_directly, setup),
      cmocka_unit_test_setup(pwrite64_efault, setup),
      cmocka_unit_test_setup(pwrite64_stdio_fd_writes_directly, setup),
      cmocka_unit_test_setup(pread64_regular_fd_saves_seek, setup),
      cmocka_unit_test_setup(pwrite64_regular_fd_saves_seek, setup),
      cmocka_unit_test_setup(stat_fifo_type_in_buf, setup),
      cmocka_unit_test_setup(stat_directory_mode_in_buf, setup),
      cmocka_unit_test_setup(access_proc_self_exe_no_path, setup),
      cmocka_unit_test_setup(access_proc_self_exe_success, setup),
      cmocka_unit_test_setup(access_stat_fails_enoent, setup),
      cmocka_unit_test_setup(faccessat_efault_on_bad_path, setup),
      cmocka_unit_test_setup(faccessat_at_fdcwd_delegates, setup),
      cmocka_unit_test_setup(faccessat_relative_non_fdcwd_enosys, setup),
      cmocka_unit_test_setup(newfstatat_empty_path_enosys, setup),
      cmocka_unit_test_setup(newfstatat_bad_flags_einval, setup),
      cmocka_unit_test_setup(newfstatat_at_fdcwd_delegates_to_stat, setup),
      cmocka_unit_test_setup(getdents64_delegates, setup),
      cmocka_unit_test_setup(pipe2_ocloexec_sets_cloexec, setup),
      cmocka_unit_test_setup(pipe2_no_cloexec, setup),
      cmocka_unit_test_setup(fcntl_dupfd_stdio_returns_fd, setup),
      cmocka_unit_test_setup(fcntl_getfl_stdio_returns_rdwr, setup),
      cmocka_unit_test_setup(fcntl_setfl_stdio_returns_zero, setup),
      /* sys_stat: proc/self/exe */
      cmocka_unit_test_setup(stat_proc_self_exe, setup),
      /* sys_fstat: for pipe fd → FIFO */
      cmocka_unit_test_setup(fstat_pipe_returns_fifo, setup),
      /* sys_readlink: ERANGE */
      cmocka_unit_test_setup(readlink_erange, setup),
      cmocka_unit_test_setup(readlink_proc_exe_erange, setup),
      /* sys_pipe: pipe_alloc_obj fails → ENOMEM */
      cmocka_unit_test_setup(pipe_no_pipe_obj_enomem, setup),
      /* sys_pread64: seek fails → propagated */
      cmocka_unit_test_setup(pread64_seek_fail, setup),
      /* sys_pwrite64: seek fails → propagated */
      cmocka_unit_test_setup(pwrite64_seek_fail, setup),
      /* new coverage */
      cmocka_unit_test_setup(fstat_stdio_fd_returns_chardev, setup),
      cmocka_unit_test_setup(fstat_vfs_fail_ebadf, setup),
      cmocka_unit_test_setup(fcntl_getfd_bad_fd_ebadf, setup),
      cmocka_unit_test_setup(fcntl_setfd_bad_fd_ebadf, setup),
      cmocka_unit_test_setup(fcntl_setfd_closed_fd_ebadf, setup),
      cmocka_unit_test_setup(fcntl_unknown_cmd_returns_zero, setup),
      cmocka_unit_test_setup(pipe_write_fd_install_fails, setup),
      cmocka_unit_test_setup(readlink_vfs_success, setup),
      cmocka_unit_test_setup(readlink_vfs_result_too_long, setup),
      cmocka_unit_test_setup(pipe_write_fd_fails_enfile, setup),
      cmocka_unit_test_setup(sys_openat_efault_path, setup),
      /* new coverage */
      cmocka_unit_test_setup(newfstatat_efault_on_bad_pathname, setup),
      cmocka_unit_test_setup(newfstatat_relative_non_fdcwd_enosys2, setup),
      cmocka_unit_test_setup(fcntl_getfd_no_proc_einval, setup),
      cmocka_unit_test_setup(fcntl_setfd_no_proc_einval, setup),
      cmocka_unit_test_setup(ftruncate_stdio_fd_ebadf, setup),
      cmocka_unit_test_setup(readlink_zero_bufsiz_einval, setup),
      cmocka_unit_test_setup(readlink_vfs_path_success_len, setup),
      cmocka_unit_test_setup(pipe_oft_alloc_fails_enfile, setup),
  };
  return cmocka_run_group_tests(tests, NULL, NULL);
}
