#include "test_common.h"
#include <alcor2/types.h>
#include <alcor2/errno.h>

#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

void *kmalloc(u64 n) { return malloc((size_t)n); }
void  kfree(void *p) { free(p); }
void *kmemcpy(void *d, const void *s, u64 n) { return memcpy(d, s, (size_t)n); }
void  kzero(void *d, u64 n) { memset(d, 0, (size_t)n); }
u64   kstrlen(const char *s) { return strlen(s); }
bool  kstreq(const char *a, const char *b) { return strcmp(a, b) == 0; }
int   kstrncmp(const char *a, const char *b, u64 n) { return strncmp(a, b, (size_t)n); }
char *kstrncpy(char *d, const char *s, u64 m) { if(!m) return d; u64 i; for(i=0;i<m-1&&s[i];i++) d[i]=s[i]; d[i]='\0'; return d; }
u64   kstrlcat(char *dst, const char *src, u64 size) {
  u64 dlen = strlen(dst);
  u64 slen = strlen(src);
  if (dlen >= size) return size + slen;
  u64 copy = size - dlen - 1;
  if (copy > slen) copy = slen;
  memcpy(dst + dlen, src, copy);
  dst[dlen + copy] = '\0';
  return dlen + slen;
}

#include <alcor2/proc/proc.h>
#include <alcor2/fs/pipe.h>

static proc_t g_proc;
proc_t *proc_current(void) { return &g_proc; }

/* Stubs for pipes */
void pipe_rd_release(void *p) { (void)p; }
void pipe_wr_release(void *p) { (void)p; }
i64 pipe_read_obj(void *p, void *b, u64 c) { (void)p;(void)b;(void)c; return 0; }
i64 pipe_write_obj(void *p, const void *b, u64 c) { (void)p;(void)b;(void)c; return 0; }
bool pipe_poll_read_ready(const void *p) { (void)p; return false; }
bool pipe_poll_write_ready(const void *p) { (void)p; return false; }

#include "../../src/fs/vfs.c"

/* Dummy FS driver */
static char g_last_rel_path[VFS_PATH_MAX];
static i64 g_readdir_ret = 0;

static fs_handle_t dummy_open(void *fs_data, const char *path, u32 flags) {
  (void)fs_data; (void)flags;
  kstrncpy(g_last_rel_path, path, sizeof(g_last_rel_path));
  if (path[0] != '/') return NULL; /* Simulate driver requirement */
  return (fs_handle_t)1;
}

static i64 dummy_stat(const void *fs_data, const char *path, vfs_stat_t *st) {
  (void)fs_data; (void)path;
  kzero(st, sizeof(*st));
  st->type = VFS_DIRECTORY;
  return 0;
}

static i64 dummy_readdir(fs_handle_t fh, u64 offset, char *name, vfs_stat_t *st) {
  (void)fh; (void)offset; (void)name; (void)st;
  return g_readdir_ret;
}

static void dummy_close(fs_handle_t fh) { (void)fh; }

static i64 dummy_read(fs_handle_t fh, void *buf, u64 count, u64 offset) {
  (void)fh; (void)offset;
  if (count > 0 && buf) {
    kzero(buf, count);
    return count;
  }
  return 0;
}

static i64 dummy_write(fs_handle_t fh, const void *buf, u64 count, u64 offset) {
  (void)fh; (void)buf; (void)offset;
  return count;
}

static i64 dummy_ioctl(fs_handle_t fh, u64 req, u64 arg) {
  (void)fh; (void)req; (void)arg;
  return 0;
}

static i64 dummy_fstat(fs_handle_t fh, vfs_stat_t *st) {
  (void)fh;
  kzero(st, sizeof(*st));
  st->type = VFS_FILE;
  st->size = 1024;
  return 0;
}

static i64 dummy_mkdir(void *fs_data, const char *path) {
  (void)fs_data; (void)path;
  return 0;
}

static i64 dummy_unlink(void *fs_data, const char *path) {
  (void)fs_data; (void)path;
  return 0;
}

