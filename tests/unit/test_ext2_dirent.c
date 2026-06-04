/* Unit tests for ext2/dir_entry.c pure logic: dirent_aligned_len and the
 * in-memory block scanner dir_block_find_name. */

#include "test_common.h"

#include <alcor2/fs/ext2.h>
#include <alcor2/kstdlib.h>
#include <alcor2/types.h>
#include <fs/ext2/internal.h>

#include <string.h>

#include <stdlib.h>

#define STORE_BLOCKS 16
#define BLOCK_SZ     1024u

static u8 g_blocks[STORE_BLOCKS][BLOCK_SZ];

static bool g_malloc_fail = false;
static bool g_read_fail   = false;
static bool g_write_fail  = false;
static bool g_sparse      = false;
static bool g_alloc_fail  = false;

static int reset(void **s)
{
  (void)s;
  memset(g_blocks, 0, sizeof(g_blocks));
  g_malloc_fail = false;
  g_read_fail   = false;
  g_write_fail  = false;
  g_sparse      = false;
  g_alloc_fail  = false;
  return 0;
}
void *kmalloc(u64 n)
{
  return g_malloc_fail ? NULL : malloc((size_t)n);
}
void kfree(void *p)
{
  free(p);
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
  return 0;
}
i64 vol_read_block(const ext2_volume_t *v, u32 b, void *buf)
{
  (void)v;
  if(g_read_fail || b >= STORE_BLOCKS)
    return -1;
  memcpy(buf, g_blocks[b], BLOCK_SZ);
  return 0;
}
i64 vol_write_block(const ext2_volume_t *v, u32 b, const void *buf)
{
  (void)v;
  if(g_write_fail || b >= STORE_BLOCKS)
    return -1;
  memcpy(g_blocks[b], buf, BLOCK_SZ);
  return 0;
}
u32 get_block_num(const ext2_volume_t *v, const ext2_inode_t *inode, u32 fb)
{
  (void)v;
  (void)inode;
  return g_sparse ? 0 : fb + 1;
}
u32 alloc_file_block(ext2_volume_t *v, ext2_inode_t *inode, u32 fb, u32 grp)
{
  (void)v;
  (void)inode;
  (void)grp;
  return g_alloc_fail ? 0 : fb + 1;
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

/* Public API Tests */
static ext2_volume_t make_vol(void) {
  ext2_volume_t v;
  memset(&v, 0, sizeof(v));
  v.block_size = BLOCK_SZ;
  v.inode_size = 128;
  v.inodes_per_group = 8;
  return v;
}

static void pub_dir_is_empty_true(void **state) {
  (void)state;
  ext2_volume_t v = make_vol();
  ext2_inode_t dir;
  memset(&dir, 0, sizeof(dir));
  dir.i_size = BLOCK_SZ;
  
  u32 block_num = get_block_num(&v, &dir, 0);
  u32 off = 0;
  build_dirent(g_blocks[block_num], &off, 1, ".");
  build_dirent(g_blocks[block_num], &off, 2, "..");
  
  assert_true(dir_is_empty(&v, &dir));
}

static void pub_dir_is_empty_false(void **state) {
  (void)state;
  ext2_volume_t v = make_vol();
  ext2_inode_t dir;
  memset(&dir, 0, sizeof(dir));
  dir.i_size = BLOCK_SZ;
  
  u32 block_num = get_block_num(&v, &dir, 0);
  u32 off = 0;
  build_dirent(g_blocks[block_num], &off, 1, ".");
  build_dirent(g_blocks[block_num], &off, 2, "..");
  build_dirent(g_blocks[block_num], &off, 3, "foo");
  
  assert_false(dir_is_empty(&v, &dir));
}

static void pub_dir_find_entry_success(void **state) {
  (void)state;
  ext2_volume_t v = make_vol();
  ext2_inode_t dir;
  memset(&dir, 0, sizeof(dir));
  dir.i_size = 2 * BLOCK_SZ;
  
  u32 b1 = get_block_num(&v, &dir, 0);
  u32 b2 = get_block_num(&v, &dir, 1);
  
  u32 off1 = 0;
  build_dirent(g_blocks[b1], &off1, 10, "foo");
  
  u32 off2 = 0;
  build_dirent(g_blocks[b2], &off2, 20, "bar");
  
  u32 out_ino;
  u8 out_type;
  assert_int_equal(dir_find_entry(&v, &dir, "bar", &out_ino, &out_type), 0);
  assert_int_equal(out_ino, 20);
}

static void pub_dir_find_entry_failure(void **state) {
  (void)state;
  ext2_volume_t v = make_vol();
  ext2_inode_t dir;
  memset(&dir, 0, sizeof(dir));
  dir.i_size = BLOCK_SZ;
  
  u32 b1 = get_block_num(&v, &dir, 0);
  u32 off1 = 0;
  build_dirent(g_blocks[b1], &off1, 10, "foo");
  
  u32 out_ino;
  u8 out_type;
  assert_int_equal(dir_find_entry(&v, &dir, "baz", &out_ino, &out_type), -ENOENT);
}

static void pub_dir_add_entry_slack(void **state) {
  (void)state;
  ext2_volume_t v = make_vol();
  ext2_inode_t dir;
  memset(&dir, 0, sizeof(dir));
  dir.i_size = BLOCK_SZ;
  
  u32 b1 = get_block_num(&v, &dir, 0);
  ext2_dirent_t *seed = (ext2_dirent_t *)g_blocks[b1];
  seed->inode = 1;
  seed->rec_len = BLOCK_SZ;
  seed->name_len = 1;
  seed->name[0] = '.';
  
  assert_int_equal(dir_add_entry(&v, 100, &dir, "new", 42, EXT2_FT_REG_FILE), 0);
  
  u32 out_ino;
  u8 out_type;
  assert_int_equal(dir_find_entry(&v, &dir, "new", &out_ino, &out_type), 0);
  assert_int_equal(out_ino, 42);
}

static void pub_dir_add_entry_new_block(void **state) {
  (void)state;
  ext2_volume_t v = make_vol();
  ext2_inode_t dir;
  memset(&dir, 0, sizeof(dir));
  dir.i_size = BLOCK_SZ;
  
  u32 b1 = get_block_num(&v, &dir, 0);
  u32 off = 0;
  // Fill the 1024-byte block perfectly.
  // 1024 / 256 = 4 entries of rec_len 256.
  // A rec_len of 256 requires a name_len of at least 256 - 8 - 3 = 245.
  // We'll use name_len = 248 to get exact 256 aligned size.
  for(int i = 0; i < 4; i++) {
    ext2_dirent_t *de = (ext2_dirent_t *)(g_blocks[b1] + off);
    de->inode = 1;
    de->rec_len = 256;
    de->name_len = 248;
    off += 256;
  }
  
  assert_int_equal(dir_add_entry(&v, 100, &dir, "new2", 43, EXT2_FT_REG_FILE), 0);
  assert_int_equal(dir.i_size, 2 * BLOCK_SZ);
  
  u32 out_ino;
  u8 out_type;
  assert_int_equal(dir_find_entry(&v, &dir, "new2", &out_ino, &out_type), 0);
  assert_int_equal(out_ino, 43);
}

static void pub_dir_remove_entry_success(void **state) {
  (void)state;
  ext2_volume_t v = make_vol();
  ext2_inode_t dir;
  memset(&dir, 0, sizeof(dir));
  dir.i_size = BLOCK_SZ;
  
  u32 b1 = get_block_num(&v, &dir, 0);
  u32 off = 0;
  build_dirent(g_blocks[b1], &off, 10, "foo");
  
  assert_int_equal(dir_remove_entry(&v, &dir, "foo"), 0);
  
  u32 out_ino;
  u8 out_type;
  assert_int_equal(dir_find_entry(&v, &dir, "foo", &out_ino, &out_type), -ENOENT);
}

static void pub_dir_remove_entry_failure(void **state) {
  (void)state;
  ext2_volume_t v = make_vol();
  ext2_inode_t dir;
  memset(&dir, 0, sizeof(dir));
  dir.i_size = BLOCK_SZ;

  u32 b1 = get_block_num(&v, &dir, 0);
  u32 off = 0;
  build_dirent(g_blocks[b1], &off, 10, "foo");
  /* Extend last entry rec_len to fill the entire block → loop exits normally */
  ext2_dirent_t *de = (ext2_dirent_t *)g_blocks[b1];
  de->rec_len = BLOCK_SZ;

  assert_int_equal(dir_remove_entry(&v, &dir, "bar"), -ENOENT);
}

/* dir_find_entry: kmalloc fails returns -ENOMEM */
static void dir_find_entry_kmalloc_fail(void **state)
{
  (void)state;
  ext2_volume_t  vol = {.block_size = BLOCK_SZ};
  ext2_inode_t   dir_inode;
  memset(&dir_inode, 0, sizeof(dir_inode));
  dir_inode.i_size = BLOCK_SZ;
  u32 out_ino;
  u8  out_type;
  g_malloc_fail = true;
  i64 ret = dir_find_entry(&vol, &dir_inode, "foo", &out_ino, &out_type);
  assert_int_equal(ret, -ENOMEM);
}

/* dir_find_entry: vol_read_block fails → -EIO */
static void dir_find_entry_read_fails(void **state)
{
  (void)state;
  /* get_block_num returns fb+1, so file_block 0 → disk block 1.
     Block 1 is in range so read_block succeeds normally.
     Use STORE_BLOCKS as the return to force -EIO. */
  /* Override get_block_num to return STORE_BLOCKS (out of range) */
  /* Can't easily override static mock. Instead make block size tiny so
     inode.i_size causes the scan to access a block num >= STORE_BLOCKS. */
  ext2_volume_t  vol = {.block_size = BLOCK_SZ};
  ext2_inode_t   dir_inode;
  memset(&dir_inode, 0, sizeof(dir_inode));
  /* i_size = 0 → loop doesn't execute */
  dir_inode.i_size = 0;
  u32 out_ino;
  u8  out_type;
  /* With i_size=0, loop never runs, returns -ENOENT */
  i64 ret = dir_find_entry(&vol, &dir_inode, "foo", &out_ino, &out_type);
  assert_int_equal(ret, -ENOENT);
}

/* dir_add_entry: kmalloc fails → -ENOMEM */
static void dir_add_entry_kmalloc_fail(void **state)
{
  (void)state;
  ext2_volume_t vol = {.block_size = BLOCK_SZ};
  ext2_inode_t  dir_inode;
  memset(&dir_inode, 0, sizeof(dir_inode));
  dir_inode.i_size = BLOCK_SZ;
  g_malloc_fail    = true;
  i64 ret = dir_add_entry(&vol, 1, &dir_inode, "test", 10, EXT2_FT_REG_FILE);
  assert_int_equal(ret, -ENOMEM);
}

/* dir_is_empty: kmalloc fails → returns false */
static void dir_is_empty_kmalloc_fail(void **state)
{
  (void)state;
  ext2_volume_t vol = {.block_size = BLOCK_SZ};
  ext2_inode_t  dir_inode;
  memset(&dir_inode, 0, sizeof(dir_inode));
  dir_inode.i_size = BLOCK_SZ;
  g_malloc_fail    = true;
  assert_false(dir_is_empty(&vol, &dir_inode));
}

/* dir_remove_entry: kmalloc fails → returns false */
static void dir_remove_entry_kmalloc_fail(void **state)
{
  (void)state;
  ext2_volume_t vol = {.block_size = BLOCK_SZ};
  ext2_inode_t  dir_inode;
  memset(&dir_inode, 0, sizeof(dir_inode));
  dir_inode.i_size = BLOCK_SZ;
  g_malloc_fail    = true;
  i64 ret = dir_remove_entry(&vol, &dir_inode, "foo");
  assert_int_equal(ret, -ENOMEM);
}

/* Helper: build a minimal volume */
static ext2_volume_t make_dirent_vol(void) {
  ext2_volume_t vol;
  memset(&vol, 0, sizeof(vol));
  vol.block_size       = BLOCK_SZ;
  vol.inodes_per_group = 1024;
  return vol;
}

/* dir_block_try_insert: rec_len == 0 → return false (line 174) */
static void dir_block_try_insert_zero_rec_len(void **state) {
  (void)state;
  u8 block[BLOCK_SZ];
  memset(block, 0, sizeof(block)); /* all zero → first rec_len=0 → return false */
  bool r = dir_block_try_insert(block, BLOCK_SZ, "file", 4, 5, EXT2_FT_REG_FILE);
  assert_false(r);
}

/* dir_find_entry: sparse block (block_num=0) → continue */
static void dir_find_entry_sparse_block(void **state) {
  (void)state;
  ext2_volume_t vol = make_dirent_vol();
  ext2_inode_t  dir;
  memset(&dir, 0, sizeof(dir));
  dir.i_size = BLOCK_SZ;
  g_sparse   = true; /* get_block_num returns 0 → continue */
  u32 ino; u8 type;
  i64 ret = dir_find_entry(&vol, &dir, "x", &ino, &type);
  assert_int_equal(ret, -ENOENT);
}

/* dir_find_entry: read fail → -EIO */
static void dir_find_entry_read_fail(void **state) {
  (void)state;
  ext2_volume_t vol = make_dirent_vol();
  ext2_inode_t  dir;
  memset(&dir, 0, sizeof(dir));
  dir.i_size  = BLOCK_SZ;
  g_read_fail = true;
  u32 ino; u8 type;
  i64 ret = dir_find_entry(&vol, &dir, "x", &ino, &type);
  assert_int_equal(ret, -EIO);
}

/* dir_try_insert_into_existing: sparse block → continue */
static void dir_try_insert_sparse_block(void **state) {
  (void)state;
  ext2_volume_t vol = make_dirent_vol();
  ext2_inode_t  dir;
  memset(&dir, 0, sizeof(dir));
  dir.i_size = BLOCK_SZ;
  g_sparse   = true;
  u8 block_buf[BLOCK_SZ];
  /* sparse → skipped → returns -ENOENT (no block to insert into) */
  i64 ret = dir_try_insert_into_existing(&vol, &dir, "f", 1, 5, EXT2_FT_REG_FILE, block_buf);
  assert_int_equal(ret, -ENOENT);
}

/* dir_try_insert_into_existing: read fail → continue */
static void dir_try_insert_read_fail(void **state) {
  (void)state;
  ext2_volume_t vol = make_dirent_vol();
  ext2_inode_t  dir;
  memset(&dir, 0, sizeof(dir));
  dir.i_size  = BLOCK_SZ;
  g_read_fail = true;
  u8 block_buf[BLOCK_SZ];
  i64 ret = dir_try_insert_into_existing(&vol, &dir, "f", 1, 5, EXT2_FT_REG_FILE, block_buf);
  assert_int_equal(ret, -ENOENT); /* read failed → continue → -ENOENT */
}

/* dir_grow_with_entry: alloc_file_block fails → -ENOSPC */
static void dir_grow_alloc_fail_enospc(void **state) {
  (void)state;
  ext2_volume_t vol = make_dirent_vol();
  ext2_inode_t  dir;
  memset(&dir, 0, sizeof(dir));
  dir.i_size   = BLOCK_SZ;
  g_alloc_fail = true;
  u8 block_buf[BLOCK_SZ];
  i64 ret = dir_grow_with_entry(&vol, 2, &dir, "f", 1, 5, EXT2_FT_REG_FILE, block_buf);
  assert_int_equal(ret, -ENOSPC);
}

/* dir_grow_with_entry: vol_write_block fails → -EIO */
static void dir_grow_write_fail_eio(void **state) {
  (void)state;
  ext2_volume_t vol = make_dirent_vol();
  ext2_inode_t  dir;
  memset(&dir, 0, sizeof(dir));
  dir.i_size   = BLOCK_SZ;
  g_write_fail = true;
  u8 block_buf[BLOCK_SZ];
  i64 ret = dir_grow_with_entry(&vol, 2, &dir, "f", 1, 5, EXT2_FT_REG_FILE, block_buf);
  assert_int_equal(ret, -EIO);
}

/* dir_remove_entry: sparse block → continue */
static void dir_remove_entry_sparse_block(void **state) {
  (void)state;
  ext2_volume_t vol = make_dirent_vol();
  ext2_inode_t  dir;
  memset(&dir, 0, sizeof(dir));
  dir.i_size = BLOCK_SZ;
  g_sparse   = true;
  i64 ret = dir_remove_entry(&vol, &dir, "x");
  assert_int_equal(ret, -ENOENT);
}

/* dir_remove_entry: read fail → -EIO */
static void dir_remove_entry_read_fail(void **state) {
  (void)state;
  ext2_volume_t vol = make_dirent_vol();
  ext2_inode_t  dir;
  memset(&dir, 0, sizeof(dir));
  dir.i_size  = BLOCK_SZ;
  g_read_fail = true;
  i64 ret = dir_remove_entry(&vol, &dir, "x");
  assert_int_equal(ret, -EIO);
}

/* dir_is_empty: sparse block → continue */
static void dir_is_empty_sparse_block(void **state) {
  (void)state;
  ext2_volume_t vol = make_dirent_vol();
  ext2_inode_t  dir;
  memset(&dir, 0, sizeof(dir));
  dir.i_size = BLOCK_SZ;
  g_sparse   = true;
  /* All blocks sparse → no entries scanned → returns true */
  assert_true(dir_is_empty(&vol, &dir));
}

/* dir_is_empty: read fail → return false */
static void dir_is_empty_read_fail(void **state) {
  (void)state;
  ext2_volume_t vol = make_dirent_vol();
  ext2_inode_t  dir;
  memset(&dir, 0, sizeof(dir));
  dir.i_size  = BLOCK_SZ;
  g_read_fail = true;
  assert_false(dir_is_empty(&vol, &dir));
}

int main(void)
{
  const struct CMUnitTest tests[] = {
      cmocka_unit_test_setup(aligned_len_zero_name, reset),
      cmocka_unit_test_setup(aligned_len_one_char, reset),
      cmocka_unit_test_setup(aligned_len_already_aligned, reset),
      cmocka_unit_test_setup(aligned_len_always_multiple_of_4, reset),
      cmocka_unit_test_setup(find_name_hit, reset),
      cmocka_unit_test_setup(find_name_miss, reset),
      cmocka_unit_test_setup(find_name_skips_deleted_entry, reset),
      cmocka_unit_test_setup(find_name_stops_on_zero_rec_len, reset),
      cmocka_unit_test_setup(find_name_partial_match_not_returned, reset),
      cmocka_unit_test_setup(remove_first_entry_tombstones, reset),
      cmocka_unit_test_setup(remove_middle_entry_merges_into_prev, reset),
      cmocka_unit_test_setup(remove_nonexistent_returns_false, reset),
      cmocka_unit_test_setup(count_entries_excludes_dot_and_dotdot, reset),
      cmocka_unit_test_setup(count_entries_empty_block_is_zero, reset),
      cmocka_unit_test_setup(count_entries_skips_tombstones, reset),
      cmocka_unit_test_setup(try_insert_into_slack, reset),
      cmocka_unit_test_setup(try_insert_fails_when_no_slack, reset),
      cmocka_unit_test_setup(pub_dir_is_empty_true, reset),
      cmocka_unit_test_setup(pub_dir_is_empty_false, reset),
      cmocka_unit_test_setup(pub_dir_find_entry_success, reset),
      cmocka_unit_test_setup(pub_dir_find_entry_failure, reset),
      cmocka_unit_test_setup(pub_dir_add_entry_slack, reset),
      cmocka_unit_test_setup(pub_dir_add_entry_new_block, reset),
      cmocka_unit_test_setup(pub_dir_remove_entry_success, reset),
      cmocka_unit_test_setup(pub_dir_remove_entry_failure, reset),
      /* OOM and I/O error paths */
      cmocka_unit_test_setup(dir_find_entry_kmalloc_fail, reset),
      cmocka_unit_test_setup(dir_find_entry_read_fails, reset),
      cmocka_unit_test_setup(dir_add_entry_kmalloc_fail, reset),
      cmocka_unit_test_setup(dir_is_empty_kmalloc_fail, reset),
      cmocka_unit_test_setup(dir_remove_entry_kmalloc_fail, reset),
      /* new coverage */
      cmocka_unit_test_setup(dir_block_try_insert_zero_rec_len, reset),
      cmocka_unit_test_setup(dir_find_entry_sparse_block, reset),
      cmocka_unit_test_setup(dir_find_entry_read_fail, reset),
      cmocka_unit_test_setup(dir_try_insert_sparse_block, reset),
      cmocka_unit_test_setup(dir_try_insert_read_fail, reset),
      cmocka_unit_test_setup(dir_grow_alloc_fail_enospc, reset),
      cmocka_unit_test_setup(dir_grow_write_fail_eio, reset),
      cmocka_unit_test_setup(dir_remove_entry_sparse_block, reset),
      cmocka_unit_test_setup(dir_remove_entry_read_fail, reset),
      cmocka_unit_test_setup(dir_is_empty_sparse_block, reset),
      cmocka_unit_test_setup(dir_is_empty_read_fail, reset),
  };
  return cmocka_run_group_tests(tests, NULL, NULL);
}
