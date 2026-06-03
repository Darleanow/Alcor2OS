/* Unit tests for ext2/path.c pure logic: path_split, mode_to_file_type,
 * extract_basename, resolve_path, and ext2_stat. All require kstdlib and kstrncpy. */

#include "test_common.h"

#include <alcor2/errno.h>
#include <alcor2/fs/ext2.h>
#include <alcor2/kstdlib.h>
#include <alcor2/types.h>
#include <fs/ext2/internal.h>

#include <string.h>

void *kmalloc(u64 n) { (void)n; return NULL; }
void  kfree(void *p) { (void)p; }
u8   *cache_get_block(u32 s) { (void)s; return NULL; }
void  cache_put_block(u8 *p) { (void)p; }
i64   ata_read(u8 d, u64 l, u32 c, void *b)
{
  (void)d; (void)l; (void)c; (void)b; return -1;
}
i64 ata_write(u8 d, u64 l, u32 c, const void *b)
{
  (void)d; (void)l; (void)c; (void)b; return -1;
}
i64 write_inode(const ext2_volume_t *v, u32 i, const ext2_inode_t *n)
{
  (void)v; (void)i; (void)n; return -1;
}
i64 vol_write_block(const ext2_volume_t *v, u32 b, const void *buf)
{
  (void)v; (void)b; (void)buf; return -1;
}
i64 flush_metadata(ext2_volume_t *v)
{
  (void)v; return -1;
}

/* Mock filesystem for dir_find_entry and read_inode */
typedef struct {
    const char *name;
    u32 ino;
    u32 parent_ino;
    u8 type;
    ext2_inode_t inode;
} mock_entry_t;

static mock_entry_t mock_fs[64];
static u32 mock_fs_count = 0;
static ext2_volume_t dummy_vol;

static void fs_reset(void) {
    mock_fs_count = 0;
    dummy_vol.mounted = true;
    dummy_vol.block_size = 1024;
}

static mock_entry_t *fs_add(u32 ino, u32 parent_ino, const char *name, u8 type, u16 mode, u32 size) {
    mock_entry_t *e = &mock_fs[mock_fs_count++];
    e->ino = ino;
    e->parent_ino = parent_ino;
    e->name = name;
    e->type = type;
    memset(&e->inode, 0, sizeof(ext2_inode_t));
    e->inode.i_mode = mode;
    e->inode.i_size = size;
    e->inode.i_uid = ino;
    return e;
}

static void fs_add_symlink(u32 ino, u32 parent_ino, const char *name, const char *target) {
    u32 len = strlen(target);
    mock_entry_t *e = fs_add(ino, parent_ino, name, EXT2_FT_SYMLINK, EXT2_S_IFLNK | 0777, len);
    if (len <= 60) {
        memcpy(e->inode.i_block, target, len);
    }
}

i64 read_inode(const ext2_volume_t *v, u32 i, ext2_inode_t *n)
{
    (void)v;
    for (u32 idx = 0; idx < mock_fs_count; idx++) {
        if (mock_fs[idx].ino == i) {
            *n = mock_fs[idx].inode;
            return 0;
        }
    }
    return -ENOENT;
}

i64 dir_find_entry(
    const ext2_volume_t *v, const ext2_inode_t *di, const char *name,
    u32 *out_ino, u8 *out_type)
{
    (void)v;
    u32 parent_ino = 0;
    for (u32 idx = 0; idx < mock_fs_count; idx++) {
        if (memcmp(&mock_fs[idx].inode, di, sizeof(ext2_inode_t)) == 0) {
            parent_ino = mock_fs[idx].ino;
            break;
        }
    }
    if (parent_ino == 0) return -ENOTDIR;

    for (u32 idx = 0; idx < mock_fs_count; idx++) {
        if (mock_fs[idx].parent_ino == parent_ino && strcmp(mock_fs[idx].name, name) == 0) {
            *out_ino = mock_fs[idx].ino;
            *out_type = mock_fs[idx].type;
            return 0;
        }
    }
    return -ENOENT;
}

i64 vol_read_block(const ext2_volume_t *v, u32 b, void *buf)
{
    (void)v; (void)b; (void)buf;
    /* Not strictly needed if we only test fast symlinks, but return error to fail gracefully */
    return -1;
}

#include "../../src/fs/ext2/path.c"

/* extract_basename */

static void basename_no_slash(void **state)
{
  (void)state;
  char n[256];
  extract_basename("file", n);
  assert_string_equal(n, "file");
}