static i64 dummy_rmdir(void *fs_data, const char *path) {
  (void)fs_data; (void)path;
  return 0;
}

static const fs_ops_t dummy_ops = {
  .open = dummy_open,
  .read = dummy_read,
  .write = dummy_write,
  .stat = dummy_stat,
  .fstat = dummy_fstat,
  .readdir = dummy_readdir,
  .close = dummy_close,
  .ioctl = dummy_ioctl,
  .mkdir = dummy_mkdir,
  .unlink = dummy_unlink,
  .rmdir = dummy_rmdir
};

static void *dummy_mount_cb(const char *source, u32 flags) {
  (void)source; (void)flags;
  return (void*)1;
}

static const fs_type_t dummy_fstype = {
  .name = "dummy",
  .ops = &dummy_ops,
  .mount = dummy_mount_cb
};

static int setup_vfs(void **state) {
  (void)state;
  memset(&g_proc, 0, sizeof(g_proc));
  for (int i = 0; i < VFS_MAX_FD; i++) g_proc.fds[i] = -1;
  strcpy(g_proc.cwd, "/");
  vfs_init();
  fs_registry_count = 0;
  vfs_register_fs(&dummy_fstype);
  memset(g_last_rel_path, 0, sizeof(g_last_rel_path));
  return 0;
}

static void vfs_normalize_removes_dots(void **state) {
  (void)state;
  char path[VFS_PATH_MAX] = "/foo/./bar/../baz";
  vfs_normalize(path);
  assert_string_equal(path, "/foo/baz");
}

static void vfs_normalize_collapses_slashes(void **state) {
  (void)state;
  char path[VFS_PATH_MAX] = "//foo///bar//";
  vfs_normalize(path);
  assert_string_equal(path, "/foo/bar");
}

static void vfs_normalize_handles_root_dotdots(void **state) {
  (void)state;
  char path[VFS_PATH_MAX] = "/../../foo";
  vfs_normalize(path);
  assert_string_equal(path, "/foo");
}

static void vfs_mount_root_open_rel_path_has_slash(void **state) {
  (void)state;
  /* Mount at root */
  assert_int_equal(vfs_mount("dev", "/", "dummy"), 0);
  
  /* Open a file. Expected rel_path passed to driver is "/foo", 
     but due to bug it might be "foo" */
  i64 fd = vfs_open("/foo", 0);
  
  /* If the driver required a leading slash, it returned NULL, making vfs_open fail */
  assert_true(fd >= 3); 
  
  /* Verify rel_path */
  assert_string_equal(g_last_rel_path, "/foo");
}

static void vfs_mount_subdir_open_rel_path_has_slash(void **state) {
  (void)state;
  assert_int_equal(vfs_mount("dev", "/mnt", "dummy"), 0);
  
  i64 fd = vfs_open("/mnt/foo", 0);
  assert_true(fd >= 3);
  assert_string_equal(g_last_rel_path, "/foo");
}

static void vfs_getdents_returns_error_on_first_failure(void **state) {
  (void)state;
  assert_int_equal(vfs_mount("dev", "/mnt", "dummy"), 0);
  i64 fd = vfs_open("/mnt/dir", 0);
  assert_true(fd >= 3);
  
  g_readdir_ret = -EIO;
  u8 buf[1024];
  assert_int_equal(vfs_getdents(fd, buf, sizeof(buf)), -EIO);
}

static void vfs_chdir_updates_cwd(void **state) {
  (void)state;
  vfs_mount("dev", "/mnt", "dummy");
  assert_int_equal(vfs_chdir("/mnt"), 0);
  assert_string_equal(g_proc.cwd, "/mnt");
}

static void vfs_close_clears_fd(void **state) {
  (void)state;
  vfs_mount("dev", "/", "dummy");
  i64 fd = vfs_open("/foo", 0);
  assert_true(fd >= 3);
  assert_int_equal(vfs_close(fd), 0);
  assert_int_equal(g_proc.fds[fd], -1);
}

