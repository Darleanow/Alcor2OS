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

#include <alcor2/fs/vfs.h>

i64 vfs_register_fs(const fs_type_t *fstype) {
  (void)fstype;
  return 0;
}
#include "../../src/fs/initfs.c"

static int reset_files(void **state) {
  (void)state;
  init_file_t *f = files;
  while(f) {
    init_file_t *next = f->next;
    free(f);
    f = next;
  }
  files = NULL;
  return 0;
}

static void init_fstat_directory_has_vfs_directory_type(void **state) {
  (void)state;
  vfs_stat_t st;
  assert_int_equal(init_fstat(INITFS_ROOT_HANDLE, &st), 0);
  assert_int_equal(st.type, VFS_DIRECTORY);
}

static void init_fstat_file_has_correct_size(void **state) {
  (void)state;
  const char *data = "123456789012345678901234567890123456789012";
  assert_int_equal(initfs_register("testfile", data, 42), 0);
  fs_handle_t fh = init_open(NULL, "/testfile", O_RDONLY);
  assert_non_null(fh);
  vfs_stat_t st;
  assert_int_equal(init_fstat(fh, &st), 0);
  assert_int_equal(st.size, 42);
}

static void init_fstat_file_has_vfs_file_type(void **state) {
  (void)state;
  assert_int_equal(initfs_register("testfile", "data", 4), 0);
  fs_handle_t fh = init_open(NULL, "/testfile", O_RDONLY);
  assert_non_null(fh);
  vfs_stat_t st;
  assert_int_equal(init_fstat(fh, &st), 0);
  assert_int_equal(st.type, VFS_FILE);
}

static void init_open_creat_flag_returns_null(void **state) {
  (void)state;
  assert_int_equal(initfs_register("testfile", "data", 4), 0);
  assert_null(init_open(NULL, "/testfile", O_RDONLY | O_CREAT));
}

static void init_open_missing_file_returns_null(void **state) {
  (void)state;
  assert_null(init_open(NULL, "/missing", O_RDONLY));
}

static void init_open_nested_path_returns_null(void **state) {
  (void)state;
  assert_null(init_open(NULL, "/foo/bar", O_RDONLY));
}

static void init_open_rdonly_registered_file_succeeds(void **state) {
  (void)state;
  assert_int_equal(initfs_register("testfile", "data", 4), 0);
  assert_non_null(init_open(NULL, "/testfile", O_RDONLY));
}

static void init_open_root_returns_root_handle(void **state) {
  (void)state;
  assert_ptr_equal(init_open(NULL, "/", O_RDONLY), INITFS_ROOT_HANDLE);
}

static void init_open_wronly_returns_null(void **state) {
  (void)state;
  assert_int_equal(initfs_register("testfile", "data", 4), 0);
  assert_null(init_open(NULL, "/testfile", O_WRONLY));
}

static void init_read_at_eof_returns_zero(void **state) {
  (void)state;
  assert_int_equal(initfs_register("testfile", "data", 4), 0);
  fs_handle_t fh = init_open(NULL, "/testfile", O_RDONLY);
  char buf[10];
  assert_int_equal(init_read(fh, buf, 10, 4), 0);
}

static void init_read_clamps_count(void **state) {
  (void)state;
  assert_int_equal(initfs_register("testfile", "0123456789", 10), 0);
  fs_handle_t fh = init_open(NULL, "/testfile", O_RDONLY);
  char buf[999] = {0};
  assert_int_equal(init_read(fh, buf, 999, 7), 3);
  assert_memory_equal(buf, "789", 3);
}

static void init_read_content_correct(void **state) {
  (void)state;
  assert_int_equal(initfs_register("testfile", "hello world", 11), 0);
  fs_handle_t fh = init_open(NULL, "/testfile", O_RDONLY);
  char buf[5] = {0};
  assert_int_equal(init_read(fh, buf, 5, 0), 5);
  assert_memory_equal(buf, "hello", 5);
}

