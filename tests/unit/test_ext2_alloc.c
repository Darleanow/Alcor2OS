/* Unit tests for ext2/alloc.c bitmap primitives.
 * bitmap_set/clear/find_clear are pure bit-manipulation and are the most
 * critical path in the allocator — a single wrong shift corrupts the FS. */

#include "test_common.h"

#include <alcor2/types.h>

#include <string.h>

/* Include proper headers first, then provide stubs. */
#include <alcor2/fs/ext2.h>
#include <alcor2/types.h>
#include <fs/ext2/internal.h>

#include <stdlib.h>
static bool g_io_ok       = false; /* default: I/O fails */
static bool g_kmalloc_ok  = false; /* default: alloc fails */
static u8   g_bitmap_buf[1024];    /* shared bitmap buffer for tests */
void *kmalloc(u64 n) { return g_kmalloc_ok ? calloc(1, n) : NULL; }
void  kfree(void *p) { free(p); }
void *kmemcpy(void *d, const void *s, u64 n) { return memcpy(d, s, n); }
void  kzero(void *d, u64 n) { memset(d, 0, n); }
u64   kstrlen(const char *s) { u64 n=0; while(s[n]) n++; return n; }
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
/* vol_read_block / vol_write_block: controllable for alloc tests */
i64 vol_read_block(const ext2_volume_t *v, u32 b, void *buf)
{
  (void)v; (void)b;
  if(!g_io_ok) return -1;
  memcpy(buf, g_bitmap_buf, 1024);
  return 0;
}
i64 vol_write_block(const ext2_volume_t *v, u32 b, const void *buf)
{
  (void)v; (void)b;
  if(!g_io_ok) return -1;
  memcpy(g_bitmap_buf, buf, 1024);
  return 0;
}
i64 flush_metadata(ext2_volume_t *v)
{
  (void)v; return -1;
}

#include "../../src/fs/ext2/alloc.c"

/* bitmap_set */

static void set_bit_0(void **state)
{
  (void)state;
  u8 bm[4] = {0};
  bitmap_set(bm, 0);
  assert_int_equal(bm[0], 0x01);
}

static void set_bit_7(void **state)
{
  (void)state;
  u8 bm[4] = {0};
  bitmap_set(bm, 7);
  assert_int_equal(bm[0], 0x80);
}

static void set_bit_8_lands_in_second_byte(void **state)
{
  (void)state;
  u8 bm[4] = {0};
  bitmap_set(bm, 8);
  assert_int_equal(bm[0], 0x00);
  assert_int_equal(bm[1], 0x01);
}

static void set_is_idempotent(void **state)
{
  (void)state;
  u8 bm[4] = {0};
  bitmap_set(bm, 3);
  bitmap_set(bm, 3);
  assert_int_equal(bm[0], 0x08);
}

/* bitmap_clear */

static void clear_bit_clears_only_target(void **state)
{
  (void)state;
  u8 bm[4] = {0xFF, 0xFF, 0, 0};
  bitmap_clear(bm, 5);
  assert_int_equal(bm[0], 0xFF & ~(1u << 5));
  assert_int_equal(bm[1], 0xFF);
}

static void clear_bit_in_second_byte(void **state)
{
  (void)state;
  u8 bm[4] = {0xFF, 0xFF, 0, 0};
  bitmap_clear(bm, 9);
  assert_int_equal(bm[1], 0xFF & ~(1u << 1));
  assert_int_equal(bm[0], 0xFF);
}

static void set_then_clear_yields_zero(void **state)
{
  (void)state;
  u8 bm[4] = {0};
  bitmap_set(bm, 4);
  bitmap_clear(bm, 4);
  assert_int_equal(bm[0], 0);
}

/* bitmap_find_clear */

static void find_clear_in_all_zero_bitmap(void **state)
{
  (void)state;
  u8 bm[4] = {0};
  assert_int_equal(bitmap_find_clear(bm, 32), 0);
}

static void find_clear_skips_set_bits(void **state)
{
  (void)state;
  u8 bm[4] = {0xFF, 0x00, 0, 0}; /* first 8 bits set, bit 8 free */
  assert_int_equal(bitmap_find_clear(bm, 16), 8);
}

static void find_clear_returns_minus1_when_full(void **state)
{
  (void)state;
  u8 bm[4] = {0xFF, 0xFF, 0xFF, 0xFF};
  assert_int_equal(bitmap_find_clear(bm, 32), (u32)-1);
}

static void find_clear_respects_size_boundary(void **state)
{
  (void)state;
  /* Only 4 bits valid; bits 4-7 are set but outside size → not returned. */
  u8 bm[1] = {0x0F}; /* bits 0-3 set, 4-7 set but out-of-size */
  assert_int_equal(bitmap_find_clear(bm, 4), (u32)-1);
}

static void find_clear_after_partial_byte(void **state)
{
  (void)state;
  u8 bm[2] = {0xFE, 0}; /* bit 0 free, rest of byte set */
  assert_int_equal(bitmap_find_clear(bm, 16), 0);
}