static void vfs_dup2_closes_existing_fd(void **state) {
  (void)state;
  vfs_mount("dev", "/", "dummy");
  i64 fd1 = vfs_open("/foo", 0);
  i64 fd2 = vfs_open("/bar", 0);
  assert_int_equal(vfs_dup2(fd1, fd2), fd2);
  assert_int_equal(g_proc.fds[fd1], g_proc.fds[fd2]);
}

static void vfs_dup2_copies_fd(void **state) {
  (void)state;
  vfs_mount("dev", "/", "dummy");
  i64 fd = vfs_open("/foo", 0);
  assert_int_equal(vfs_dup2(fd, 10), 10);
  assert_int_equal(g_proc.fds[fd], g_proc.fds[10]);
}

static void vfs_dup2_same_fd_noop(void **state) {
  (void)state;
  vfs_mount("dev", "/", "dummy");
  i64 fd = vfs_open("/foo", 0);
  assert_int_equal(vfs_dup2(fd, fd), fd);
}

static void vfs_dup_allocates_lowest_fd(void **state) {
  (void)state;
  vfs_mount("dev", "/", "dummy");
  i64 fd1 = vfs_open("/foo", 0);
  i64 fd2 = vfs_dup(fd1);
  assert_true(fd2 > fd1);
  assert_int_equal(g_proc.fds[fd1], g_proc.fds[fd2]);
}

static void vfs_dup_retains_oft(void **state) {
  (void)state;
  vfs_mount("dev", "/", "dummy");
  i64 fd1 = vfs_open("/foo", 0);
  i32 oft_idx = g_proc.fds[fd1];
  i32 initial_refcount = oft[oft_idx].refcount;
  i64 fd2 = vfs_dup(fd1);
  assert_int_equal(oft[oft_idx].refcount, initial_refcount + 1);
  vfs_close(fd2);
}

static void vfs_ioctl_dispatches_to_driver(void **state) {
  (void)state;
  vfs_mount("dev", "/", "dummy");
  i64 fd = vfs_open("/foo", 0);
  assert_int_equal(vfs_ioctl(fd, 0x1234, 0x5678), 0);
}

static void vfs_mkdir_dispatches_to_driver(void **state) {
  (void)state;
  vfs_mount("dev", "/", "dummy");
  assert_int_equal(vfs_mkdir("/foo"), 0);
}

static void vfs_open_allocates_fd(void **state) {
  (void)state;
  vfs_mount("dev", "/", "dummy");
  i64 fd = vfs_open("/foo", 0);
  assert_true(fd >= 3);
  assert_true(g_proc.fds[fd] >= 0);
}

static void vfs_open_allocates_oft(void **state) {
  (void)state;
  vfs_mount("dev", "/", "dummy");
  i64 fd = vfs_open("/foo", 0);
  i32 oft_idx = g_proc.fds[fd];
  assert_true(oft[oft_idx].in_use);
}

static void vfs_open_cloexec_flag(void **state) {
  (void)state;
  vfs_mount("dev", "/", "dummy");
  i64 fd = vfs_open("/foo", O_CLOEXEC);
  assert_int_equal(g_proc.fd_cloexec[fd], 1);
}

static void vfs_read_advances_offset(void **state) {
  (void)state;
  vfs_mount("dev", "/", "dummy");
  i64 fd = vfs_open("/foo", 0);
  char buf[10];
  assert_int_equal(vfs_read(fd, buf, 10), 10);
  i32 oft_idx = g_proc.fds[fd];
  assert_int_equal(oft[oft_idx].offset, 10);
}

static void vfs_rmdir_dispatches_to_driver(void **state) {
  (void)state;
  vfs_mount("dev", "/", "dummy");
  assert_int_equal(vfs_rmdir("/foo"), 0);
}

static void vfs_seek_cur(void **state) {
  (void)state;
  vfs_mount("dev", "/", "dummy");
  i64 fd = vfs_open("/foo", 0);
  vfs_seek(fd, 5, SEEK_SET);
  assert_int_equal(vfs_seek(fd, 5, SEEK_CUR), 10);
  i32 oft_idx = g_proc.fds[fd];
  assert_int_equal(oft[oft_idx].offset, 10);
}

