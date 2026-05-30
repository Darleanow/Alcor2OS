#include "test_common.h"
#include <alcor2/errno.h>
#include <alcor2/fs/ext2.h>
#include <alcor2/fs/vfs.h>
#include <alcor2/kstdlib.h>
#include <alcor2/mm/heap.h>
#include <alcor2/types.h>
#include <fs/ext2/internal.h>

#include <string.h>
#include <stdlib.h>

/* Mock open file table */
ext2_file_t g_files[EXT2_MAX_FILES];

/* Mocks */

u32 alloc_block(ext2_volume_t *vol, u32 preferred_group) {
  check_expected_ptr(vol);
  check_expected(preferred_group);
  return mock_type(u32);
}

u32 alloc_inode(ext2_volume_t *vol, u32 preferred_group, bool is_dir) {
  check_expected_ptr(vol);
  check_expected(preferred_group);
  check_expected(is_dir);
  return mock_type(u32);
}

i64 dir_add_entry(
    ext2_volume_t *vol, u32 dir_ino, ext2_inode_t *dir_inode, const char *name,
    u32 inode_num, u8 file_type
) {
  check_expected_ptr(vol);
  check_expected(dir_ino);
  check_expected_ptr(dir_inode);
  check_expected(name);
  check_expected(inode_num);
  check_expected(file_type);
  return mock_type(i64);
}

bool dir_is_empty(const ext2_volume_t *vol, const ext2_inode_t *dir_inode) {
  check_expected_ptr(vol);
  check_expected_ptr(dir_inode);
  return mock_type(bool);
}

i64 dir_remove_entry(const ext2_volume_t *vol, const ext2_inode_t *dir_inode, const char *name) {
  check_expected_ptr(vol);
  check_expected_ptr(dir_inode);
  check_expected(name);
  return mock_type(i64);
}

i64 flush_metadata(ext2_volume_t *vol) {
  check_expected_ptr(vol);
  return mock_type(i64);
}

i64 free_block(ext2_volume_t *vol, u32 block) {
  check_expected_ptr(vol);
  check_expected(block);
  return mock_type(i64);
}

i64 free_inode(ext2_volume_t *vol, u32 ino, bool is_dir) {
  check_expected_ptr(vol);
  check_expected(ino);
  check_expected(is_dir);
  return mock_type(i64);
}

i64 free_inode_blocks(ext2_volume_t *vol, ext2_inode_t *inode) {
  check_expected_ptr(vol);
  check_expected_ptr(inode);
  return mock_type(i64);
}

void path_split(const char *path, char *parent, char *name) {
  check_expected(path);
  const char *mock_parent = mock_ptr_type(const char *);
  const char *mock_name = mock_ptr_type(const char *);
  kstrncpy(parent, mock_parent, VFS_PATH_MAX);
  kstrncpy(name, mock_name, EXT2_NAME_MAX + 1);
}

i64 resolve_path(const ext2_volume_t *vol, const char *path, u32 *out_ino, ext2_inode_t *out_inode) {
  check_expected_ptr(vol);
  check_expected(path);
  i64 ret = mock_type(i64);
  if (ret == 0) {
    *out_ino = mock_type(u32);
    ext2_inode_t *inode = mock_ptr_type(ext2_inode_t *);
    if (out_inode && inode) *out_inode = *inode;
  }
  return ret;
}

i64 vol_write_block(const ext2_volume_t *vol, u32 block, const void *buf) {
  check_expected_ptr(vol);
  check_expected(block);
  check_expected_ptr(buf);
  return mock_type(i64);
}

i64 write_inode(const ext2_volume_t *vol, u32 ino, const ext2_inode_t *inode) {
  check_expected_ptr(vol);
  check_expected(ino);
  check_expected_ptr(inode);
  return mock_type(i64);
}

void *kmalloc(size_t size) { return malloc(size); }
void kfree(void *ptr) { free(ptr); }

char *kstrncpy(char *dst, const char *src, u64 max) {
  strncpy(dst, src, max);
  if (max > 0) dst[max - 1] = '\0';
  return dst;
}

int kstrcmp(const char *a, const char *b) {
  return strcmp(a, b);
}

void kzero(void *dst, u64 n) {
  memset(dst, 0, n);
}