static void find_clear_middle_of_byte(void **state)
{
  (void)state;
  u8 bm[1] = {0b11110111}; /* bit 3 free */
  assert_int_equal(bitmap_find_clear(bm, 8), 3);
}

/* Helper: build a minimal ext2_volume_t with one group */
static void build_vol(ext2_volume_t *vol, u32 block_size)
{
  memset(vol, 0, sizeof(*vol));
  vol->block_size         = block_size;
  vol->blocks_per_group   = block_size * 8; /* one bitmap byte per block */
  vol->groups_count       = 1;
  vol->first_data_block   = 1;
  vol->blocks_count       = vol->first_data_block + vol->blocks_per_group;
  vol->inodes_per_group   = block_size * 8;
  vol->inodes_count       = vol->inodes_per_group; /* one group of inodes */
  vol->sb.s_free_blocks_count = vol->blocks_per_group;
  vol->sb.s_free_inodes_count = vol->inodes_per_group;
  /* Set up single group descriptor */
  static ext2_group_desc_t gd;
  memset(&gd, 0, sizeof(gd));
  gd.bg_block_bitmap      = 2;
  gd.bg_inode_bitmap      = 3;
  gd.bg_free_blocks_count = vol->blocks_per_group;
  gd.bg_free_inodes_count = vol->inodes_per_group;
  vol->groups = &gd;
}

/* alloc_block_in_group: group out of range returns 0 */
static void alloc_block_in_group_bad_group(void **state)
{
  (void)state;
  ext2_volume_t vol;
  build_vol(&vol, 1024);
  /* group=1 >= groups_count=1 */
  assert_int_equal(alloc_block_in_group(&vol, 1), 0);
}

/* alloc_block_in_group: no free blocks returns 0 */
static void alloc_block_in_group_no_free(void **state)
{
  (void)state;
  ext2_volume_t vol;
  build_vol(&vol, 1024);
  vol.groups[0].bg_free_blocks_count = 0;
  assert_int_equal(alloc_block_in_group(&vol, 0), 0);
}

/* alloc_block_in_group: kmalloc failure returns 0 */
static void alloc_block_kmalloc_fail(void **state)
{
  (void)state;
  ext2_volume_t vol;
  build_vol(&vol, 1024);
  g_kmalloc_ok = false;
  assert_int_equal(alloc_block_in_group(&vol, 0), 0);
}

/* alloc_block_in_group: I/O failure returns 0 */
static void alloc_block_io_fail(void **state)
{
  (void)state;
  ext2_volume_t vol;
  build_vol(&vol, 1024);
  g_kmalloc_ok = true;
  g_io_ok      = false;
  assert_int_equal(alloc_block_in_group(&vol, 0), 0);
}

/* alloc_block_in_group: success allocates block 1 (first data block) */
static void alloc_block_in_group_success(void **state)
{
  (void)state;
  ext2_volume_t vol;
  build_vol(&vol, 1024);
  g_kmalloc_ok = true;
  g_io_ok      = true;
  memset(g_bitmap_buf, 0, sizeof(g_bitmap_buf)); /* all blocks free */

  u32 block = alloc_block_in_group(&vol, 0);
  assert_true(block > 0);
  /* group 0, bit 0: first_data_block + 0 */
  assert_int_equal(block, vol.first_data_block);
  /* free count decremented */
  assert_int_equal(vol.groups[0].bg_free_blocks_count,
                   vol.blocks_per_group - 1);
}

/* alloc_block: prefers preferred group, falls back on failure */
static void alloc_block_fallback(void **state)
{
  (void)state;
  ext2_volume_t vol;
  build_vol(&vol, 1024);
  /* Two groups; preferred_group=1 is out of range so falls back to group 0 */
  g_kmalloc_ok = true;
  g_io_ok      = true;
  memset(g_bitmap_buf, 0, sizeof(g_bitmap_buf));
  u32 block = alloc_block(&vol, 1); /* preferred=1, falls back to g=0 */
  assert_true(block > 0);
}

/* free_block: out-of-range block returns -EINVAL */
static void free_block_out_of_range(void **state)
{
  (void)state;
  ext2_volume_t vol;
  build_vol(&vol, 1024);
  /* block 0 is below first_data_block=1 */
  assert_int_equal((i64)free_block(&vol, 0), -EINVAL);
  /* block at blocks_count is also out of range */
  assert_int_equal((i64)free_block(&vol, vol.blocks_count), -EINVAL);
}

/* free_block: kmalloc failure returns -ENOMEM */
static void free_block_kmalloc_fail(void **state)
{
  (void)state;
  ext2_volume_t vol;
  build_vol(&vol, 1024);
  g_kmalloc_ok = false;
  assert_int_equal((i64)free_block(&vol, vol.first_data_block), -ENOMEM);
}

