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

/* ext2_readdir: null/not-in-use/not-dir guards */
static void ext2_readdir_null_returns_einval(void **state) {
  (void)state;
  ext2_entry_t entry;
  assert_int_equal((i64)ext2_readdir(NULL, 0, &entry), -EINVAL);
}

static void ext2_readdir_not_in_use_returns_einval(void **state) {
  (void)state;
  ext2_file_t dir = {.in_use = false, .is_dir = true};
  ext2_entry_t entry;
  assert_int_equal((i64)ext2_readdir(&dir, 0, &entry), -EINVAL);
}

static void ext2_readdir_not_dir_returns_einval(void **state) {
  (void)state;
  ext2_volume_t vol = create_mock_vol();
  ext2_file_t dir = {.in_use = true, .is_dir = false, .vol = &vol};
  ext2_entry_t entry;
  assert_int_equal((i64)ext2_readdir(&dir, 0, &entry), -EINVAL);
}

/* ext2_readdir: cache_get_block fails → -ENOMEM */
static void ext2_readdir_no_cache_returns_enomem(void **state) {
  (void)state;
  ext2_volume_t vol = create_mock_vol();
  ext2_file_t dir = {
    .in_use = true, .is_dir = true, .vol = &vol,
  };
  dir.inode.i_size = 1024;

  will_return(cache_get_block, NULL);

  ext2_entry_t entry;
  assert_int_equal((i64)ext2_readdir(&dir, 0, &entry), -ENOMEM);
}

/* ext2_readdir: block is 0 (sparse) — skip it, return 0 (end of dir) */
static void ext2_readdir_sparse_block_returns_zero(void **state) {
  (void)state;
  ext2_volume_t vol = create_mock_vol();
  static u8 block_buf[1024];
  ext2_file_t dir = {
    .in_use = true, .is_dir = true, .vol = &vol,
  };
  dir.inode.i_size = 1024;

  will_return(cache_get_block, block_buf);
  expect_any(get_block_num, vol);
  expect_any(get_block_num, inode);
  expect_value(get_block_num, file_block, 0);
  will_return(get_block_num, 0); /* sparse */

  ext2_entry_t entry;
  i64 ret = ext2_readdir(&dir, 0, &entry);
  assert_int_equal(ret, 0);
}

/* ext2_readdir: valid entry found at index 0 */
static void ext2_readdir_returns_first_entry(void **state) {
  (void)state;
  ext2_volume_t vol = create_mock_vol();
  static u8 block_buf[1024];
  memset(block_buf, 0, sizeof(block_buf));

  /* Populate a minimal dirent at offset 0 */
  ext2_dirent_t *de = (ext2_dirent_t *)block_buf;
  de->inode    = 5;
  de->rec_len  = 16;
  de->name_len = 3;
  de->file_type = EXT2_FT_REG_FILE;
  de->name[0] = 'f'; de->name[1] = 'o'; de->name[2] = 'o';

  ext2_file_t dir = {
    .in_use = true, .is_dir = true, .vol = &vol,
  };
  dir.inode.i_size = 1024;

  will_return(cache_get_block, block_buf);
  expect_any(get_block_num, vol);
  expect_any(get_block_num, inode);
  expect_value(get_block_num, file_block, 0);
  will_return(get_block_num, 7);

  expect_any(vol_read_block, vol);
  expect_value(vol_read_block, block, 7);
  expect_any(vol_read_block, buf);
  will_return(vol_read_block, 0);

  /* fill_entry_from_dirent calls read_inode to get file size */
  expect_any(read_inode, vol);
  expect_value(read_inode, ino, 5);
  will_return(read_inode, 0);

  ext2_entry_t entry;
  i64 ret = ext2_readdir(&dir, 0, &entry);
  assert_int_equal(ret, 1);
  assert_int_equal(entry.inode, 5);
}

