#include "test_common.h"

#include <alcor2/errno.h>
#include <alcor2/fs/ext2.h>
#include <alcor2/fs/vfs.h>
#include <alcor2/kstdlib.h>
#include <alcor2/types.h>
#include <fs/ext2/internal.h>

#include <string.h>
#include <stdlib.h>

char *kstrncpy(char *d, const char *s, u64 m) {
  if(!m) return d;
  u64 i;
  for(i = 0; i < m-1 && s[i]; i++) d[i] = s[i];
  d[i] = '\0';
  return d;
}
void kzero(void *d, u64 n)           { memset(d, 0, n); }
void *kmalloc(u64 n)                 { return malloc(n); }
void  kfree(void *p)                 { free(p); }
void console_print(const char *s)    { (void)s; }

/* Stub ext2_* functions */
static ext2_file_t g_file;
static i64 g_open_ret  = (i64)&g_file;  /* returned as file handle */
static i64 g_create_ret= (i64)&g_file;
static i64 g_read_ret  = 8;
static i64 g_write_ret = 8;
static i64 g_mkdir_ret = 0;
static i64 g_unlink_ret= 0;
static i64 g_rmdir_ret = 0;
static i64 g_readdir_ret = 1;
static i64 g_truncate_ret= 0;
static i64 g_stat_ret  = 0;
static i64 g_readlink_ret = 5;
static i64 g_mount_ret = 0;

ext2_file_t *ext2_open(ext2_volume_t *v, const char *p)   { (void)v; (void)p; return (ext2_file_t *)g_open_ret; }
ext2_file_t *ext2_create(ext2_volume_t *v, const char *p) { (void)v; (void)p; return (ext2_file_t *)g_create_ret; }
void         ext2_close(ext2_file_t *f)                   { (void)f; }
i64          ext2_read(ext2_file_t *f, void *b, u64 n, u64 o) { (void)f;(void)b;(void)n;(void)o; return g_read_ret; }
i64          ext2_write(ext2_file_t *f, const void *b, u64 n, u64 o) { (void)f;(void)b;(void)n;(void)o; return g_write_ret; }
i64          ext2_mkdir(ext2_volume_t *v, const char *p)  { (void)v;(void)p; return g_mkdir_ret; }
i64          ext2_unlink(ext2_volume_t *v, const char *p) { (void)v;(void)p; return g_unlink_ret; }
i64          ext2_rmdir(ext2_volume_t *v, const char *p)  { (void)v;(void)p; return g_rmdir_ret; }
i64          ext2_truncate(ext2_file_t *f, u64 l)         { (void)f;(void)l; return g_truncate_ret; }
i64          ext2_readlink(const ext2_volume_t *v, const char *p, char *b, u64 c) {
  (void)v;(void)p;(void)c;
  if(b) memcpy(b, "hello", 5);
  return g_readlink_ret;
}

static ext2_entry_t g_readdir_entry;
i64 ext2_readdir(ext2_file_t *f, u64 idx, ext2_entry_t *e) {
  (void)f;(void)idx;
  if(e) *e = g_readdir_entry;
  return g_readdir_ret;
}

static ext2_entry_t g_stat_entry;
i64 ext2_stat(const ext2_volume_t *v, const char *p, ext2_entry_t *e) {
  (void)v;(void)p;
  if(e) *e = g_stat_entry;
  return g_stat_ret;
}

i64 vfs_register_fs(const fs_type_t *ft) { (void)ft; return 0; }

static ext2_volume_t g_vol;
ext2_volume_t *ext2_mount(const blockdev_t *dev, u32 flags) {
  (void)dev;(void)flags;
  return g_mount_ret ? &g_vol : NULL;
}
const blockdev_t *g_default_dev = NULL;

#include "../../src/fs/ext2/ops.c"