static void basename_root_child(void **state)
{
  (void)state;
  char n[256];
  extract_basename("/foo", n);
  assert_string_equal(n, "foo");
}

static void basename_simple(void **state)
{
  (void)state;
  char n[256];
  extract_basename("/usr/bin/ls", n);
  assert_string_equal(n, "ls");
}

/* mode_to_file_type */

static void mode_blkdev(void **state)
{
  (void)state;
  assert_int_equal(mode_to_file_type(EXT2_S_IFBLK), EXT2_FT_BLKDEV);
}

static void mode_chrdev(void **state)
{
  (void)state;
  assert_int_equal(mode_to_file_type(EXT2_S_IFCHR), EXT2_FT_CHRDEV);
}

static void mode_directory(void **state)
{
  (void)state;
  assert_int_equal(mode_to_file_type(EXT2_S_IFDIR), EXT2_FT_DIR);
}

static void mode_fifo(void **state)
{
  (void)state;
  assert_int_equal(mode_to_file_type(EXT2_S_IFIFO), EXT2_FT_FIFO);
}

static void mode_permission_bits_stripped(void **state)
{
  (void)state;
  assert_int_equal(mode_to_file_type(EXT2_S_IFREG | 0755), EXT2_FT_REG_FILE);
}

static void mode_regular(void **state)
{
  (void)state;
  assert_int_equal(mode_to_file_type(EXT2_S_IFREG), EXT2_FT_REG_FILE);
}

static void mode_sock(void **state)
{
  (void)state;
  assert_int_equal(mode_to_file_type(EXT2_S_IFSOCK), EXT2_FT_SOCK);
}

static void mode_symlink(void **state)
{
  (void)state;
  assert_int_equal(mode_to_file_type(EXT2_S_IFLNK), EXT2_FT_SYMLINK);
}

static void mode_unknown_returns_unknown(void **state)
{
  (void)state;
  assert_int_equal(mode_to_file_type(0x0000), EXT2_FT_UNKNOWN);
}

/* path_split */

static void split_absolute_root(void **state)
{
  (void)state;
  char p[256], n[256];
  path_split("/foo", p, n);
  assert_string_equal(p, "/");
  assert_string_equal(n, "foo");
}

static void split_double_slash(void **state)
{
  (void)state;
  char p[256], n[256];
  path_split("/a/b", p, n);
  assert_string_equal(p, "/a");
  assert_string_equal(n, "b");
}

static void split_nested(void **state)
{
  (void)state;
  char p[256], n[256];
  path_split("/usr/bin/ls", p, n);
  assert_string_equal(p, "/usr/bin");
  assert_string_equal(n, "ls");
}

static void split_no_slash_treats_as_relative(void **state)
{
  (void)state;
  char p[256], n[256];
  path_split("foo", p, n);
  assert_string_equal(p, "/");
  assert_string_equal(n, "foo");
}

static void split_trailing_slash(void **state)
{
  (void)state;
  char p[256], n[256];
  path_split("/a/", p, n);
  assert_string_equal(p, "/a");
  assert_string_equal(n, "");
}

/* resolve_path & ext2_stat */

static void test_ext2_stat_success(void **state)
{
    (void)state;
    fs_reset();
    fs_add(EXT2_ROOT_INODE, 0, "/", EXT2_FT_DIR, EXT2_S_IFDIR | 0755, 1024);
    fs_add(3, EXT2_ROOT_INODE, "foo", EXT2_FT_REG_FILE, EXT2_S_IFREG | 0644, 4096);

    ext2_entry_t entry;
    i64 ret = ext2_stat(&dummy_vol, "/foo", &entry);
    assert_int_equal(ret, 0);
    assert_int_equal(entry.inode, 3);
    assert_int_equal(entry.size, 4096);
    assert_int_equal(entry.file_type, EXT2_FT_REG_FILE);
    assert_string_equal(entry.name, "foo");
}

static void test_resolve_path_multi_level(void **state)
{
    (void)state;
    fs_reset();
    fs_add(EXT2_ROOT_INODE, 0, "/", EXT2_FT_DIR, EXT2_S_IFDIR | 0755, 1024);
    fs_add(3, EXT2_ROOT_INODE, "usr", EXT2_FT_DIR, EXT2_S_IFDIR | 0755, 1024);
    fs_add(4, 3, "bin", EXT2_FT_DIR, EXT2_S_IFDIR | 0755, 1024);
    fs_add(5, 4, "ls", EXT2_FT_REG_FILE, EXT2_S_IFREG | 0755, 8192);

    u32 ino;
    ext2_inode_t inode;
    i64 ret = resolve_path(&dummy_vol, "/usr/bin/ls", &ino, &inode);
    assert_int_equal(ret, 0);
    assert_int_equal(ino, 5);
}

