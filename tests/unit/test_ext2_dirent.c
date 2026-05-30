/* Unit tests for ext2/dir_entry.c pure logic: dirent_aligned_len and the
 * in-memory block scanner dir_block_find_name. */

#include "test_common.h"

#include <alcor2/fs/ext2.h>
#include <alcor2/kstdlib.h>
#include <alcor2/types.h>
#include <fs/ext2/internal.h>

#include <string.h>

void *kmalloc(u64 n)
{
  (void)n;
  return NULL;
}
void kfree(void *p)
{
  (void)p;
}
void *kmemcpy(void *d, const void *s, u64 n)
{
  return memcpy(d, s, n);
}
void kzero(void *d, u64 n)
{
  memset(d, 0, n);
}
int kstrncmp(const char *a, const char *b, u64 n)
{
  return strncmp(a, b, n);
}
u64 kstrlen(const char *s)
{
  return strlen(s);
}
char *kstrncpy(char *d, const char *s, u64 m)
{
  if(m == 0)
    return d;
  u64 i;
  for(i = 0; i < m - 1 && s[i]; i++)
    d[i] = s[i];
  d[i] = '\0';
  return d;
}
u8 *cache_get_block(u32 s)
{
  (void)s;
  return NULL;
}
void cache_put_block(u8 *p)
{
  (void)p;
}
i64 ata_read(u8 d, u64 l, u32 c, void *b)
{
  (void)d;
  (void)l;
  (void)c;
  (void)b;
  return -1;
}
i64 ata_write(u8 d, u64 l, u32 c, const void *b)
{
  (void)d;
  (void)l;
  (void)c;
  (void)b;
  return -1;
}
i64 read_inode(const ext2_volume_t *v, u32 i, ext2_inode_t *n)
{
  (void)v;
  (void)i;
  (void)n;
  return -1;
}
i64 write_inode(const ext2_volume_t *v, u32 i, const ext2_inode_t *n)
{
  (void)v;
  (void)i;
  (void)n;
  return -1;
}
i64 vol_read_block(const ext2_volume_t *v, u32 b, void *buf)
{
  (void)v;
  (void)b;
  (void)buf;
  return -1;
}
i64 vol_write_block(const ext2_volume_t *v, u32 b, const void *buf)
{
  (void)v;
  (void)b;
  (void)buf;
  return -1;
}
u32 get_block_num(const ext2_volume_t *v, const ext2_inode_t *inode, u32 fb)
{
  (void)v;
  (void)inode;
  (void)fb;
  return 0;
}
u32 alloc_file_block(ext2_volume_t *v, ext2_inode_t *inode, u32 fb, u32 grp)
{
  (void)v;
  (void)inode;
  (void)fb;
  (void)grp;
  return 0;
}
i64 flush_metadata(ext2_volume_t *v)
{
  (void)v;
  return -1;
}

#include "../../src/fs/ext2/dir_entry.c"

/* dirent_aligned_len */

static void aligned_len_zero_name(void **state)
{
  (void)state;
  /* 0-char name: sizeof(ext2_dirent_t) rounded up to 4. */
  u32 base     = (u32)sizeof(ext2_dirent_t);
  u32 expected = (base + EXT2_DIRENT_ALIGN - 1) & ~(EXT2_DIRENT_ALIGN - 1);
  assert_int_equal(dirent_aligned_len(0), expected);
}

static void aligned_len_one_char(void **state)
{
  (void)state;
  u32 raw      = (u32)sizeof(ext2_dirent_t) + 1;
  u32 expected = (raw + EXT2_DIRENT_ALIGN - 1) & ~(EXT2_DIRENT_ALIGN - 1);
  assert_int_equal(dirent_aligned_len(1), expected);
}

static void aligned_len_already_aligned(void **state)
{
  (void)state;
  /* Choose a name_len that makes the total already 4-aligned. */
  u32 base = (u32)sizeof(ext2_dirent_t);
  /* Find name_len such that base + name_len % 4 == 0. */
  u32 pad =
      (EXT2_DIRENT_ALIGN - (base % EXT2_DIRENT_ALIGN)) % EXT2_DIRENT_ALIGN;
  u32 n = pad; /* total = base + pad → aligned */
  assert_int_equal(dirent_aligned_len(n) % EXT2_DIRENT_ALIGN, 0);
}