void *kmemcpy(void *dst, const void *src, u64 n) {
  return memcpy(dst, src, n);
}

u8 *cache_get_block(u32 sec) {
  (void)sec;
  return mock_type(u8 *);
}

void cache_put_block(u8 *ptr) {
  (void)ptr;
}

u32 get_block_num(const ext2_volume_t *vol, const ext2_inode_t *inode, u32 file_block) {
  check_expected_ptr(vol);
  check_expected_ptr(inode);
  check_expected(file_block);
  return mock_type(u32);
}

i64 read_inode(const ext2_volume_t *vol, u32 ino, ext2_inode_t *inode) {
  (void)inode;
  check_expected_ptr(vol);
  check_expected(ino);
  return mock_type(i64);
}

i64 vol_read_block(const ext2_volume_t *vol, u32 block, void *buf) {
  check_expected_ptr(vol);
  check_expected(block);
  check_expected_ptr(buf);
  return mock_type(i64);
}


#include "../../src/fs/ext2/dir.c"

/* Helper for initializing a volume */
static ext2_volume_t create_mock_vol(void) {
  ext2_volume_t vol = {0};
  vol.mounted = true;
  vol.block_size = 1024;
  vol.inodes_per_group = 8192;
  return vol;
}

/* TESTS */

static void ext2_mkdir_fails_alloc_block(void **state) {
  (void)state;
  ext2_volume_t vol = create_mock_vol();
  ext2_inode_t parent_inode = { .i_mode = EXT2_S_IFDIR, .i_links_count = 2 };

  expect_value(resolve_path, vol, &vol);
  expect_string(resolve_path, path, "/newdir");
  will_return(resolve_path, -ENOENT);

  expect_string(path_split, path, "/newdir");
  will_return(path_split, "/");
  will_return(path_split, "newdir");

  expect_value(resolve_path, vol, &vol);
  expect_string(resolve_path, path, "/");
  will_return(resolve_path, 0);
  will_return(resolve_path, 2);
  will_return(resolve_path, &parent_inode);

  expect_value(alloc_inode, vol, &vol);
  expect_value(alloc_inode, preferred_group, 0);
  expect_value(alloc_inode, is_dir, true);
  will_return(alloc_inode, 3);

  expect_value(alloc_block, vol, &vol);
  expect_value(alloc_block, preferred_group, 0);
  will_return(alloc_block, 0);

  expect_value(free_inode, vol, &vol);
  expect_value(free_inode, ino, 3);
  expect_value(free_inode, is_dir, true);
  will_return(free_inode, 0);

  i64 ret = ext2_mkdir(&vol, "/newdir");
  assert_int_equal(ret, -ENOSPC);
}

static void ext2_mkdir_fails_alloc_inode(void **state) {
  (void)state;
  ext2_volume_t vol = create_mock_vol();
  ext2_inode_t parent_inode = { .i_mode = EXT2_S_IFDIR, .i_links_count = 2 };

  expect_value(resolve_path, vol, &vol);
  expect_string(resolve_path, path, "/newdir");
  will_return(resolve_path, -ENOENT);

  expect_string(path_split, path, "/newdir");
  will_return(path_split, "/");
  will_return(path_split, "newdir");

  expect_value(resolve_path, vol, &vol);
  expect_string(resolve_path, path, "/");
  will_return(resolve_path, 0);
  will_return(resolve_path, 2);
  will_return(resolve_path, &parent_inode);

  expect_value(alloc_inode, vol, &vol);
  expect_value(alloc_inode, preferred_group, 0);
  expect_value(alloc_inode, is_dir, true);
  will_return(alloc_inode, 0);

  i64 ret = ext2_mkdir(&vol, "/newdir");
  assert_int_equal(ret, -ENOSPC);
}