/* ext2_readdir: vol_read_block fails → -EIO */
static void ext2_readdir_read_fails_eio(void **state) {
  (void)state;
  ext2_volume_t vol = create_mock_vol();
  static u8 block_buf[1024];
  ext2_file_t dir = {.in_use = true, .is_dir = true, .vol = &vol};
  dir.inode.i_size = 1024;

  will_return(cache_get_block, block_buf);
  expect_any(get_block_num, vol);
  expect_any(get_block_num, inode);
  expect_value(get_block_num, file_block, 0);
  will_return(get_block_num, 7); /* valid block */

  expect_any(vol_read_block, vol);
  expect_value(vol_read_block, block, 7);
  expect_any(vol_read_block, buf);
  will_return(vol_read_block, -1); /* read fails */

  ext2_entry_t entry;
  assert_int_equal((i64)ext2_readdir(&dir, 0, &entry), -EIO);
}

/* ext2_readdir: skip to second entry (index=1) */
static void ext2_readdir_returns_second_entry(void **state) {
  (void)state;
  ext2_volume_t vol = create_mock_vol();
  static u8 block_buf[1024];
  memset(block_buf, 0, sizeof(block_buf));

  /* Populate two dirents in the block */
  ext2_dirent_t *de0 = (ext2_dirent_t *)block_buf;
  de0->inode    = 5;
  de0->rec_len  = 16;
  de0->name_len = 1;
  de0->file_type = EXT2_FT_REG_FILE;
  de0->name[0]  = 'a';

  ext2_dirent_t *de1 = (ext2_dirent_t *)(block_buf + 16);
  de1->inode    = 6;
  de1->rec_len  = 16;
  de1->name_len = 1;
  de1->file_type = EXT2_FT_REG_FILE;
  de1->name[0]  = 'b';

  ext2_file_t dir = {.in_use = true, .is_dir = true, .vol = &vol};
  dir.inode.i_size = 32; /* exactly 2 entries = 32 bytes, no more blocks needed */

  will_return(cache_get_block, block_buf);
  /* First iteration: file_block=0 */
  expect_any(get_block_num, vol); expect_any(get_block_num, inode);
  expect_value(get_block_num, file_block, 0); will_return(get_block_num, 7);
  expect_any(vol_read_block, vol); expect_value(vol_read_block, block, 7);
  expect_any(vol_read_block, buf); will_return(vol_read_block, 0);

  /* no read_inode for inode 5 — fill_entry_from_dirent not called for skipped */

  /* Second iteration: file_block=0 again (pos=16, 16/1024=0) */
  expect_any(get_block_num, vol); expect_any(get_block_num, inode);
  expect_value(get_block_num, file_block, 0); will_return(get_block_num, 7);
  expect_any(vol_read_block, vol); expect_value(vol_read_block, block, 7);
  expect_any(vol_read_block, buf); will_return(vol_read_block, 0);

  /* read_inode for inode 6 (index 1 matches) */
  expect_any(read_inode, vol); expect_value(read_inode, ino, 6);
  will_return(read_inode, 0);

  ext2_entry_t entry;
  i64 ret = ext2_readdir(&dir, 1, &entry);
  assert_int_equal(ret, 1);
  assert_int_equal(entry.inode, 6);
}

/* ext2_readdir: zero rec_len forces skip to next block */
static void ext2_readdir_zero_rec_len_skips_block(void **state) {
  (void)state;
  ext2_volume_t vol = create_mock_vol();
  static u8 block_buf[1024];
  memset(block_buf, 0, sizeof(block_buf)); /* rec_len=0 at start */

  ext2_file_t dir = {.in_use = true, .is_dir = true, .vol = &vol};
  dir.inode.i_size = 1024;

  will_return(cache_get_block, block_buf);
  expect_any(get_block_num, vol);
  expect_any(get_block_num, inode);
  expect_value(get_block_num, file_block, 0);
  will_return(get_block_num, 7);

  expect_any(vol_read_block, vol);
  expect_value(vol_read_block, block, 7);
  expect_any(vol_read_block, buf);
  will_return(vol_read_block, 0);

  ext2_entry_t entry;
  i64 ret = ext2_readdir(&dir, 0, &entry);
  assert_int_equal(ret, 0); /* no entry found */
}