static int setup(void **state)
{
  (void)state;
  memset(&g_file, 0, sizeof(g_file));
  g_file.in_use    = true;
  g_file.is_dir    = false;
  g_file.inode_num = 42;
  g_file.inode.i_size = 100;

  memset(&g_readdir_entry, 0, sizeof(g_readdir_entry));
  strcpy(g_readdir_entry.name, "foo");
  g_readdir_entry.file_type = EXT2_FT_REG_FILE;
  g_readdir_entry.size      = 256;
  g_readdir_entry.inode     = 10;

  memset(&g_stat_entry, 0, sizeof(g_stat_entry));
  strcpy(g_stat_entry.name, "bar");
  g_stat_entry.file_type = EXT2_FT_DIR;
  g_stat_entry.size      = 512;
  g_stat_entry.inode     = 5;

  g_open_ret    = (i64)&g_file;
  g_create_ret  = (i64)&g_file;
  g_read_ret    = 8;
  g_write_ret   = 8;
  g_mkdir_ret   = 0;
  g_unlink_ret  = 0;
  g_rmdir_ret   = 0;
  g_readdir_ret = 1;
  g_truncate_ret= 0;
  g_stat_ret    = 0;
  g_readlink_ret= 5;
  g_mount_ret   = 1;
  memset(&g_vol, 0, sizeof(g_vol));
  return 0;
}

/* ext2_ops_open: open without O_CREAT */
static void ops_open_no_creat_calls_ext2_open(void **state) {
  (void)state;
  fs_handle_t fh = ext2_ops_open(&g_vol, "/f", 0);
  assert_ptr_equal(fh, &g_file);
}

/* ext2_ops_open: open with O_CREAT */
static void ops_open_creat_calls_ext2_create(void **state) {
  (void)state;
  fs_handle_t fh = ext2_ops_open(&g_vol, "/f", O_CREAT);
  assert_ptr_equal(fh, &g_file);
}

/* ext2_ops_close: does not crash */
static void ops_close_noop(void **state) {
  (void)state;
  ext2_ops_close(&g_file);
}

/* ext2_ops_read */
static void ops_read_returns_bytes(void **state) {
  (void)state;
  char buf[8];
  assert_int_equal(ext2_ops_read(&g_file, buf, 8, 0), 8);
}

/* ext2_ops_write */
static void ops_write_returns_bytes(void **state) {
  (void)state;
  char buf[8];
  assert_int_equal(ext2_ops_write(&g_file, buf, 8, 0), 8);
}

/* ext2_ops_mkdir */
static void ops_mkdir_success(void **state) {
  (void)state;
  assert_int_equal(ext2_ops_mkdir(&g_vol, "/d"), 0);
}

/* ext2_ops_unlink */
static void ops_unlink_success(void **state) {
  (void)state;
  assert_int_equal(ext2_ops_unlink(&g_vol, "/f"), 0);
}

/* ext2_ops_rmdir */
static void ops_rmdir_success(void **state) {
  (void)state;
  assert_int_equal(ext2_ops_rmdir(&g_vol, "/d"), 0);
}

/* ext2_ops_fstat: success */
static void ops_fstat_fills_stat(void **state) {
  (void)state;
  vfs_stat_t st;
  assert_int_equal(ext2_ops_fstat(&g_file, &st), 0);
  assert_int_equal(st.size, 100);
  assert_int_equal(st.type, VFS_FILE);
  assert_int_equal(st.ino, 42);
}

/* ext2_ops_fstat: null handle returns -EINVAL */
static void ops_fstat_null_handle_einval(void **state) {
  (void)state;
  vfs_stat_t st;
  assert_int_equal((i64)ext2_ops_fstat(NULL, &st), -EINVAL);
}

/* ext2_ops_fstat: null st returns -EINVAL */
static void ops_fstat_null_st_einval(void **state) {
  (void)state;
  assert_int_equal((i64)ext2_ops_fstat(&g_file, NULL), -EINVAL);
}

/* ext2_ops_fstat: directory type */
static void ops_fstat_dir_type(void **state) {
  (void)state;
  g_file.is_dir = true;
  vfs_stat_t st;
  ext2_ops_fstat(&g_file, &st);
  assert_int_equal(st.type, VFS_DIRECTORY);
}

/* ext2_ops_stat */
static void ops_stat_success(void **state) {
  (void)state;
  vfs_stat_t st;
  assert_int_equal(ext2_ops_stat(&g_vol, "/bar", &st), 0);
  assert_int_equal(st.type, VFS_DIRECTORY); /* EXT2_FT_DIR */
  assert_int_equal(st.size, 512);
}