static void aligned_len_always_multiple_of_4(void **state)
{
  (void)state;
  for(u32 n = 0; n < 16; n++)
    assert_int_equal(dirent_aligned_len(n) % EXT2_DIRENT_ALIGN, 0);
}

/* dir_block_find_name — build a minimal in-memory dirent block. */

static void build_dirent(u8 *buf, u32 *off, u32 ino, const char *name)
{
  u8            nlen = (u8)strlen(name);
  u32           rec  = dirent_aligned_len(nlen);
  ext2_dirent_t de;
  memset(&de, 0, sizeof(de));
  de.inode    = ino;
  de.name_len = nlen;
  de.rec_len  = (u16)rec;
  memcpy(buf + *off, &de, sizeof(de));
  memcpy(buf + *off + sizeof(de), name, nlen);
  *off += rec;
}

static void find_name_hit(void **state)
{
  (void)state;
  u8  block[512];
  u32 off = 0;
  memset(block, 0, sizeof(block));
  build_dirent(block, &off, 10, "foo");
  build_dirent(block, &off, 20, "bar");

  ext2_dirent_t *de = dir_block_find_name(block, sizeof(block), "bar", 3);
  assert_non_null(de);
  assert_int_equal(de->inode, 20);
}

static void find_name_miss(void **state)
{
  (void)state;
  u8  block[512];
  u32 off = 0;
  memset(block, 0, sizeof(block));
  build_dirent(block, &off, 10, "foo");

  assert_null(dir_block_find_name(block, sizeof(block), "baz", 3));
}

static void find_name_skips_deleted_entry(void **state)
{
  (void)state;
  /* An entry with inode=0 is a tombstone and must be skipped. */
  u8  block[512];
  u32 off = 0;
  memset(block, 0, sizeof(block));
  build_dirent(block, &off, 0, "foo"); /* deleted */
  build_dirent(block, &off, 5, "foo"); /* live */

  ext2_dirent_t *de = dir_block_find_name(block, sizeof(block), "foo", 3);
  assert_non_null(de);
  assert_int_equal(de->inode, 5);
}

static void find_name_stops_on_zero_rec_len(void **state)
{
  (void)state;
  u8 block[512];
  memset(block, 0, sizeof(block));
  /* rec_len=0 at start → immediate stop, no entry found. */
  assert_null(dir_block_find_name(block, sizeof(block), "foo", 3));
}

static void find_name_partial_match_not_returned(void **state)
{
  (void)state;
  u8  block[512];
  u32 off = 0;
  memset(block, 0, sizeof(block));
  build_dirent(block, &off, 10, "foobar");

  /* "foo" is a prefix but not a match. */
  assert_null(dir_block_find_name(block, sizeof(block), "foo", 3));
}

/* dir_block_remove_name */

static void remove_first_entry_tombstones(void **state)
{
  (void)state;
  u8  block[512];
  u32 off = 0;
  memset(block, 0, sizeof(block));
  build_dirent(block, &off, 10, "foo");

  assert_true(dir_block_remove_name(block, sizeof(block), "foo", 3));
  ext2_dirent_t *de = (ext2_dirent_t *)block;
  assert_int_equal(de->inode, 0); /* tombstoned, not spliced */
}

static void remove_middle_entry_merges_into_prev(void **state)
{
  (void)state;
  u8  block[512];
  u32 off = 0;
  memset(block, 0, sizeof(block));
  build_dirent(block, &off, 10, "a");
  build_dirent(block, &off, 20, "b");
  build_dirent(block, &off, 30, "c");

  u32 prev_rec = ((ext2_dirent_t *)block)->rec_len;
  u32 mid_rec  = ((ext2_dirent_t *)(block + prev_rec))->rec_len;

  assert_true(dir_block_remove_name(block, sizeof(block), "b", 1));

  /* "a"'s rec_len must absorb "b"'s space */
  assert_int_equal(((ext2_dirent_t *)block)->rec_len, prev_rec + mid_rec);
  /* "c" still findable */
  assert_non_null(dir_block_find_name(block, sizeof(block), "c", 1));
}