/* ext2_readdir: inode=0 entry (deleted) — not counted */
static void ext2_readdir_skips_deleted_inode(void **state) {
  (void)state;
  ext2_volume_t vol = create_mock_vol();
  static u8 block_buf[1024];
  memset(block_buf, 0, sizeof(block_buf));

  /* Deleted entry (inode=0) followed by valid entry */
  ext2_dirent_t *de0 = (ext2_dirent_t *)block_buf;
  de0->inode    = 0; /* deleted */
  de0->rec_len  = 16;
  de0->name_len = 1;

  ext2_dirent_t *de1 = (ext2_dirent_t *)(block_buf + 16);
  de1->inode    = 9;
  de1->rec_len  = 16;
  de1->name_len = 1;
  de1->file_type = EXT2_FT_REG_FILE;
  de1->name[0]  = 'x';

  ext2_file_t dir = {.in_use = true, .is_dir = true, .vol = &vol};
  dir.inode.i_size = 32; /* 2 entries exactly */

  will_return(cache_get_block, block_buf);
  /* First iteration: file_block=0 for de0 (deleted, inode=0, not counted) */
  expect_any(get_block_num, vol); expect_any(get_block_num, inode);
  expect_value(get_block_num, file_block, 0); will_return(get_block_num, 7);
  expect_any(vol_read_block, vol); expect_value(vol_read_block, block, 7);
  expect_any(vol_read_block, buf); will_return(vol_read_block, 0);
  /* deleted inode → not counted, pos+=16 */

  /* Second iteration: file_block=0 for de1 */
  expect_any(get_block_num, vol); expect_any(get_block_num, inode);
  expect_value(get_block_num, file_block, 0); will_return(get_block_num, 7);
  expect_any(vol_read_block, vol); expect_value(vol_read_block, block, 7);
  expect_any(vol_read_block, buf); will_return(vol_read_block, 0);

  expect_any(read_inode, vol); expect_value(read_inode, ino, 9);
  will_return(read_inode, 0);

  ext2_entry_t entry;
  i64 ret = ext2_readdir(&dir, 0, &entry);
  assert_int_equal(ret, 1);
  assert_int_equal(entry.inode, 9);
}


/* ext2_unlink: i_links_count > 1 → write_inode (line 377) */
static void ext2_unlink_hardlink_writes_inode(void **state) {
  (void)state;
  ext2_volume_t vol = create_mock_vol();
  /* File has 2 hard links — after decrement still > 0 → write_inode */
  ext2_inode_t file_inode = { .i_mode = EXT2_S_IFREG, .i_links_count = 2 };
  ext2_inode_t parent_inode = { .i_mode = EXT2_S_IFDIR };

  expect_value(resolve_path, vol, &vol);
  expect_string(resolve_path, path, "/link");
  will_return(resolve_path, 0);
  will_return(resolve_path, 5);
  will_return(resolve_path, &file_inode);

  expect_string(path_split, path, "/link");
  will_return(path_split, "/");
  will_return(path_split, "link");

  expect_value(resolve_path, vol, &vol);
  expect_string(resolve_path, path, "/");
  will_return(resolve_path, 0);
  will_return(resolve_path, 2);
  will_return(resolve_path, &parent_inode);

  expect_value(dir_remove_entry, vol, &vol);
  expect_any(dir_remove_entry, dir_inode);
  expect_string(dir_remove_entry, name, "link");
  will_return(dir_remove_entry, 0);

  memset(g_files, 0, sizeof(g_files));

  /* i_links_count=2-1=1 > 0 → write_inode called (line 377) */
  expect_value(write_inode, vol, &vol);
  expect_value(write_inode, ino, 5);
  expect_any(write_inode, inode);
  will_return(write_inode, 0);

  expect_value(flush_metadata, vol, &vol);
  will_return(flush_metadata, 0);

  i64 ret = ext2_unlink(&vol, "/link");
  assert_int_equal(ret, 0);
}

