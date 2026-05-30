/* Unit tests for ext2/path.c pure logic: path_split, mode_to_file_type,
 * extract_basename. All require kstdlib and kstrncpy. */

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
i64 read_inode(const ext2_volume_t *v, u32 i, ext2_inode_t *n)
{
  (void)v; (void)i; (void)n; return -1;
}
i64 write_inode(const ext2_volume_t *v, u32 i, const ext2_inode_t *n)
{
  (void)v; (void)i; (void)n; return -1;
}
i64 vol_read_block(const ext2_volume_t *v, u32 b, void *buf)
{
  (void)v; (void)b; (void)buf; return -1;
}
i64 vol_write_block(const ext2_volume_t *v, u32 b, const void *buf)
{
  (void)v; (void)b; (void)buf; return -1;
}
i64 flush_metadata(ext2_volume_t *v)
{
  (void)v; return -1;
}
i64 dir_find_entry(
    const ext2_volume_t *v, const ext2_inode_t *di, const char *name,
    u32 *out_ino, u8 *out_type)
{
  (void)v; (void)di; (void)name; (void)out_ino; (void)out_type;
  return -ENOENT;
}

#include "../../src/fs/ext2/path.c"

/* path_split */

static void split_absolute_root(void **state)
{
  (void)state;
  char p[256], n[256];
  path_split("/foo", p, n);
  assert_string_equal(p, "/");
  assert_string_equal(n, "foo");
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
  /* "/a/" — last_slash=2, parent="/a", name="" */
  path_split("/a/", p, n);
  assert_string_equal(p, "/a");
  assert_string_equal(n, "");
}

static void split_double_slash(void **state)
{
  (void)state;
  char p[256], n[256];
  path_split("/a/b", p, n);
  assert_string_equal(p, "/a");
  assert_string_equal(n, "b");
}

/* mode_to_file_type */

static void mode_regular(void **state)
{
  (void)state;
  assert_int_equal(mode_to_file_type(EXT2_S_IFREG), EXT2_FT_REG_FILE);
}

static void mode_directory(void **state)
{
  (void)state;
  assert_int_equal(mode_to_file_type(EXT2_S_IFDIR), EXT2_FT_DIR);
}

static void mode_symlink(void **state)
{
  (void)state;
  assert_int_equal(mode_to_file_type(EXT2_S_IFLNK), EXT2_FT_SYMLINK);
}

static void mode_chrdev(void **state)
{
  (void)state;
  assert_int_equal(mode_to_file_type(EXT2_S_IFCHR), EXT2_FT_CHRDEV);
}

static void mode_blkdev(void **state)
{
  (void)state;
  assert_int_equal(mode_to_file_type(EXT2_S_IFBLK), EXT2_FT_BLKDEV);
}

static void mode_fifo(void **state)
{
  (void)state;
  assert_int_equal(mode_to_file_type(EXT2_S_IFIFO), EXT2_FT_FIFO);
}

static void mode_sock(void **state)
{
  (void)state;
  assert_int_equal(mode_to_file_type(EXT2_S_IFSOCK), EXT2_FT_SOCK);
}

static void mode_unknown_returns_unknown(void **state)
{
  (void)state;
  assert_int_equal(mode_to_file_type(0x0000), EXT2_FT_UNKNOWN);
}

static void mode_permission_bits_stripped(void **state)
{
  (void)state;
  /* Permission bits (low 12) must not affect the result. */
  assert_int_equal(mode_to_file_type(EXT2_S_IFREG | 0755), EXT2_FT_REG_FILE);
}

/* extract_basename */

static void basename_simple(void **state)
{
  (void)state;
  char n[256];
  extract_basename("/usr/bin/ls", n);
  assert_string_equal(n, "ls");
}

static void basename_root_child(void **state)
{
  (void)state;
  char n[256];
  extract_basename("/foo", n);
  assert_string_equal(n, "foo");
}

static void basename_no_slash(void **state)
{
  (void)state;
  char n[256];
  extract_basename("file", n);
  assert_string_equal(n, "file");
}

int main(void)
{
  const struct CMUnitTest tests[] = {
      cmocka_unit_test(split_absolute_root),
      cmocka_unit_test(split_nested),
      cmocka_unit_test(split_no_slash_treats_as_relative),
      cmocka_unit_test(split_trailing_slash),
      cmocka_unit_test(split_double_slash),
      cmocka_unit_test(mode_regular),
      cmocka_unit_test(mode_directory),
      cmocka_unit_test(mode_symlink),
      cmocka_unit_test(mode_chrdev),
      cmocka_unit_test(mode_blkdev),
      cmocka_unit_test(mode_fifo),
      cmocka_unit_test(mode_sock),
      cmocka_unit_test(mode_unknown_returns_unknown),
      cmocka_unit_test(mode_permission_bits_stripped),
      cmocka_unit_test(basename_simple),
      cmocka_unit_test(basename_root_child),
      cmocka_unit_test(basename_no_slash),
  };
  return cmocka_run_group_tests(tests, NULL, NULL);
}