static void ext2_mkdir_fails_dir_add_entry(void **state) {
  (void)state;
  ext2_volume_t vol = create_mock_vol();
  ext2_inode_t parent_inode = { .i_mode = EXT2_S_IFDIR, .i_links_count = 2 };

  expect_value(resolve_path, vol, &vol);
  expect_string(resolve_path, path, "/newdir");
  will_return(resolve_path, -ENOENT);

  expect_string(path_split, path, "/newdir");
  will_return(path_split, "/");
  will_return(path_split, "newdir");

  expect_value(resolve_path, vol, &vol);
  expect_string(resolve_path, path, "/");
  will_return(resolve_path, 0);
  will_return(resolve_path, 2);
  will_return(resolve_path, &parent_inode);

  expect_value(alloc_inode, vol, &vol);
  expect_value(alloc_inode, preferred_group, 0);
  expect_value(alloc_inode, is_dir, true);
  will_return(alloc_inode, 3);

  expect_value(alloc_block, vol, &vol);
  expect_value(alloc_block, preferred_group, 0);
  will_return(alloc_block, 42);

  expect_value(vol_write_block, vol, &vol);
  expect_value(vol_write_block, block, 42);
  expect_any(vol_write_block, buf);
  will_return(vol_write_block, 0);

  expect_value(write_inode, vol, &vol);
  expect_value(write_inode, ino, 3);
  expect_any(write_inode, inode);
  will_return(write_inode, 0);

  expect_value(dir_add_entry, vol, &vol);
  expect_value(dir_add_entry, dir_ino, 2);
  expect_any(dir_add_entry, dir_inode);
  expect_string(dir_add_entry, name, "newdir");
  expect_value(dir_add_entry, inode_num, 3);
  expect_value(dir_add_entry, file_type, EXT2_FT_DIR);
  will_return(dir_add_entry, -ENOSPC);

  expect_value(free_block, vol, &vol);
  expect_value(free_block, block, 42);
  will_return(free_block, 0);

  expect_value(free_inode, vol, &vol);
  expect_value(free_inode, ino, 3);
  expect_value(free_inode, is_dir, true);
  will_return(free_inode, 0);

  i64 ret = ext2_mkdir(&vol, "/newdir");
  assert_int_equal(ret, -EIO);
}

static void ext2_mkdir_fails_parent_enoent(void **state) {
  (void)state;
  ext2_volume_t vol = create_mock_vol();

  expect_value(resolve_path, vol, &vol);
  expect_string(resolve_path, path, "/missing/newdir");
  will_return(resolve_path, -ENOENT);

  expect_string(path_split, path, "/missing/newdir");
  will_return(path_split, "/missing");
  will_return(path_split, "newdir");

  expect_value(resolve_path, vol, &vol);
  expect_string(resolve_path, path, "/missing");
  will_return(resolve_path, -ENOENT);

  i64 ret = ext2_mkdir(&vol, "/missing/newdir");
  assert_int_equal(ret, -ENOENT);
}

static void ext2_mkdir_fails_parent_notdir(void **state) {
  (void)state;
  ext2_volume_t vol = create_mock_vol();
  ext2_inode_t parent_inode = { .i_mode = EXT2_S_IFREG, .i_links_count = 1 };

  expect_value(resolve_path, vol, &vol);
  expect_string(resolve_path, path, "/file/newdir");
  will_return(resolve_path, -ENOENT);

  expect_string(path_split, path, "/file/newdir");
  will_return(path_split, "/file");
  will_return(path_split, "newdir");

  expect_value(resolve_path, vol, &vol);
  expect_string(resolve_path, path, "/file");
  will_return(resolve_path, 0);
  will_return(resolve_path, 2);
  will_return(resolve_path, &parent_inode);

  i64 ret = ext2_mkdir(&vol, "/file/newdir");
  assert_int_equal(ret, -ENOTDIR);
}

static void ext2_mkdir_returns_eexist(void **state) {
  (void)state;
  ext2_volume_t vol = create_mock_vol();
  ext2_inode_t existing_inode = { .i_mode = EXT2_S_IFDIR };

  expect_value(resolve_path, vol, &vol);
  expect_string(resolve_path, path, "/existing");
  will_return(resolve_path, 0);
  will_return(resolve_path, 3);
  will_return(resolve_path, &existing_inode);

  i64 ret = ext2_mkdir(&vol, "/existing");
  assert_int_equal(ret, -EEXIST);
}

static void ext2_mkdir_returns_einval_for_empty_name(void **state) {
  (void)state;
  ext2_volume_t vol = create_mock_vol();

  expect_value(resolve_path, vol, &vol);
  expect_string(resolve_path, path, "/");
  will_return(resolve_path, -ENOENT);

  expect_string(path_split, path, "/");
  will_return(path_split, "/");
  will_return(path_split, "");

  i64 ret = ext2_mkdir(&vol, "/");
  assert_int_equal(ret, -EINVAL);
}