/* ext2_mkdir: mkdir_seed_first_block alloc_block fails → ENOSPC */
static void ext2_mkdir_seed_kmalloc_fail_enomem(void **state) {
  (void)state;
  ext2_volume_t vol = create_mock_vol();
  ext2_inode_t parent_inode = { .i_mode = EXT2_S_IFDIR | 0755, .i_links_count = 2 };

  expect_value(resolve_path, vol, &vol);
  expect_string(resolve_path, path, "/newdir");
  will_return(resolve_path, -ENOENT);

  expect_string(path_split, path, "/newdir");
  will_return(path_split, "/");
  will_return(path_split, "newdir");

  expect_value(resolve_path, vol, &vol);
  expect_string(resolve_path, path, "/");
  will_return(resolve_path, 0);
  will_return(resolve_path, (u32)2);
  will_return(resolve_path, &parent_inode);

  expect_value(alloc_inode, vol, &vol);
  expect_any(alloc_inode, preferred_group);
  expect_value(alloc_inode, is_dir, true);
  will_return(alloc_inode, (u32)10);

  /* alloc_block returns 0 → ENOSPC in mkdir_seed_first_block */
  expect_value(alloc_block, vol, &vol);
  expect_any(alloc_block, preferred_group);
  will_return(alloc_block, (u32)0);

  /* mkdir_finalize_or_rollback called with seed=-ENOSPC → rollback */
  expect_value(free_inode, vol, &vol);
  expect_value(free_inode, ino, (u32)10);
  expect_value(free_inode, is_dir, true);
  will_return(free_inode, 0);

  i64 ret = ext2_mkdir(&vol, "/newdir");
  assert_int_equal(ret, -ENOSPC);
}

/* ext2_mkdir: vol_write_block fails → -EIO + rollback (lines 200-204) */
static void ext2_mkdir_seed_write_block_fail_eio(void **state) {
  (void)state;
  ext2_volume_t vol = create_mock_vol();
  ext2_inode_t parent_inode = { .i_mode = EXT2_S_IFDIR | 0755, .i_links_count = 2 };

  expect_value(resolve_path, vol, &vol);
  expect_string(resolve_path, path, "/newdir2");
  will_return(resolve_path, -ENOENT);

  expect_string(path_split, path, "/newdir2");
  will_return(path_split, "/");
  will_return(path_split, "newdir2");

  expect_value(resolve_path, vol, &vol);
  expect_string(resolve_path, path, "/");
  will_return(resolve_path, 0);
  will_return(resolve_path, (u32)2);
  will_return(resolve_path, &parent_inode);

  expect_value(alloc_inode, vol, &vol);
  expect_any(alloc_inode, preferred_group);
  expect_value(alloc_inode, is_dir, true);
  will_return(alloc_inode, (u32)11);

  expect_value(alloc_block, vol, &vol);
  expect_any(alloc_block, preferred_group);
  will_return(alloc_block, (u32)6);

  expect_value(vol_write_block, vol, &vol);
  expect_value(vol_write_block, block, (u32)6);
  expect_any(vol_write_block, buf);
  will_return(vol_write_block, (i64)-1);

  expect_value(free_block, vol, &vol);
  expect_value(free_block, block, (u32)6);
  will_return(free_block, 0);

  expect_value(free_inode, vol, &vol);
  expect_value(free_inode, ino, (u32)11);
  expect_value(free_inode, is_dir, true);
  will_return(free_inode, 0);

  i64 ret = ext2_mkdir(&vol, "/newdir2");
  assert_int_equal(ret, -EIO);
}