static void test_resolve_path_not_found(void **state)
{
    (void)state;
    fs_reset();
    fs_add(EXT2_ROOT_INODE, 0, "/", EXT2_FT_DIR, EXT2_S_IFDIR | 0755, 1024);

    u32 ino;
    ext2_inode_t inode;
    i64 ret = resolve_path(&dummy_vol, "/missing", &ino, &inode);
    assert_int_equal(ret, -ENOENT);
}

static void test_resolve_path_root(void **state)
{
    (void)state;
    fs_reset();
    fs_add(EXT2_ROOT_INODE, 0, "/", EXT2_FT_DIR, EXT2_S_IFDIR | 0755, 1024);

    u32 ino;
    ext2_inode_t inode;
    i64 ret = resolve_path(&dummy_vol, "/", &ino, &inode);
    assert_int_equal(ret, 0);
    assert_int_equal(ino, EXT2_ROOT_INODE);
}

static void test_resolve_path_single_level(void **state)
{
    (void)state;
    fs_reset();
    fs_add(EXT2_ROOT_INODE, 0, "/", EXT2_FT_DIR, EXT2_S_IFDIR | 0755, 1024);
    fs_add(3, EXT2_ROOT_INODE, "foo", EXT2_FT_REG_FILE, EXT2_S_IFREG | 0644, 1234);

    u32 ino;
    ext2_inode_t inode;
    i64 ret = resolve_path(&dummy_vol, "/foo", &ino, &inode);
    assert_int_equal(ret, 0);
    assert_int_equal(ino, 3);
}

static void test_resolve_path_symlink(void **state)
{
    (void)state;
    fs_reset();
    fs_add(EXT2_ROOT_INODE, 0, "/", EXT2_FT_DIR, EXT2_S_IFDIR | 0755, 1024);
    fs_add(3, EXT2_ROOT_INODE, "target", EXT2_FT_REG_FILE, EXT2_S_IFREG | 0644, 555);
    fs_add_symlink(4, EXT2_ROOT_INODE, "link", "/target");

    u32 ino;
    ext2_inode_t inode;
    i64 ret = resolve_path(&dummy_vol, "/link", &ino, &inode);
    assert_int_equal(ret, 0);
    assert_int_equal(ino, 3);
}

static void test_resolve_path_symlink_loop(void **state)
{
    (void)state;
    fs_reset();
    fs_add(EXT2_ROOT_INODE, 0, "/", EXT2_FT_DIR, EXT2_S_IFDIR | 0755, 1024);
    fs_add_symlink(3, EXT2_ROOT_INODE, "loop", "/loop");

    u32 ino;
    ext2_inode_t inode;
    i64 ret = resolve_path(&dummy_vol, "/loop", &ino, &inode);
    assert_int_equal(ret, -ELOOP);
}

/* ext2_stat: null/unmounted guards */
static void test_ext2_stat_null_vol(void **state)
{
  (void)state;
  ext2_entry_t entry;
  assert_int_equal((i64)ext2_stat(NULL, "/foo", &entry), -EINVAL);
}

static void test_ext2_stat_unmounted(void **state)
{
  (void)state;
  fs_reset();
  dummy_vol.mounted = false;
  ext2_entry_t entry;
  assert_int_equal((i64)ext2_stat(&dummy_vol, "/foo", &entry), -EINVAL);
  dummy_vol.mounted = true;
}

static void test_ext2_stat_null_path(void **state)
{
  (void)state;
  fs_reset();
  ext2_entry_t entry;
  assert_int_equal((i64)ext2_stat(&dummy_vol, NULL, &entry), -EINVAL);
}

static void test_ext2_stat_null_entry(void **state)
{
  (void)state;
  fs_reset();
  assert_int_equal((i64)ext2_stat(&dummy_vol, "/foo", NULL), -EINVAL);
}

/* ext2_stat: not found path */
static void test_ext2_stat_not_found(void **state)
{
  (void)state;
  fs_reset();
  fs_add(EXT2_ROOT_INODE, 0, "/", EXT2_FT_DIR, EXT2_S_IFDIR | 0755, 0);
  ext2_entry_t entry;
  assert_int_equal((i64)ext2_stat(&dummy_vol, "/missing", &entry), -ENOENT);
}