static void remove_nonexistent_returns_false(void **state)
{
  (void)state;
  u8  block[512];
  u32 off = 0;
  memset(block, 0, sizeof(block));
  build_dirent(block, &off, 10, "foo");
  assert_false(dir_block_remove_name(block, sizeof(block), "bar", 3));
}

/* dir_block_count_entries */

static void count_entries_excludes_dot_and_dotdot(void **state)
{
  (void)state;
  u8  block[512];
  u32 off = 0;
  memset(block, 0, sizeof(block));
  build_dirent(block, &off, 1, ".");
  build_dirent(block, &off, 1, "..");
  build_dirent(block, &off, 5, "real");
  assert_int_equal(dir_block_count_entries(block, sizeof(block)), 1);
}

static void count_entries_empty_block_is_zero(void **state)
{
  (void)state;
  u8 block[512];
  memset(block, 0, sizeof(block));
  assert_int_equal(dir_block_count_entries(block, sizeof(block)), 0);
}

static void count_entries_skips_tombstones(void **state)
{
  (void)state;
  u8  block[512];
  u32 off = 0;
  memset(block, 0, sizeof(block));
  build_dirent(block, &off, 0, "gone"); /* inode=0 = deleted */
  build_dirent(block, &off, 5, "live");
  assert_int_equal(dir_block_count_entries(block, sizeof(block)), 1);
}

/* dir_block_try_insert */

static void try_insert_into_slack(void **state)
{
  (void)state;
  u8 block[512];
  memset(block, 0, sizeof(block));

  /* Seed one entry that spans the whole block (rec_len = block_size).
   * This gives maximum slack. */
  ext2_dirent_t *seed = (ext2_dirent_t *)block;
  seed->inode         = 99;
  seed->rec_len       = (u16)sizeof(block);
  seed->name_len      = 1;
  seed->name[0]       = 'x';

  bool ok = dir_block_try_insert(
      block, sizeof(block), "new", 3, 42, EXT2_FT_REG_FILE
  );
  assert_true(ok);

  /* The new entry must now be findable. */
  ext2_dirent_t *found = dir_block_find_name(block, sizeof(block), "new", 3);
  assert_non_null(found);
  assert_int_equal(found->inode, 42);
}

static void try_insert_fails_when_no_slack(void **state)
{
  (void)state;
  u8 block[32]; /* tiny block */
  memset(block, 0, sizeof(block));

  /* One entry that exactly fills the block with no slack. */
  ext2_dirent_t *seed = (ext2_dirent_t *)block;
  seed->inode         = 1;
  seed->rec_len       = (u16)sizeof(block);
  seed->name_len      = (u8)(sizeof(block) - sizeof(ext2_dirent_t));
  memset(seed->name, 'z', seed->name_len);

  /* Another entry won't fit. */
  assert_false(
      dir_block_try_insert(block, sizeof(block), "a", 1, 2, EXT2_FT_REG_FILE)
  );
}

int main(void)
{
  const struct CMUnitTest tests[] = {
      cmocka_unit_test(aligned_len_zero_name),
      cmocka_unit_test(aligned_len_one_char),
      cmocka_unit_test(aligned_len_already_aligned),
      cmocka_unit_test(aligned_len_always_multiple_of_4),
      cmocka_unit_test(find_name_hit),
      cmocka_unit_test(find_name_miss),
      cmocka_unit_test(find_name_skips_deleted_entry),
      cmocka_unit_test(find_name_stops_on_zero_rec_len),
      cmocka_unit_test(find_name_partial_match_not_returned),
      cmocka_unit_test(remove_first_entry_tombstones),
      cmocka_unit_test(remove_middle_entry_merges_into_prev),
      cmocka_unit_test(remove_nonexistent_returns_false),
      cmocka_unit_test(count_entries_excludes_dot_and_dotdot),
      cmocka_unit_test(count_entries_empty_block_is_zero),
      cmocka_unit_test(count_entries_skips_tombstones),
      cmocka_unit_test(try_insert_into_slack),
      cmocka_unit_test(try_insert_fails_when_no_slack),
  };
  return cmocka_run_group_tests(tests, NULL, NULL);
}