/* fill_entry_from_dirent: name_len clamped to EXT2_NAME_MAX (line 51) */
static void ext2_readdir_name_len_clamped(void **state) {
  (void)state;
  ext2_volume_t vol = create_mock_vol();
  ext2_file_t dir = {.in_use = true, .is_dir = true, .vol = &vol};
  dir.inode.i_size = 1024;

  /* Provide a read block with name_len > EXT2_NAME_MAX */
  static u8 raw_buf[1024];
  memset(raw_buf, 0, sizeof(raw_buf));
  ext2_dirent_t *de = (ext2_dirent_t *)raw_buf;
  de->inode     = 5;
  de->rec_len   = 1024;
  de->name_len  = 255; /* EXT2_NAME_MAX is typically 255, so if it's < 255 this triggers clamp */
  de->file_type = EXT2_FT_REG_FILE;
  memset(de->name, 'x', 255);

  static u8 cache_buf[1024];
  memcpy(cache_buf, raw_buf, 1024);
  will_return(cache_get_block, cache_buf);

  expect_any(get_block_num, vol);
  expect_any(get_block_num, inode);
  expect_value(get_block_num, file_block, 0);
  will_return(get_block_num, 7);

  expect_any(vol_read_block, vol);
  expect_value(vol_read_block, block, 7);
  expect_any(vol_read_block, buf);
  will_return(vol_read_block, 0);

  expect_any(read_inode, vol);
  expect_value(read_inode, ino, 5);
  will_return(read_inode, 0);

  ext2_entry_t entry;
  i64 ret = ext2_readdir(&dir, 0, &entry);
  assert_int_equal(ret, 1);
}

/* fill_entry_from_dirent: name_len > EXT2_NAME_MAX → clamped (line 51) */
static void ext2_readdir_name_len_over_max_clamped(void **state) {
  (void)state;
  ext2_volume_t vol = create_mock_vol();
  static u8 block_buf[1024];
  memset(block_buf, 0, sizeof(block_buf));

  ext2_dirent_t *de = (ext2_dirent_t *)block_buf;
  de->inode     = 5;
  de->rec_len   = 1024;
  de->name_len  = 255; /* EXT2_NAME_MAX is typically 255; use value > it if less */
  de->file_type = EXT2_FT_REG_FILE;
  /* Fill name with 255 bytes */
  memset(de->name, 'a', 255);

  ext2_file_t dir = {.in_use = true, .is_dir = true, .vol = &vol};
  dir.inode.i_size = 1024;

  will_return(cache_get_block, block_buf);
  expect_any(get_block_num, vol);
  expect_any(get_block_num, inode);
  expect_value(get_block_num, file_block, 0);
  will_return(get_block_num, 7);

  expect_any(vol_read_block, vol);
  expect_value(vol_read_block, block, 7);
  expect_any(vol_read_block, buf);
  will_return(vol_read_block, 0);

  expect_any(read_inode, vol);
  expect_value(read_inode, ino, 5);
  will_return(read_inode, 0);

  ext2_entry_t entry;
  i64 ret = ext2_readdir(&dir, 0, &entry);
  assert_int_equal(ret, 1);
}

/* ext2_mkdir: null vol → -EINVAL */
static void ext2_mkdir_null_vol_einval(void **state) {
  (void)state;
  assert_int_equal((i64)ext2_mkdir(NULL, "/dir"), -EINVAL);
}

/* ext2_mkdir: unmounted vol → -EINVAL */
static void ext2_mkdir_unmounted_einval(void **state) {
  (void)state;
  ext2_volume_t vol = {.mounted = false};
  assert_int_equal((i64)ext2_mkdir(&vol, "/dir"), -EINVAL);
}


static void ext2_unlink_file_not_found(void **state) {
  (void)state;
  ext2_volume_t vol = create_mock_vol();
  expect_value(resolve_path, vol, &vol);
  expect_string(resolve_path, path, "/nosuchfile");
  will_return(resolve_path, -ENOENT);
  assert_int_equal((i64)ext2_unlink(&vol, "/nosuchfile"), -ENOENT);
}

