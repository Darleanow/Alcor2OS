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
char *kstrncpy(char *d, const char *s, u64 m) { if(!m) return d; u64 i; for(i=0;i<m-1&&s[i];i++) d[i]=s[i]; d[i]='\0'; return d; }
void *kzalloc(u64 n) { return calloc(1, (size_t)n); }
void *krealloc(void *ptr, u64 size) { return realloc(ptr, (size_t)size); }

char *kstrrchr(const char *s, int c) {
  return strrchr(s, c);
}

/* Prevent linking real VFS */
#include <alcor2/fs/vfs.h>
i64 vfs_register_fs(const fs_type_t *fstype) {
  (void)fstype;
  return 0;
}

#include "../../src/fs/ramfs.c"

static int reset_ramfs(void **state) {
  (void)state;
  /* Simple brute-force reset for testing */
  if (root) {
    /* To fully clean up we'd need a recursive free, but for unit tests
       in a short-lived process, just nulling root and calling init is 
       often enough, or we can just leak if cmocka doesn't strictly check.
       Actually, let's just do a proper teardown if possible, or 
       just reset root. */
    root = NULL;
  }
  ramfs_init();
  return 0;
}

static void ram_mkdir_creates_directory(void **state) {
  (void)state;
  assert_int_equal(ram_mkdir(NULL, "/testdir"), 0);
  ram_node_t *node = ram__resolve("/testdir");
  assert_non_null(node);
  assert_int_equal(node->type, VFS_DIRECTORY);
}

static void ram_mkdir_nested_fails_if_parent_missing(void **state) {
  (void)state;
  assert_int_equal(ram_mkdir(NULL, "/missing/dir"), -ENOENT);
}

static void ram_mkdir_nested_fails_if_parent_notdir(void **state) {
  (void)state;
  ram_open(NULL, "/file", O_CREAT | O_WRONLY);
  assert_int_equal(ram_mkdir(NULL, "/file/dir"), -ENOTDIR);
}

static void ram_open_creat_creates_file(void **state) {
  (void)state;
  fs_handle_t fh = ram_open(NULL, "/testfile", O_CREAT | O_WRONLY);
  assert_non_null(fh);
  ram_node_t *node = ram__resolve("/testfile");
  assert_non_null(node);
  assert_int_equal(node->type, VFS_FILE);
}

static void ram_write_and_read_file(void **state) {
  (void)state;
  fs_handle_t fh = ram_open(NULL, "/file_rw", O_CREAT | O_WRONLY);
  assert_non_null(fh);
  const char *data = "hello ramfs";
  assert_int_equal(ram_write(fh, data, 11, 0), 11);
  
  char buf[16] = {0};
  assert_int_equal(ram_read(fh, buf, 11, 0), 11);
  assert_string_equal(buf, "hello ramfs");
}

static void ram_write_past_end_expands_capacity(void **state) {
  (void)state;
  fs_handle_t fh = ram_open(NULL, "/file_expand", O_CREAT | O_WRONLY);
  assert_int_equal(ram_write(fh, "x", 1, 2000), 1);
  ram_node_t *node = (ram_node_t *)fh;
  assert_int_equal(node->size, 2001);
  assert_true(node->capacity >= 2001);
}

static void ram_write_overflow_fails(void **state) {
  (void)state;
  fs_handle_t fh = ram_open(NULL, "/file_overflow", O_CREAT | O_WRONLY);
  /* Test u64 wrap-around check */
  assert_int_equal(ram_write(fh, "x", 10, (u64)-5), -EFBIG);
}

static void ram_unlink_removes_file(void **state) {
  (void)state;
  ram_open(NULL, "/todelete", O_CREAT | O_WRONLY);
  assert_non_null(ram__resolve("/todelete"));
  assert_int_equal(ram_unlink(NULL, "/todelete"), 0);
  assert_null(ram__resolve("/todelete"));
}

