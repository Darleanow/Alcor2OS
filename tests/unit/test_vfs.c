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

static void dummy_close(fs_handle_t fh) { (void)fh; }

static const fs_ops_t dummy_ops = {
  .open = dummy_open,
  .stat = dummy_stat,
  .close = dummy_close
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

int main(void) {
  const struct CMUnitTest tests[] = {
      cmocka_unit_test_setup(vfs_normalize_removes_dots, setup_vfs),
      cmocka_unit_test_setup(vfs_normalize_collapses_slashes, setup_vfs),
      cmocka_unit_test_setup(vfs_normalize_handles_root_dotdots, setup_vfs),
      cmocka_unit_test_setup(vfs_mount_root_open_rel_path_has_slash, setup_vfs),
      cmocka_unit_test_setup(vfs_mount_subdir_open_rel_path_has_slash, setup_vfs),
  };
  return cmocka_run_group_tests(tests, NULL, NULL);
}
