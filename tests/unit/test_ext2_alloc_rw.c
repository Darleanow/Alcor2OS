#include "test_common.h"
#include <alcor2/types.h>
#include <alcor2/errno.h>

#include <stdlib.h>
#include <string.h>

#include <alcor2/fs/ext2.h>
#include <fs/ext2/internal.h>

#define STORE_BLOCKS 32
#define BLOCK_SZ 1024

static u8 g_store[STORE_BLOCKS][BLOCK_SZ];

void *kmalloc(u64 n) { return malloc((size_t)n); }
void  kfree(void *p) { free(p); }
void *kmemcpy(void *d, const void *s, u64 n) { return memcpy(d, s, n); }
void  kzero(void *d, u64 n) { memset(d, 0, (size_t)n); }

u8   *cache_get_block(u32 s) { (void)s; return NULL; }
void  cache_put_block(u8 *p) { (void)p; }
i64   flush_metadata(ext2_volume_t *v) { (void)v; return 0; }
u32   alloc_file_block(ext2_volume_t *v, ext2_inode_t *inode, u32 fb, u32 grp) { (void)v;(void)inode;(void)fb;(void)grp; return 0; }
i64   write_inode(const ext2_volume_t *v, u32 i, const ext2_inode_t *n) { (void)v;(void)i;(void)n; return 0; }
u32   get_block_num(const ext2_volume_t *v, const ext2_inode_t *inode, u32 fb) { (void)v;(void)inode;(void)fb; return 0; }

i64 vol_read_block(const ext2_volume_t *v, u32 b, void *buf) {
  (void)v;
  if (b >= STORE_BLOCKS) return -EIO;
  memcpy(buf, g_store[b], BLOCK_SZ);
  return BLOCK_SZ;
}

i64 vol_write_block(const ext2_volume_t *v, u32 b, const void *buf) {
  (void)v;
  if (b >= STORE_BLOCKS) return -EIO;
  memcpy(g_store[b], buf, BLOCK_SZ);
  return BLOCK_SZ;
}

#include "../../src/fs/ext2/alloc.c"

static ext2_volume_t vol;
static ext2_group_desc_t gd[2];

static int setup_vol(void **state) {
  (void)state;
  memset(g_store, 0, sizeof(g_store));
  memset(&vol, 0, sizeof(vol));
  memset(gd, 0, sizeof(gd));

  vol.block_size = BLOCK_SZ;
  vol.blocks_per_group = 8;
  vol.inodes_per_group = 8;
  vol.inode_size = 128;
  vol.first_data_block = 1;
  vol.blocks_count = 17;
  vol.inodes_count = 16;
  vol.groups_count = 2;
  vol.groups = gd;

  vol.sb.s_free_blocks_count = 16;
  vol.sb.s_free_inodes_count = 16;

  gd[0].bg_block_bitmap = 1;
  gd[0].bg_inode_bitmap = 2;
  gd[0].bg_inode_table = 3;
  gd[0].bg_free_blocks_count = 8;
  gd[0].bg_free_inodes_count = 8;

  gd[1].bg_block_bitmap = 10;
  gd[1].bg_inode_bitmap = 11;
  gd[1].bg_inode_table = 12;
  gd[1].bg_free_blocks_count = 8;
  gd[1].bg_free_inodes_count = 8;

  return 0;
}

static void alloc_block_decrements_free_count(void **state) {
  (void)state;
  assert_int_equal(vol.groups[0].bg_free_blocks_count, 8);
  assert_int_equal(vol.sb.s_free_blocks_count, 16);
  u32 b = alloc_block(&vol, 0);
  assert_true(b > 0);
  assert_int_equal(vol.groups[0].bg_free_blocks_count, 7);
  assert_int_equal(vol.sb.s_free_blocks_count, 15);
}

static void alloc_block_exhausted_group_falls_back(void **state) {
  (void)state;
  memset(g_store[1], 0xFF, BLOCK_SZ);
  vol.groups[0].bg_free_blocks_count = 0;
  u32 b = alloc_block(&vol, 0);
  assert_true(b > 0);
  assert_true(b >= vol.first_data_block + 8);
}

static void alloc_block_preferred_group_tried_first(void **state) {
  (void)state;
  u32 b = alloc_block(&vol, 1);
  assert_true(b >= vol.first_data_block + 8);
}

static void alloc_block_returns_correct_formula(void **state) {
  (void)state;
  u32 b = alloc_block(&vol, 0);
  assert_int_equal(b, vol.first_data_block);
}