/* ext2_stat: directory type */
static void test_ext2_stat_directory(void **state)
{
  (void)state;
  fs_reset();
  fs_add(EXT2_ROOT_INODE, 0, "/", EXT2_FT_DIR, EXT2_S_IFDIR | 0755, 0);
  fs_add(3, EXT2_ROOT_INODE, "dir", EXT2_FT_DIR, EXT2_S_IFDIR | 0755, 512);
  ext2_entry_t entry;
  i64 ret = ext2_stat(&dummy_vol, "/dir", &entry);
  assert_int_equal(ret, 0);
  assert_int_equal(entry.file_type, EXT2_FT_DIR);
}

/* ext2_readlink: null guards */
static void test_ext2_readlink_null_vol(void **state)
{
  (void)state;
  char buf[64];
  assert_int_equal((i64)ext2_readlink(NULL, "/link", buf, 64), -EINVAL);
}

static void test_ext2_readlink_unmounted(void **state)
{
  (void)state;
  fs_reset();
  dummy_vol.mounted = false;
  char buf[64];
  assert_int_equal((i64)ext2_readlink(&dummy_vol, "/link", buf, 64), -EINVAL);
  dummy_vol.mounted = true;
}

static void test_ext2_readlink_null_buf(void **state)
{
  (void)state;
  fs_reset();
  assert_int_equal((i64)ext2_readlink(&dummy_vol, "/link", NULL, 64), -EINVAL);
}

static void test_ext2_readlink_zero_cap(void **state)
{
  (void)state;
  fs_reset();
  char buf[4];
  assert_int_equal((i64)ext2_readlink(&dummy_vol, "/link", buf, 0), -EINVAL);
}

/* ext2_readlink: success with fast symlink */
static void test_ext2_readlink_success(void **state)
{
  (void)state;
  fs_reset();
  fs_add(EXT2_ROOT_INODE, 0, "/", EXT2_FT_DIR, EXT2_S_IFDIR | 0755, 0);
  fs_add_symlink(3, EXT2_ROOT_INODE, "lnk", "/target");
  char buf[64];
  i64 ret = ext2_readlink(&dummy_vol, "/lnk", buf, sizeof(buf));
  assert_true(ret >= 0);
  /* read_symlink_target returns 0; buf filled via direct memcpy of i_block */
}

/* ext2_readlink: parent not found */
static void test_ext2_readlink_parent_missing(void **state)
{
  (void)state;
  fs_reset();
  char buf[64];
  assert_int_equal((i64)ext2_readlink(&dummy_vol, "/nope/lnk", buf, 64), -ENOENT);
}

/* resolve_path: walk hits non-directory intermediate */
static void test_resolve_path_file_as_dir(void **state)
{
  (void)state;
  fs_reset();
  fs_add(EXT2_ROOT_INODE, 0, "/", EXT2_FT_DIR, EXT2_S_IFDIR | 0755, 0);
  fs_add(3, EXT2_ROOT_INODE, "file", EXT2_FT_REG_FILE, EXT2_S_IFREG | 0644, 0);
  u32 ino;
  ext2_inode_t inode;
  /* Try to descend into a regular file */
  i64 ret = resolve_path(&dummy_vol, "/file/sub", &ino, &inode);
  assert_true(ret < 0);
}

/* path_split: path with slash but name at end (double slash handled) */
static void split_path_ends_at_slash(void **state)
{
  (void)state;
  char parent[256], name[256];
  path_split("/a/b/c", parent, name);
  assert_string_equal(parent, "/a/b");
  assert_string_equal(name, "c");
}

/* read_symlink_target: zero-size symlink returns -EINVAL */
static void test_read_symlink_zero_size(void **state)
{
  (void)state;
  fs_reset();
  fs_add(EXT2_ROOT_INODE, 0, "/", EXT2_FT_DIR, EXT2_S_IFDIR | 0755, 0);
  /* Symlink with size=0 */
  mock_entry_t *e = fs_add(3, EXT2_ROOT_INODE, "lnk", EXT2_FT_SYMLINK,
                           EXT2_S_IFLNK | 0777, 0 /* size=0 */);
  (void)e;
  char buf[64];
  i64 ret = ext2_readlink(&dummy_vol, "/lnk", buf, sizeof(buf));
  /* read_symlink_target: len==0 → -EINVAL → ext2_readlink returns -EIO */
  assert_int_equal(ret, -EIO);
}