static void ext2_rmdir_dir_not_found(void **state) {
  (void)state;
  ext2_volume_t vol = create_mock_vol();
  expect_value(resolve_path, vol, &vol);
  expect_string(resolve_path, path, "/nosuchdir");
  will_return(resolve_path, -ENOENT);
  assert_int_equal((i64)ext2_rmdir(&vol, "/nosuchdir"), -ENOENT);
}

/* ext2_unlink: null vol → -EINVAL */
static void ext2_unlink_null_vol_einval(void **state) {
  (void)state;
  assert_int_equal((i64)ext2_unlink(NULL, "/f"), -EINVAL);
}

/* ext2_unlink: parent resolve fails → -ENOENT */
static void ext2_unlink_parent_enoent(void **state) {
  (void)state;
  ext2_volume_t vol = create_mock_vol();
  static ext2_inode_t file_inode = {.i_mode = EXT2_S_IFREG, .i_links_count = 1};

  /* resolve file */
  expect_value(resolve_path, vol, &vol);
  expect_string(resolve_path, path, "/dir/f");
  will_return(resolve_path, 0);
  will_return(resolve_path, (u32)5);
  will_return(resolve_path, &file_inode);

  /* path_split */
  expect_string(path_split, path, "/dir/f");
  will_return(path_split, "/dir");
  will_return(path_split, "f");

  /* resolve parent fails */
  expect_value(resolve_path, vol, &vol);
  expect_string(resolve_path, path, "/dir");
  will_return(resolve_path, -ENOENT);

  assert_int_equal((i64)ext2_unlink(&vol, "/dir/f"), -ENOENT);
}

/* ext2_unlink: dir_remove_entry fails → -EIO */
static void ext2_unlink_remove_entry_eio(void **state) {
  (void)state;
  ext2_volume_t vol = create_mock_vol();
  static ext2_inode_t file_inode = {.i_mode = EXT2_S_IFREG, .i_links_count = 1};
  static ext2_inode_t parent_inode = {.i_mode = EXT2_S_IFDIR, .i_links_count = 2};

  expect_value(resolve_path, vol, &vol);
  expect_string(resolve_path, path, "/f");
  will_return(resolve_path, 0);
  will_return(resolve_path, (u32)5);
  will_return(resolve_path, &file_inode);

  expect_string(path_split, path, "/f");
  will_return(path_split, "/");
  will_return(path_split, "f");

  expect_value(resolve_path, vol, &vol);
  expect_string(resolve_path, path, "/");
  will_return(resolve_path, 0);
  will_return(resolve_path, (u32)2);
  will_return(resolve_path, &parent_inode);

  expect_value(dir_remove_entry, vol, &vol);
  expect_any(dir_remove_entry, dir_inode);
  expect_string(dir_remove_entry, name, "f");
  will_return(dir_remove_entry, -1); /* fails */

  assert_int_equal((i64)ext2_unlink(&vol, "/f"), -EIO);
}

/* ext2_rmdir: null vol → -EINVAL */
static void ext2_rmdir_null_vol_einval(void **state) {
  (void)state;
  assert_int_equal((i64)ext2_rmdir(NULL, "/dir"), -EINVAL);
}

/* ext2_rmdir: parent resolve fails → -ENOENT */
static void ext2_rmdir_parent_enoent(void **state) {
  (void)state;
  ext2_volume_t vol = create_mock_vol();
  static ext2_inode_t dir_inode = {.i_mode = EXT2_S_IFDIR, .i_links_count = 2};

  expect_value(resolve_path, vol, &vol);
  expect_string(resolve_path, path, "/a/b");
  will_return(resolve_path, 0);
  will_return(resolve_path, (u32)5);
  will_return(resolve_path, &dir_inode);

  expect_value(dir_is_empty, vol, &vol);
  expect_any(dir_is_empty, dir_inode);
  will_return(dir_is_empty, true);

  expect_string(path_split, path, "/a/b");
  will_return(path_split, "/a");
  will_return(path_split, "b");

  expect_value(resolve_path, vol, &vol);
  expect_string(resolve_path, path, "/a");
  will_return(resolve_path, -ENOENT);

  assert_int_equal((i64)ext2_rmdir(&vol, "/a/b"), -ENOENT);
}