static void vfs_seek_end(void **state) {
  (void)state;
  vfs_mount("dev", "/", "dummy");
  i64 fd = vfs_open("/foo", 0);
  assert_int_equal(vfs_seek(fd, 0, SEEK_END), 1024);
  i32 oft_idx = g_proc.fds[fd];
  assert_int_equal(oft[oft_idx].offset, 1024);
}

static void vfs_seek_set(void **state) {
  (void)state;
  vfs_mount("dev", "/", "dummy");
  i64 fd = vfs_open("/foo", 0);
  assert_int_equal(vfs_seek(fd, 100, SEEK_SET), 100);
  i32 oft_idx = g_proc.fds[fd];
  assert_int_equal(oft[oft_idx].offset, 100);
}

static void vfs_unlink_dispatches_to_driver(void **state) {
  (void)state;
  vfs_mount("dev", "/", "dummy");
  assert_int_equal(vfs_unlink("/foo"), 0);
}

static void vfs_write_advances_offset(void **state) {
  (void)state;
  vfs_mount("dev", "/", "dummy");
  i64 fd = vfs_open("/foo", 0);
  char buf[10] = {0};
  assert_int_equal(vfs_write(fd, buf, 10), 10);
  i32 oft_idx = g_proc.fds[fd];
  assert_int_equal(oft[oft_idx].offset, 10);
}

static void vfs_write_append_updates_offset(void **state) {
  (void)state;
  vfs_mount("dev", "/", "dummy");
  i64 fd = vfs_open("/foo", O_APPEND);
  char buf[10] = {0};
  assert_int_equal(vfs_write(fd, buf, 10), 10);
  i32 oft_idx = g_proc.fds[fd];
  assert_int_equal(oft[oft_idx].offset, 1024 + 10);
}

static void vfs_fd_is_pipe_false_for_file(void **state) {
  (void)state;
  vfs_mount("dev", "/", "dummy");
  i64 fd = vfs_open("/foo", 0);
  assert_false(vfs_fd_is_pipe(fd));
}

static void vfs_fd_is_pipe_true_for_pipe(void **state) {
  (void)state;
  i32 oft_idx = vfs_oft_alloc_pipe(VFS_KIND_PIPE_RD, (void*)0x1234);
  i64 fd = vfs_install_fd(oft_idx);
  assert_true(vfs_fd_is_pipe(fd));
}

static void vfs_fd_is_valid_false_for_closed(void **state) {
  (void)state;
  assert_false(vfs_fd_is_valid(10));
}

static void vfs_fd_is_valid_true_for_open(void **state) {
  (void)state;
  vfs_mount("dev", "/", "dummy");
  i64 fd = vfs_open("/foo", 0);
  assert_true(vfs_fd_is_valid(fd));
}

static void vfs_install_fd_finds_lowest_available(void **state) {
  (void)state;
  g_proc.fds[3] = 100;
  g_proc.fds[4] = -1;
  i64 fd = vfs_install_fd(200);
  assert_int_equal(fd, 4);
  assert_int_equal(g_proc.fds[4], 200);
}

static void vfs_oft_alloc_pipe_sets_kind_and_pipe(void **state) {
  (void)state;
  i32 oft_idx = vfs_oft_alloc_pipe(VFS_KIND_PIPE_WR, (void*)0x5678);
  assert_true(oft_idx >= 0);
  assert_int_equal(oft[oft_idx].kind, VFS_KIND_PIPE_WR);
  assert_ptr_equal(oft[oft_idx].pipe, (void*)0x5678);
}

static void vfs_proc_close_cloexec_fds_closes_only_cloexec(void **state) {
  (void)state;
  vfs_mount("dev", "/", "dummy");
  i64 fd1 = vfs_open("/foo", 0);
  i64 fd2 = vfs_open("/bar", O_CLOEXEC);
  vfs_proc_close_cloexec_fds();
  assert_true(g_proc.fds[fd1] >= 0);
  assert_int_equal(g_proc.fds[fd2], -1);
}