static void ops_stat_file_type(void **state) {
  (void)state;
  g_stat_entry.file_type = EXT2_FT_REG_FILE;
  vfs_stat_t st;
  ext2_ops_stat(&g_vol, "/f", &st);
  assert_int_equal(st.type, VFS_FILE);
}

static void ops_stat_enoent_propagated(void **state) {
  (void)state;
  g_stat_ret = -ENOENT;
  vfs_stat_t st;
  assert_int_equal((i64)ext2_ops_stat(&g_vol, "/nope", &st), -ENOENT);
}

/* ext2_ops_readdir */
static void ops_readdir_returns_entry(void **state) {
  (void)state;
  char name[VFS_NAME_MAX];
  vfs_stat_t st;
  assert_int_equal(ext2_ops_readdir(&g_file, 0, name, &st), 1);
  assert_string_equal(name, "foo");
  assert_int_equal(st.size, 256);
  assert_int_equal(st.ino, 10);
}

static void ops_readdir_null_st_skips_stat(void **state) {
  (void)state;
  char name[VFS_NAME_MAX];
  assert_int_equal(ext2_ops_readdir(&g_file, 0, name, NULL), 1);
  assert_string_equal(name, "foo");
}

static void ops_readdir_end_returns_zero(void **state) {
  (void)state;
  g_readdir_ret = 0;
  char name[VFS_NAME_MAX];
  assert_int_equal(ext2_ops_readdir(&g_file, 99, name, NULL), 0);
}

/* ext2_ops_truncate */
static void ops_truncate_delegates(void **state) {
  (void)state;
  assert_int_equal(ext2_ops_truncate(&g_file, 0), 0);
}

/* ext2_ops_readlink */
static void ops_readlink_returns_bytes(void **state) {
  (void)state;
  char buf[32];
  assert_int_equal(ext2_ops_readlink(&g_vol, "/lnk", buf, sizeof(buf)), 5);
  assert_memory_equal(buf, "hello", 5);
}

static void ops_mount_calls_ext2_mount(void **state) {
    (void)state;
    g_mount_ret = 1;
    void *result = g_ext2_fstype.mount("dev", 0);
    assert_non_null(result);
}

static void ops_mount_returns_null_on_fail(void **state) {
    (void)state;
    g_mount_ret = 0;
    void *result = g_ext2_fstype.mount("dev", 0);
    assert_null(result);
}

int main(void)
{
  const struct CMUnitTest tests[] = {
      cmocka_unit_test_setup(ops_open_no_creat_calls_ext2_open, setup),
      cmocka_unit_test_setup(ops_open_creat_calls_ext2_create, setup),
      cmocka_unit_test_setup(ops_close_noop, setup),
      cmocka_unit_test_setup(ops_read_returns_bytes, setup),
      cmocka_unit_test_setup(ops_write_returns_bytes, setup),
      cmocka_unit_test_setup(ops_mkdir_success, setup),
      cmocka_unit_test_setup(ops_unlink_success, setup),
      cmocka_unit_test_setup(ops_rmdir_success, setup),
      cmocka_unit_test_setup(ops_fstat_fills_stat, setup),
      cmocka_unit_test_setup(ops_fstat_null_handle_einval, setup),
      cmocka_unit_test_setup(ops_fstat_null_st_einval, setup),
      cmocka_unit_test_setup(ops_fstat_dir_type, setup),
      cmocka_unit_test_setup(ops_stat_success, setup),
      cmocka_unit_test_setup(ops_stat_file_type, setup),
      cmocka_unit_test_setup(ops_stat_enoent_propagated, setup),
      cmocka_unit_test_setup(ops_readdir_returns_entry, setup),
      cmocka_unit_test_setup(ops_readdir_null_st_skips_stat, setup),
      cmocka_unit_test_setup(ops_readdir_end_returns_zero, setup),
      cmocka_unit_test_setup(ops_truncate_delegates, setup),
      cmocka_unit_test_setup(ops_readlink_returns_bytes, setup),
      cmocka_unit_test_setup(ops_mount_calls_ext2_mount, setup),
      cmocka_unit_test_setup(ops_mount_returns_null_on_fail, setup),
  };
  return cmocka_run_group_tests(tests, NULL, NULL);
}