static void alloc_block_returns_nonzero_on_success(void **state) {
  (void)state;
  u32 b = alloc_block(&vol, 0);
  assert_true(b > 0);
}

static void alloc_block_returns_zero_when_full(void **state) {
  (void)state;
  memset(g_store[1], 0xFF, BLOCK_SZ);
  memset(g_store[10], 0xFF, BLOCK_SZ);
  vol.groups[0].bg_free_blocks_count = 0;
  vol.groups[1].bg_free_blocks_count = 0;
  u32 b = alloc_block(&vol, 0);
  assert_int_equal(b, 0);
}

static void alloc_inode_is_dir_increments_used_dirs(void **state) {
  (void)state;
  assert_int_equal(vol.groups[0].bg_used_dirs_count, 0);
  alloc_inode(&vol, 0, true);
  assert_int_equal(vol.groups[0].bg_used_dirs_count, 1);
}

static void alloc_inode_is_one_indexed(void **state) {
  (void)state;
  u32 ino = alloc_inode(&vol, 0, false);
  assert_int_equal(ino, 1);
}

static void alloc_then_free_then_alloc_returns_same_block(void **state) {
  (void)state;
  u32 b1 = alloc_block(&vol, 0);
  assert_true(b1 > 0);
  assert_int_equal(free_block(&vol, b1), 0);
  u32 b2 = alloc_block(&vol, 0);
  assert_int_equal(b1, b2);
}

static void alloc_two_blocks_are_different(void **state) {
  (void)state;
  u32 b1 = alloc_block(&vol, 0);
  u32 b2 = alloc_block(&vol, 0);
  assert_true(b1 > 0);
  assert_true(b2 > 0);
  assert_true(b1 != b2);
}

static void free_block_at_count_is_einval(void **state) {
  (void)state;
  assert_int_equal(free_block(&vol, vol.blocks_count), -EINVAL);
}

static void free_block_below_first_data_block_is_einval(void **state) {
  (void)state;
  assert_int_equal(free_block(&vol, vol.first_data_block - 1), -EINVAL);
}

static void free_block_increments_free_count(void **state) {
  (void)state;
  u32 b = alloc_block(&vol, 0);
  assert_true(b > 0);
  assert_int_equal(vol.groups[0].bg_free_blocks_count, 7);
  assert_int_equal(vol.sb.s_free_blocks_count, 15);
  free_block(&vol, b);
  assert_int_equal(vol.groups[0].bg_free_blocks_count, 8);
  assert_int_equal(vol.sb.s_free_blocks_count, 16);
}

static void free_inode_above_count_is_einval(void **state) {
  (void)state;
  assert_int_equal(free_inode(&vol, vol.inodes_count + 1, false), -EINVAL);
}

static void free_inode_below_one_is_einval(void **state) {
  (void)state;
  assert_int_equal(free_inode(&vol, 0, false), -EINVAL);
}

int main(void) {
  const struct CMUnitTest tests[] = {
      cmocka_unit_test_setup(alloc_block_decrements_free_count, setup_vol),
      cmocka_unit_test_setup(alloc_block_exhausted_group_falls_back, setup_vol),
      cmocka_unit_test_setup(alloc_block_preferred_group_tried_first, setup_vol),
      cmocka_unit_test_setup(alloc_block_returns_correct_formula, setup_vol),
      cmocka_unit_test_setup(alloc_block_returns_nonzero_on_success, setup_vol),
      cmocka_unit_test_setup(alloc_block_returns_zero_when_full, setup_vol),
      cmocka_unit_test_setup(alloc_inode_is_dir_increments_used_dirs, setup_vol),
      cmocka_unit_test_setup(alloc_inode_is_one_indexed, setup_vol),
      cmocka_unit_test_setup(alloc_then_free_then_alloc_returns_same_block, setup_vol),
      cmocka_unit_test_setup(alloc_two_blocks_are_different, setup_vol),
      cmocka_unit_test_setup(free_block_at_count_is_einval, setup_vol),
      cmocka_unit_test_setup(free_block_below_first_data_block_is_einval, setup_vol),
      cmocka_unit_test_setup(free_block_increments_free_count, setup_vol),
      cmocka_unit_test_setup(free_inode_above_count_is_einval, setup_vol),
      cmocka_unit_test_setup(free_inode_below_one_is_einval, setup_vol),
  };
  return cmocka_run_group_tests(tests, NULL, NULL);
}