static void vfs_proc_inherit_fds_copies_and_retains(void **state) {
  (void)state;
  vfs_mount("dev", "/", "dummy");
  i64 fd = vfs_open("/foo", 0);
  i32 oft_idx = g_proc.fds[fd];
  i32 initial_refcount = oft[oft_idx].refcount;

  i32 child_fds[VFS_MAX_FD];
  u8 child_clox[VFS_MAX_FD];
  vfs_proc_init_fds(child_fds);

  vfs_proc_inherit_fds(child_fds, child_clox, g_proc.fds, g_proc.fd_cloexec);

  assert_int_equal(child_fds[fd], oft_idx);
  assert_int_equal(oft[oft_idx].refcount, initial_refcount + 1);
}

static void vfs_proc_init_fds_sets_all_to_minus_one(void **state) {
  (void)state;
  i32 test_fds[VFS_MAX_FD];
  test_fds[0] = 5;
  vfs_proc_init_fds(test_fds);
  for (int i = 0; i < VFS_MAX_FD; i++) {
    assert_int_equal(test_fds[i], -1);
  }
}

static void vfs_proc_release_fds_releases_all(void **state) {
  (void)state;
  vfs_mount("dev", "/", "dummy");
  i64 fd = vfs_open("/foo", 0);
  i32 oft_idx = g_proc.fds[fd];
  
  vfs_proc_release_fds(g_proc.fds);
  assert_int_equal(g_proc.fds[fd], -1);
  assert_false(oft[oft_idx].in_use);
}

static void vfs_read_fails_on_pipe_wr(void **state) {
  (void)state;
  i32 oft_idx = vfs_oft_alloc_pipe(VFS_KIND_PIPE_WR, (void*)0x1234);
  i64 fd = vfs_install_fd(oft_idx);
  char buf[10];
  assert_int_equal(vfs_read(fd, buf, 10), -EBADF);
}

static void vfs_select_read_ready_ebadf_for_pipe_wr(void **state) {
  (void)state;
  i32 oft_idx = vfs_oft_alloc_pipe(VFS_KIND_PIPE_WR, (void*)0x1234);
  i64 fd = vfs_install_fd(oft_idx);
  assert_int_equal(vfs_select_read_ready(fd), -EBADF);
}

static void vfs_select_read_ready_true_for_file(void **state) {
  (void)state;
  vfs_mount("dev", "/", "dummy");
  i64 fd = vfs_open("/foo", 0);
  assert_int_equal(vfs_select_read_ready(fd), 1);
}

static void vfs_select_write_ready_ebadf_for_pipe_rd(void **state) {
  (void)state;
  i32 oft_idx = vfs_oft_alloc_pipe(VFS_KIND_PIPE_RD, (void*)0x1234);
  i64 fd = vfs_install_fd(oft_idx);
  assert_int_equal(vfs_select_write_ready(fd), -EBADF);
}

static void vfs_select_write_ready_true_for_file(void **state) {
  (void)state;
  vfs_mount("dev", "/", "dummy");
  i64 fd = vfs_open("/foo", 0);
  assert_int_equal(vfs_select_write_ready(fd), 1);
}

static void vfs_write_fails_on_pipe_rd(void **state) {
  (void)state;
  i32 oft_idx = vfs_oft_alloc_pipe(VFS_KIND_PIPE_RD, (void*)0x1234);
  i64 fd = vfs_install_fd(oft_idx);
  char buf[10];
  assert_int_equal(vfs_write(fd, buf, 10), -EBADF);
}