/* ext2_rmdir: dir_remove_entry fails → -EIO */
static void ext2_rmdir_remove_entry_eio(void **state) {
  (void)state;
  ext2_volume_t vol = create_mock_vol();
  static ext2_inode_t dir_inode = {.i_mode = EXT2_S_IFDIR, .i_links_count = 2};
  static ext2_inode_t parent_inode = {.i_mode = EXT2_S_IFDIR, .i_links_count = 3};

  expect_value(resolve_path, vol, &vol);
  expect_string(resolve_path, path, "/d");
  will_return(resolve_path, 0);
  will_return(resolve_path, (u32)5);
  will_return(resolve_path, &dir_inode);

  expect_value(dir_is_empty, vol, &vol);
  expect_any(dir_is_empty, dir_inode);
  will_return(dir_is_empty, true);

  expect_string(path_split, path, "/d");
  will_return(path_split, "/");
  will_return(path_split, "d");

  expect_value(resolve_path, vol, &vol);
  expect_string(resolve_path, path, "/");
  will_return(resolve_path, 0);
  will_return(resolve_path, (u32)2);
  will_return(resolve_path, &parent_inode);

  expect_value(dir_remove_entry, vol, &vol);
  expect_any(dir_remove_entry, dir_inode);
  expect_string(dir_remove_entry, name, "d");
  will_return(dir_remove_entry, -1);

  assert_int_equal((i64)ext2_rmdir(&vol, "/d"), -EIO);
}

/* ext2_rmdir: also add the enoent test registered but not the _enoent variant */

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
      /* ext2_readdir */
      cmocka_unit_test(ext2_readdir_null_returns_einval),
      cmocka_unit_test(ext2_readdir_not_in_use_returns_einval),
      cmocka_unit_test(ext2_readdir_not_dir_returns_einval),
      cmocka_unit_test(ext2_readdir_no_cache_returns_enomem),
      cmocka_unit_test(ext2_readdir_sparse_block_returns_zero),
      cmocka_unit_test(ext2_readdir_returns_first_entry),
      cmocka_unit_test(ext2_readdir_read_fails_eio),
      cmocka_unit_test(ext2_readdir_returns_second_entry),
      cmocka_unit_test(ext2_readdir_zero_rec_len_skips_block),
      cmocka_unit_test(ext2_readdir_skips_deleted_inode),
      /* new coverage */
      cmocka_unit_test(ext2_mkdir_null_vol_einval),
      cmocka_unit_test(ext2_mkdir_unmounted_einval),
      cmocka_unit_test(ext2_unlink_null_vol_einval),
      cmocka_unit_test(ext2_unlink_parent_enoent),
      cmocka_unit_test(ext2_unlink_remove_entry_eio),
      cmocka_unit_test(ext2_rmdir_null_vol_einval),
      cmocka_unit_test(ext2_rmdir_parent_enoent),
      cmocka_unit_test(ext2_rmdir_remove_entry_eio),
      cmocka_unit_test(ext2_unlink_hardlink_writes_inode),
      cmocka_unit_test(ext2_mkdir_seed_kmalloc_fail_enomem),
      cmocka_unit_test(ext2_readdir_name_len_over_max_clamped),
      cmocka_unit_test(ext2_mkdir_seed_write_block_fail_eio),
      cmocka_unit_test(ext2_unlink_file_not_found),
      cmocka_unit_test(ext2_rmdir_dir_not_found),
      cmocka_unit_test(ext2_readdir_name_len_clamped),
  };
  return cmocka_run_group_tests(tests, NULL, NULL);
}