static void ext2_mkdir_success(void **state) {
  (void)state;
  ext2_volume_t vol = create_mock_vol();
  ext2_inode_t parent_inode = { .i_mode = EXT2_S_IFDIR, .i_links_count = 2 };

  expect_value(resolve_path, vol, &vol);
  expect_string(resolve_path, path, "/newdir");
  will_return(resolve_path, -ENOENT);

  expect_string(path_split, path, "/newdir");
  will_return(path_split, "/");
  will_return(path_split, "newdir");

  expect_value(resolve_path, vol, &vol);
  expect_string(resolve_path, path, "/");
  will_return(resolve_path, 0);
  will_return(resolve_path, 2);
  will_return(resolve_path, &parent_inode);

  expect_value(alloc_inode, vol, &vol);
  expect_value(alloc_inode, preferred_group, 0);
  expect_value(alloc_inode, is_dir, true);
  will_return(alloc_inode, 3);

  expect_value(alloc_block, vol, &vol);
  expect_value(alloc_block, preferred_group, 0);
  will_return(alloc_block, 42);

  expect_value(vol_write_block, vol, &vol);
  expect_value(vol_write_block, block, 42);
  expect_any(vol_write_block, buf);
  will_return(vol_write_block, 0);

  expect_value(write_inode, vol, &vol);
  expect_value(write_inode, ino, 3);
  expect_any(write_inode, inode);
  will_return(write_inode, 0);

  expect_value(dir_add_entry, vol, &vol);
  expect_value(dir_add_entry, dir_ino, 2);
  expect_any(dir_add_entry, dir_inode);
  expect_string(dir_add_entry, name, "newdir");
  expect_value(dir_add_entry, inode_num, 3);
  expect_value(dir_add_entry, file_type, EXT2_FT_DIR);
  will_return(dir_add_entry, 0);

  expect_value(write_inode, vol, &vol);
  expect_value(write_inode, ino, 2);
  expect_any(write_inode, inode);
  will_return(write_inode, 0);

  expect_value(flush_metadata, vol, &vol);
  will_return(flush_metadata, 0);

  i64 ret = ext2_mkdir(&vol, "/newdir");
  assert_int_equal(ret, 0);
}

static void ext2_rmdir_fails_dir_not_empty(void **state) {
  (void)state;
  ext2_volume_t vol = create_mock_vol();
  ext2_inode_t dir_inode = { .i_mode = EXT2_S_IFDIR };

  expect_value(resolve_path, vol, &vol);
  expect_string(resolve_path, path, "/dir");
  will_return(resolve_path, 0);
  will_return(resolve_path, 3);
  will_return(resolve_path, &dir_inode);

  expect_value(dir_is_empty, vol, &vol);
  expect_any(dir_is_empty, dir_inode);
  will_return(dir_is_empty, false);

  i64 ret = ext2_rmdir(&vol, "/dir");
  assert_int_equal(ret, -ENOTEMPTY);
}

static void ext2_rmdir_returns_einval_for_dot(void **state) {
  (void)state;
  ext2_volume_t vol = create_mock_vol();
  ext2_inode_t dir_inode = { .i_mode = EXT2_S_IFDIR };

  expect_value(resolve_path, vol, &vol);
  expect_string(resolve_path, path, "/.");
  will_return(resolve_path, 0);
  will_return(resolve_path, 3);
  will_return(resolve_path, &dir_inode);

  expect_value(dir_is_empty, vol, &vol);
  expect_any(dir_is_empty, dir_inode);
  will_return(dir_is_empty, true);

  expect_string(path_split, path, "/.");
  will_return(path_split, "/");
  will_return(path_split, ".");

  i64 ret = ext2_rmdir(&vol, "/.");
  assert_int_equal(ret, -EINVAL);
}