int main(void) {
  const struct CMUnitTest tests[] = {
      cmocka_unit_test_setup(vfs_chdir_updates_cwd, setup_vfs),
      cmocka_unit_test_setup(vfs_close_clears_fd, setup_vfs),
      cmocka_unit_test_setup(vfs_dup2_closes_existing_fd, setup_vfs),
      cmocka_unit_test_setup(vfs_dup2_copies_fd, setup_vfs),
      cmocka_unit_test_setup(vfs_dup2_same_fd_noop, setup_vfs),
      cmocka_unit_test_setup(vfs_dup_allocates_lowest_fd, setup_vfs),
      cmocka_unit_test_setup(vfs_dup_retains_oft, setup_vfs),
      cmocka_unit_test_setup(vfs_fd_is_pipe_false_for_file, setup_vfs),
      cmocka_unit_test_setup(vfs_fd_is_pipe_true_for_pipe, setup_vfs),
      cmocka_unit_test_setup(vfs_fd_is_valid_false_for_closed, setup_vfs),
      cmocka_unit_test_setup(vfs_fd_is_valid_true_for_open, setup_vfs),
      cmocka_unit_test_setup(vfs_getdents_returns_error_on_first_failure, setup_vfs),
      cmocka_unit_test_setup(vfs_install_fd_finds_lowest_available, setup_vfs),
      cmocka_unit_test_setup(vfs_ioctl_dispatches_to_driver, setup_vfs),
      cmocka_unit_test_setup(vfs_mkdir_dispatches_to_driver, setup_vfs),
      cmocka_unit_test_setup(vfs_mount_root_open_rel_path_has_slash, setup_vfs),
      cmocka_unit_test_setup(vfs_mount_subdir_open_rel_path_has_slash, setup_vfs),
      cmocka_unit_test_setup(vfs_normalize_collapses_slashes, setup_vfs),
      cmocka_unit_test_setup(vfs_normalize_handles_root_dotdots, setup_vfs),
      cmocka_unit_test_setup(vfs_normalize_removes_dots, setup_vfs),
      cmocka_unit_test_setup(vfs_oft_alloc_pipe_sets_kind_and_pipe, setup_vfs),
      cmocka_unit_test_setup(vfs_open_allocates_fd, setup_vfs),
      cmocka_unit_test_setup(vfs_open_allocates_oft, setup_vfs),
      cmocka_unit_test_setup(vfs_open_cloexec_flag, setup_vfs),
      cmocka_unit_test_setup(vfs_proc_close_cloexec_fds_closes_only_cloexec, setup_vfs),
      cmocka_unit_test_setup(vfs_proc_inherit_fds_copies_and_retains, setup_vfs),
      cmocka_unit_test_setup(vfs_proc_init_fds_sets_all_to_minus_one, setup_vfs),
      cmocka_unit_test_setup(vfs_proc_release_fds_releases_all, setup_vfs),
      cmocka_unit_test_setup(vfs_read_advances_offset, setup_vfs),
      cmocka_unit_test_setup(vfs_read_fails_on_pipe_wr, setup_vfs),
      cmocka_unit_test_setup(vfs_rmdir_dispatches_to_driver, setup_vfs),
      cmocka_unit_test_setup(vfs_seek_cur, setup_vfs),
      cmocka_unit_test_setup(vfs_seek_end, setup_vfs),
      cmocka_unit_test_setup(vfs_seek_set, setup_vfs),
      cmocka_unit_test_setup(vfs_select_read_ready_ebadf_for_pipe_wr, setup_vfs),
      cmocka_unit_test_setup(vfs_select_read_ready_true_for_file, setup_vfs),
      cmocka_unit_test_setup(vfs_select_write_ready_ebadf_for_pipe_rd, setup_vfs),
      cmocka_unit_test_setup(vfs_select_write_ready_true_for_file, setup_vfs),
      cmocka_unit_test_setup(vfs_unlink_dispatches_to_driver, setup_vfs),
      cmocka_unit_test_setup(vfs_write_advances_offset, setup_vfs),
      cmocka_unit_test_setup(vfs_write_append_updates_offset, setup_vfs),
      cmocka_unit_test_setup(vfs_write_fails_on_pipe_rd, setup_vfs),
  };
  return cmocka_run_group_tests(tests, NULL, NULL);
}