/* free_block: read I/O failure returns -EIO */
static void free_block_read_io_fail(void **state)
{
  (void)state;
  ext2_volume_t vol;
  build_vol(&vol, 1024);
  g_kmalloc_ok = true;
  g_io_ok      = false;
  assert_int_equal((i64)free_block(&vol, vol.first_data_block), -EIO);
}

/* free_block: success increments free count */
static void free_block_success(void **state)
{
  (void)state;
  ext2_volume_t vol;
  build_vol(&vol, 1024);
  g_kmalloc_ok = true;
  g_io_ok      = true;
  memset(g_bitmap_buf, 0xFF, sizeof(g_bitmap_buf)); /* all used */
  vol.groups[0].bg_free_blocks_count = 0;
  vol.sb.s_free_blocks_count = 0;

  assert_int_equal(free_block(&vol, vol.first_data_block), 0);
  assert_int_equal(vol.groups[0].bg_free_blocks_count, 1);
  assert_int_equal(vol.sb.s_free_blocks_count, 1);
}

/* alloc_inode: success */
static void alloc_inode_success(void **state)
{
  (void)state;
  ext2_volume_t vol;
  build_vol(&vol, 1024);
  g_kmalloc_ok = true;
  g_io_ok      = true;
  memset(g_bitmap_buf, 0, sizeof(g_bitmap_buf));

  u32 ino = alloc_inode(&vol, 0, false);
  assert_true(ino > 0);
}

/* alloc_inode: is_dir bumps dir count */
static void alloc_inode_dir_bumps_dir_count(void **state)
{
  (void)state;
  ext2_volume_t vol;
  build_vol(&vol, 1024);
  g_kmalloc_ok = true;
  g_io_ok      = true;
  memset(g_bitmap_buf, 0, sizeof(g_bitmap_buf));
  u32 before = vol.groups[0].bg_used_dirs_count;
  u32 ino    = alloc_inode(&vol, 0, true);
  assert_true(ino > 0);
  assert_int_equal(vol.groups[0].bg_used_dirs_count, before + 1);
}

/* free_inode: out-of-range returns -EINVAL */
static void free_inode_zero_einval(void **state)
{
  (void)state;
  ext2_volume_t vol;
  build_vol(&vol, 1024);
  assert_int_equal((i64)free_inode(&vol, 0, false), -EINVAL);
}

/* free_inode: success decrements used_dirs for dir */
static void free_inode_dir_decrements_dir_count(void **state)
{
  (void)state;
  ext2_volume_t vol;
  build_vol(&vol, 1024);
  g_kmalloc_ok = true;
  g_io_ok      = true;
  memset(g_bitmap_buf, 0xFF, sizeof(g_bitmap_buf));
  vol.groups[0].bg_used_dirs_count = 2;
  vol.sb.s_free_inodes_count       = 0;
  vol.groups[0].bg_free_inodes_count = 0;
  /* inode 1 is in group 0 */
  assert_int_equal(free_inode(&vol, 1, true), 0);
  assert_int_equal(vol.groups[0].bg_used_dirs_count, 1);
}

int main(void)
{
  const struct CMUnitTest tests[] = {
      cmocka_unit_test(set_bit_0),
      cmocka_unit_test(set_bit_7),
      cmocka_unit_test(set_bit_8_lands_in_second_byte),
      cmocka_unit_test(set_is_idempotent),
      cmocka_unit_test(clear_bit_clears_only_target),
      cmocka_unit_test(clear_bit_in_second_byte),
      cmocka_unit_test(set_then_clear_yields_zero),
      cmocka_unit_test(find_clear_in_all_zero_bitmap),
      cmocka_unit_test(find_clear_skips_set_bits),
      cmocka_unit_test(find_clear_returns_minus1_when_full),
      cmocka_unit_test(find_clear_respects_size_boundary),
      cmocka_unit_test(find_clear_after_partial_byte),
      cmocka_unit_test(find_clear_middle_of_byte),
      /* alloc_block / free_block */
      cmocka_unit_test(alloc_block_in_group_bad_group),
      cmocka_unit_test(alloc_block_in_group_no_free),
      cmocka_unit_test(alloc_block_kmalloc_fail),
      cmocka_unit_test(alloc_block_io_fail),
      cmocka_unit_test(alloc_block_in_group_success),
      cmocka_unit_test(alloc_block_fallback),
      cmocka_unit_test(free_block_out_of_range),
      cmocka_unit_test(free_block_kmalloc_fail),
      cmocka_unit_test(free_block_read_io_fail),
      cmocka_unit_test(free_block_success),
      /* alloc_inode / free_inode */
      cmocka_unit_test(alloc_inode_success),
      cmocka_unit_test(alloc_inode_dir_bumps_dir_count),
      cmocka_unit_test(free_inode_zero_einval),
      cmocka_unit_test(free_inode_dir_decrements_dir_count),
  };
  return cmocka_run_group_tests(tests, NULL, NULL);
}