static void init_read_past_eof_returns_zero(void **state) {
  (void)state;
  assert_int_equal(initfs_register("testfile", "data", 4), 0);
  fs_handle_t fh = init_open(NULL, "/testfile", O_RDONLY);
  char buf[10];
  assert_int_equal(init_read(fh, buf, 10, 100), 0);
}

static void init_read_root_returns_eisdir(void **state) {
  (void)state;
  char buf[10];
  assert_int_equal(init_read(INITFS_ROOT_HANDLE, buf, 10, 0), -EISDIR);
}

static void init_readdir_nonroot_fh_returns_enotdir(void **state) {
  (void)state;
  assert_int_equal(initfs_register("testfile", "data", 4), 0);
  fs_handle_t fh = init_open(NULL, "/testfile", O_RDONLY);
  char name[VFS_NAME_MAX + 1];
  assert_int_equal(init_readdir(fh, 0, name, NULL), -ENOTDIR);
}

static void init_readdir_past_end_returns_zero(void **state) {
  (void)state;
  assert_int_equal(initfs_register("testfile", "data", 4), 0);
  char name[VFS_NAME_MAX + 1];
  assert_int_equal(init_readdir(INITFS_ROOT_HANDLE, 1, name, NULL), 0);
}

static void init_readdir_returns_registered_filename(void **state) {
  (void)state;
  assert_int_equal(initfs_register("cat", "meow", 4), 0);
  char name[VFS_NAME_MAX + 1];
  assert_int_equal(init_readdir(INITFS_ROOT_HANDLE, 0, name, NULL), 1);
  assert_string_equal(name, "cat");
}

static void init_readdir_sequential_three_files(void **state) {
  (void)state;
  assert_int_equal(initfs_register("f1", "1", 1), 0);
  assert_int_equal(initfs_register("f2", "2", 1), 0);
  assert_int_equal(initfs_register("f3", "3", 1), 0);
  char name[VFS_NAME_MAX + 1];
  assert_int_equal(init_readdir(INITFS_ROOT_HANDLE, 0, name, NULL), 1);
  assert_int_equal(init_readdir(INITFS_ROOT_HANDLE, 1, name, NULL), 1);
  assert_int_equal(init_readdir(INITFS_ROOT_HANDLE, 2, name, NULL), 1);
  assert_int_equal(init_readdir(INITFS_ROOT_HANDLE, 3, name, NULL), 0);
}

static void init_stat_missing_returns_enoent(void **state) {
  (void)state;
  vfs_stat_t st;
  assert_int_equal(init_stat(NULL, "/missing", &st), -ENOENT);
}

static void init_stat_registered_file_is_vfs_file(void **state) {
  (void)state;
  assert_int_equal(initfs_register("testfile", "data", 4), 0);
  vfs_stat_t st;
  assert_int_equal(init_stat(NULL, "/testfile", &st), 0);
  assert_int_equal(st.type, VFS_FILE);
  assert_int_equal(st.size, 4);
}

static void init_stat_root_is_vfs_directory(void **state) {
  (void)state;
  vfs_stat_t st;
  assert_int_equal(init_stat(NULL, "/", &st), 0);
  assert_int_equal(st.type, VFS_DIRECTORY);
}

static void init_write_returns_erofs(void **state) {
  (void)state;
  assert_int_equal(initfs_register("testfile", "data", 4), 0);
  fs_handle_t fh = init_open(NULL, "/testfile", O_RDONLY);
  assert_int_equal(init_write(fh, "test", 4, 0), -EROFS);
}

static void initfs_register_duplicate_is_eexist(void **state) {
  (void)state;
  assert_int_equal(initfs_register("testfile", "data", 4), 0);
  assert_int_equal(initfs_register("testfile", "data", 4), -EEXIST);
}

static void initfs_register_empty_name_is_einval(void **state) {
  (void)state;
  assert_int_equal(initfs_register("", "data", 4), -EINVAL);
}