static void ext2_rmdir_returns_einval_for_dotdot(void **state) {
  (void)state;
  ext2_volume_t vol = create_mock_vol();
  ext2_inode_t dir_inode = { .i_mode = EXT2_S_IFDIR };

  expect_value(resolve_path, vol, &vol);
  expect_string(resolve_path, path, "/..");
  will_return(resolve_path, 0);
  will_return(resolve_path, 3);
  will_return(resolve_path, &dir_inode);

  expect_value(dir_is_empty, vol, &vol);
  expect_any(dir_is_empty, dir_inode);
  will_return(dir_is_empty, true);

  expect_string(path_split, path, "/..");
  will_return(path_split, "/");
  will_return(path_split, "..");

  i64 ret = ext2_rmdir(&vol, "/..");
  assert_int_equal(ret, -EINVAL);
}

static void ext2_rmdir_returns_einval_for_root(void **state) {
  (void)state;
  ext2_volume_t vol = create_mock_vol();
  i64 ret = ext2_rmdir(&vol, "/");
  assert_int_equal(ret, -EINVAL);
}

static void ext2_rmdir_returns_enotdir(void **state) {
  (void)state;
  ext2_volume_t vol = create_mock_vol();
  ext2_inode_t file_inode = { .i_mode = EXT2_S_IFREG };

  expect_value(resolve_path, vol, &vol);
  expect_string(resolve_path, path, "/file");
  will_return(resolve_path, 0);
  will_return(resolve_path, 3);
  will_return(resolve_path, &file_inode);

  i64 ret = ext2_rmdir(&vol, "/file");
  assert_int_equal(ret, -ENOTDIR);
}

static void ext2_rmdir_success(void **state) {
  (void)state;
  ext2_volume_t vol = create_mock_vol();
  ext2_inode_t dir_inode = { .i_mode = EXT2_S_IFDIR };
  ext2_inode_t parent_inode = { .i_mode = EXT2_S_IFDIR, .i_links_count = 3 };

  expect_value(resolve_path, vol, &vol);
  expect_string(resolve_path, path, "/dir");
  will_return(resolve_path, 0);
  will_return(resolve_path, 3);
  will_return(resolve_path, &dir_inode);

  expect_value(dir_is_empty, vol, &vol);
  expect_any(dir_is_empty, dir_inode);
  will_return(dir_is_empty, true);

  expect_string(path_split, path, "/dir");
  will_return(path_split, "/");
  will_return(path_split, "dir");

  expect_value(resolve_path, vol, &vol);
  expect_string(resolve_path, path, "/");
  will_return(resolve_path, 0);
  will_return(resolve_path, 2);
  will_return(resolve_path, &parent_inode);

  expect_value(dir_remove_entry, vol, &vol);
  expect_any(dir_remove_entry, dir_inode);
  expect_string(dir_remove_entry, name, "dir");
  will_return(dir_remove_entry, 0);

  expect_value(write_inode, vol, &vol);
  expect_value(write_inode, ino, 2);
  expect_any(write_inode, inode);
  will_return(write_inode, 0);

  expect_value(free_inode_blocks, vol, &vol);
  expect_any(free_inode_blocks, inode);
  will_return(free_inode_blocks, 0);

  expect_value(free_inode, vol, &vol);
  expect_value(free_inode, ino, 3);
  expect_value(free_inode, is_dir, true);
  will_return(free_inode, 0);

  expect_value(flush_metadata, vol, &vol);
  will_return(flush_metadata, 0);

  i64 ret = ext2_rmdir(&vol, "/dir");
  assert_int_equal(ret, 0);
}

static void ext2_unlink_defers_free_when_file_open(void **state) {
  (void)state;
  ext2_volume_t vol = create_mock_vol();
  ext2_inode_t file_inode = { .i_mode = EXT2_S_IFREG, .i_links_count = 1 };
  ext2_inode_t parent_inode = { .i_mode = EXT2_S_IFDIR };

  expect_value(resolve_path, vol, &vol);
  expect_string(resolve_path, path, "/file");
  will_return(resolve_path, 0);
  will_return(resolve_path, 4);
  will_return(resolve_path, &file_inode);

  expect_string(path_split, path, "/file");
  will_return(path_split, "/");
  will_return(path_split, "file");

  expect_value(resolve_path, vol, &vol);
  expect_string(resolve_path, path, "/");
  will_return(resolve_path, 0);
  will_return(resolve_path, 2);
  will_return(resolve_path, &parent_inode);

  expect_value(dir_remove_entry, vol, &vol);
  expect_any(dir_remove_entry, dir_inode);
  expect_string(dir_remove_entry, name, "file");
  will_return(dir_remove_entry, 0);

  memset(g_files, 0, sizeof(g_files));
  g_files[0].in_use = true;
  g_files[0].vol = &vol;
  g_files[0].inode_num = 4;

  expect_value(write_inode, vol, &vol);
  expect_value(write_inode, ino, 4);
  expect_any(write_inode, inode);
  will_return(write_inode, 0);

  expect_value(flush_metadata, vol, &vol);
  will_return(flush_metadata, 0);

  i64 ret = ext2_unlink(&vol, "/file");
  assert_int_equal(ret, 0);
}