static void ram_rmdir_removes_empty_dir(void **state) {
  (void)state;
  ram_mkdir(NULL, "/emptydir");
  assert_int_equal(ram_rmdir(NULL, "/emptydir"), 0);
  assert_null(ram__resolve("/emptydir"));
}

static void ram_rmdir_fails_on_nonempty(void **state) {
  (void)state;
  ram_mkdir(NULL, "/fulldir");
  ram_open(NULL, "/fulldir/file", O_CREAT | O_WRONLY);
  assert_int_equal(ram_rmdir(NULL, "/fulldir"), -ENOTEMPTY);
}

static void ram_truncate_shrinks_size(void **state) {
  (void)state;
  fs_handle_t fh = ram_open(NULL, "/file_trunc", O_CREAT | O_WRONLY);
  ram_write(fh, "1234567890", 10, 0);
  assert_int_equal(ram_truncate(fh, 5), 0);
  ram_node_t *node = (ram_node_t *)fh;
  assert_int_equal(node->size, 5);
}

static void ramfs_chardev_register_works(void **state) {
  (void)state;
  ramfs_chardev_ops_t cops = {0};
  assert_int_equal(ramfs_chardev_register("/mydev", &cops, NULL), 0);
  ram_node_t *node = ram__resolve("/mydev");
  assert_non_null(node);
  assert_ptr_equal(node->cops, &cops);
}

static void ramfs_chardev_register_missing_parent(void **state) {
  (void)state;
  ramfs_chardev_ops_t cops = {0};
  assert_int_equal(ramfs_chardev_register("/missing/mydev", &cops, NULL), -ENOENT);
}

static void ramfs_chardev_register_notdir_parent(void **state) {
  (void)state;
  ram_open(NULL, "/file", O_CREAT | O_WRONLY);
  ramfs_chardev_ops_t cops = {0};
  assert_int_equal(ramfs_chardev_register("/file/mydev", &cops, NULL), -ENOTDIR);
}

static void ram_resolve_rejects_long_name(void **state) {
  (void)state;
  /* VFS_NAME_MAX is usually 256. Let's create a path with > 256 chars */
  char path[300];
  path[0] = '/';
  memset(path + 1, 'a', 260);
  path[261] = '\0';
  assert_null(ram__resolve(path));
}

int main(void) {
  const struct CMUnitTest tests[] = {
      cmocka_unit_test_setup(ram_mkdir_creates_directory, reset_ramfs),
      cmocka_unit_test_setup(ram_mkdir_nested_fails_if_parent_missing, reset_ramfs),
      cmocka_unit_test_setup(ram_mkdir_nested_fails_if_parent_notdir, reset_ramfs),
      cmocka_unit_test_setup(ram_open_creat_creates_file, reset_ramfs),
      cmocka_unit_test_setup(ram_resolve_rejects_long_name, reset_ramfs),
      cmocka_unit_test_setup(ram_rmdir_fails_on_nonempty, reset_ramfs),
      cmocka_unit_test_setup(ram_rmdir_removes_empty_dir, reset_ramfs),
      cmocka_unit_test_setup(ram_truncate_shrinks_size, reset_ramfs),
      cmocka_unit_test_setup(ram_unlink_removes_file, reset_ramfs),
      cmocka_unit_test_setup(ram_write_and_read_file, reset_ramfs),
      cmocka_unit_test_setup(ram_write_overflow_fails, reset_ramfs),
      cmocka_unit_test_setup(ram_write_past_end_expands_capacity, reset_ramfs),
      cmocka_unit_test_setup(ramfs_chardev_register_missing_parent, reset_ramfs),
      cmocka_unit_test_setup(ramfs_chardev_register_notdir_parent, reset_ramfs),
      cmocka_unit_test_setup(ramfs_chardev_register_works, reset_ramfs),
  };
  return cmocka_run_group_tests(tests, NULL, NULL);
}