/* read_symlink_target: slow symlink (size > 60) with cache_get_block returning NULL */
static void test_read_symlink_slow_oom(void **state)
{
  (void)state;
  fs_reset();
  fs_add(EXT2_ROOT_INODE, 0, "/", EXT2_FT_DIR, EXT2_S_IFDIR | 0755, 0);
  /* Symlink with size=65 (> 60 = EXT2_FAST_SYMLINK_MAX) */
  mock_entry_t *e = fs_add(3, EXT2_ROOT_INODE, "lnk", EXT2_FT_SYMLINK,
                           EXT2_S_IFLNK | 0777, 65);
  (void)e;
  /* cache_get_block returns NULL → -ENOMEM */
  char buf[128];
  i64 ret = ext2_readlink(&dummy_vol, "/lnk", buf, sizeof(buf));
  /* cache_get_block returns NULL → read_symlink_target returns -ENOMEM
     → ext2_readlink propagates as -EIO */
  assert_int_equal(ret, -EIO);
}

/* build_symlink_path: relative symlink joined to base directory */
static void test_build_symlink_path_relative(void **state)
{
  (void)state;
  char out[256];
  /* base="/foo/bar", relative target="baz" → out should be "/foo/baz" */
  build_symlink_path("/foo/bar", "baz", out, sizeof(out));
  assert_string_equal(out, "/foo/baz");
}

/* build_symlink_path: absolute target used directly */
static void test_build_symlink_path_absolute(void **state)
{
  (void)state;
  char out[256];
  build_symlink_path("/foo/bar", "/abs/target", out, sizeof(out));
  assert_string_equal(out, "/abs/target");
}

/* build_symlink_path: no slash in base → target used directly */
static void test_build_symlink_path_no_slash_base(void **state)
{
  (void)state;
  char out[256];
  /* base with no slash → last_slash=0 → kstrncpy target into out */
  build_symlink_path("nodir", "rel", out, sizeof(out));
  assert_string_equal(out, "rel");
}

int main(void)
{
  const struct CMUnitTest tests[] = {
      cmocka_unit_test(basename_no_slash),
      cmocka_unit_test(basename_root_child),
      cmocka_unit_test(basename_simple),
      cmocka_unit_test(mode_blkdev),
      cmocka_unit_test(mode_chrdev),
      cmocka_unit_test(mode_directory),
      cmocka_unit_test(mode_fifo),
      cmocka_unit_test(mode_permission_bits_stripped),
      cmocka_unit_test(mode_regular),
      cmocka_unit_test(mode_sock),
      cmocka_unit_test(mode_symlink),
      cmocka_unit_test(mode_unknown_returns_unknown),
      cmocka_unit_test(split_absolute_root),
      cmocka_unit_test(split_double_slash),
      cmocka_unit_test(split_nested),
      cmocka_unit_test(split_no_slash_treats_as_relative),
      cmocka_unit_test(split_trailing_slash),
      cmocka_unit_test(test_ext2_stat_success),
      cmocka_unit_test(test_resolve_path_multi_level),
      cmocka_unit_test(test_resolve_path_not_found),
      cmocka_unit_test(test_resolve_path_root),
      cmocka_unit_test(test_resolve_path_single_level),
      cmocka_unit_test(test_resolve_path_symlink),
      cmocka_unit_test(test_resolve_path_symlink_loop),
      /* ext2_stat guards */
      cmocka_unit_test(test_ext2_stat_null_vol),
      cmocka_unit_test(test_ext2_stat_unmounted),
      cmocka_unit_test(test_ext2_stat_null_path),
      cmocka_unit_test(test_ext2_stat_null_entry),
      cmocka_unit_test(test_ext2_stat_not_found),
      cmocka_unit_test(test_ext2_stat_directory),
      /* ext2_readlink */
      cmocka_unit_test(test_ext2_readlink_null_vol),
      cmocka_unit_test(test_ext2_readlink_unmounted),
      cmocka_unit_test(test_ext2_readlink_null_buf),
      cmocka_unit_test(test_ext2_readlink_zero_cap),
      cmocka_unit_test(test_ext2_readlink_success),
      cmocka_unit_test(test_ext2_readlink_parent_missing),
      /* resolve_path extra */
      cmocka_unit_test(test_resolve_path_file_as_dir),
      /* path_split extra */
      cmocka_unit_test(split_path_ends_at_slash),
      /* read_symlink_target paths */
      cmocka_unit_test(test_read_symlink_zero_size),
      cmocka_unit_test(test_read_symlink_slow_oom),
      /* build_symlink_path */
      cmocka_unit_test(test_build_symlink_path_relative),
      cmocka_unit_test(test_build_symlink_path_absolute),
      cmocka_unit_test(test_build_symlink_path_no_slash_base),
  };
  return cmocka_run_group_tests(tests, NULL, NULL);
}