static void ext2_unlink_fails_eisdir(void **state) {
  (void)state;
  ext2_volume_t vol = create_mock_vol();
  ext2_inode_t dir_inode = { .i_mode = EXT2_S_IFDIR };

  expect_value(resolve_path, vol, &vol);
  expect_string(resolve_path, path, "/dir");
  will_return(resolve_path, 0);
  will_return(resolve_path, 4);
  will_return(resolve_path, &dir_inode);

  i64 ret = ext2_unlink(&vol, "/dir");
  assert_int_equal(ret, -EISDIR);
}

static void ext2_unlink_frees_immediately_when_closed(void **state) {
  (void)state;
  ext2_volume_t vol = create_mock_vol();
  ext2_inode_t file_inode = { .i_mode = EXT2_S_IFREG, .i_links_count = 1 };
  ext2_inode_t parent_inode = { .i_mode = EXT2_S_IFDIR };

  expect_value(resolve_path, vol, &vol);
  expect_string(resolve_path, path, "/file");
  will_return(resolve_path, 0);
  will_return(resolve_path, 4);
  will_return(resolve_path, &file_inode);

  expect_string(path_split, path, "/file");
  will_return(path_split, "/");
  will_return(path_split, "file");

  expect_value(resolve_path, vol, &vol);
  expect_string(resolve_path, path, "/");
  will_return(resolve_path, 0);
  will_return(resolve_path, 2);
  will_return(resolve_path, &parent_inode);

  expect_value(dir_remove_entry, vol, &vol);
  expect_any(dir_remove_entry, dir_inode);
  expect_string(dir_remove_entry, name, "file");
  will_return(dir_remove_entry, 0);

  memset(g_files, 0, sizeof(g_files));

  expect_value(free_inode_blocks, vol, &vol);
  expect_any(free_inode_blocks, inode);
  will_return(free_inode_blocks, 0);

  expect_value(free_inode, vol, &vol);
  expect_value(free_inode, ino, 4);
  expect_value(free_inode, is_dir, false);
  will_return(free_inode, 0);

  expect_value(flush_metadata, vol, &vol);
  will_return(flush_metadata, 0);

  i64 ret = ext2_unlink(&vol, "/file");
  assert_int_equal(ret, 0);
}

int main(void) {
  const struct CMUnitTest tests[] = {
      cmocka_unit_test(ext2_mkdir_fails_alloc_block),
      cmocka_unit_test(ext2_mkdir_fails_alloc_inode),
      cmocka_unit_test(ext2_mkdir_fails_dir_add_entry),
      cmocka_unit_test(ext2_mkdir_fails_parent_enoent),
      cmocka_unit_test(ext2_mkdir_fails_parent_notdir),
      cmocka_unit_test(ext2_mkdir_returns_eexist),
      cmocka_unit_test(ext2_mkdir_returns_einval_for_empty_name),
      cmocka_unit_test(ext2_mkdir_success),
      cmocka_unit_test(ext2_rmdir_fails_dir_not_empty),
      cmocka_unit_test(ext2_rmdir_returns_einval_for_dot),
      cmocka_unit_test(ext2_rmdir_returns_einval_for_dotdot),
      cmocka_unit_test(ext2_rmdir_returns_einval_for_root),
      cmocka_unit_test(ext2_rmdir_returns_enotdir),
      cmocka_unit_test(ext2_rmdir_success),
      cmocka_unit_test(ext2_unlink_defers_free_when_file_open),
      cmocka_unit_test(ext2_unlink_fails_eisdir),
      cmocka_unit_test(ext2_unlink_frees_immediately_when_closed),
  };
  return cmocka_run_group_tests(tests, NULL, NULL);
}