static void initfs_register_name_with_slash_is_einval(void **state) {
  (void)state;
  assert_int_equal(initfs_register("foo/bar", "data", 4), -EINVAL);
}

static void initfs_register_null_data_is_einval(void **state) {
  (void)state;
  assert_int_equal(initfs_register("testfile", NULL, 4), -EINVAL);
}

static void initfs_register_null_name_is_einval(void **state) {
  (void)state;
  assert_int_equal(initfs_register(NULL, "data", 4), -EINVAL);
}

static void strip_slash_no_slash_unchanged(void **state) {
  (void)state;
  const char *s = "foo";
  assert_ptr_equal(strip_slash(s), s);
}

static void strip_slash_null_returns_null(void **state) {
  (void)state;
  assert_null(strip_slash(NULL));
}

static void strip_slash_removes_leading_slash(void **state) {
  (void)state;
  const char *s = "/foo";
  assert_string_equal(strip_slash(s), "foo");
}

static void strip_slash_root_gives_empty(void **state) {
  (void)state;
  const char *s = "/";
  assert_string_equal(strip_slash(s), "");
}

int main(void) {
  const struct CMUnitTest tests[] = {
      cmocka_unit_test_setup(init_fstat_directory_has_vfs_directory_type, reset_files),
      cmocka_unit_test_setup(init_fstat_file_has_correct_size, reset_files),
      cmocka_unit_test_setup(init_fstat_file_has_vfs_file_type, reset_files),
      cmocka_unit_test_setup(init_open_creat_flag_returns_null, reset_files),
      cmocka_unit_test_setup(init_open_missing_file_returns_null, reset_files),
      cmocka_unit_test_setup(init_open_nested_path_returns_null, reset_files),
      cmocka_unit_test_setup(init_open_rdonly_registered_file_succeeds, reset_files),
      cmocka_unit_test_setup(init_open_root_returns_root_handle, reset_files),
      cmocka_unit_test_setup(init_open_wronly_returns_null, reset_files),
      cmocka_unit_test_setup(init_read_at_eof_returns_zero, reset_files),
      cmocka_unit_test_setup(init_read_clamps_count, reset_files),
      cmocka_unit_test_setup(init_read_content_correct, reset_files),
      cmocka_unit_test_setup(init_read_past_eof_returns_zero, reset_files),
      cmocka_unit_test_setup(init_read_root_returns_eisdir, reset_files),
      cmocka_unit_test_setup(init_readdir_nonroot_fh_returns_enotdir, reset_files),
      cmocka_unit_test_setup(init_readdir_past_end_returns_zero, reset_files),
      cmocka_unit_test_setup(init_readdir_returns_registered_filename, reset_files),
      cmocka_unit_test_setup(init_readdir_sequential_three_files, reset_files),
      cmocka_unit_test_setup(init_stat_missing_returns_enoent, reset_files),
      cmocka_unit_test_setup(init_stat_registered_file_is_vfs_file, reset_files),
      cmocka_unit_test_setup(init_stat_root_is_vfs_directory, reset_files),
      cmocka_unit_test_setup(init_write_returns_erofs, reset_files),
      cmocka_unit_test_setup(initfs_register_duplicate_is_eexist, reset_files),
      cmocka_unit_test_setup(initfs_register_empty_name_is_einval, reset_files),
      cmocka_unit_test_setup(initfs_register_name_with_slash_is_einval, reset_files),
      cmocka_unit_test_setup(initfs_register_null_data_is_einval, reset_files),
      cmocka_unit_test_setup(initfs_register_null_name_is_einval, reset_files),
      cmocka_unit_test_setup(strip_slash_no_slash_unchanged, reset_files),
      cmocka_unit_test_setup(strip_slash_null_returns_null, reset_files),
      cmocka_unit_test_setup(strip_slash_removes_leading_slash, reset_files),
      cmocka_unit_test_setup(strip_slash_root_gives_empty, reset_files),
  };
  return cmocka_run_group_tests(tests, NULL, NULL);
}
