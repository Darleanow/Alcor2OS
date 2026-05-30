/* Adversarial tests for inode.c — targeting the read-modify-write correctness
 * of write_inode.  The critical bug class: writing inode N reads the block,
 * patches bytes [offset, offset+sizeof(inode)), then writes it back.  Any
 * arithmetic error in `offset` corrupts the neighboring inodes that share
 * the same 1 KiB block.
 *
 * Each test puts a distinct bit-pattern in every inode slot and re-reads all
 * of them after each write, so a single byte of corruption is immediately
 * visible. */

#include "test_common.h"

#include <alcor2/errno.h>
#include <alcor2/fs/ext2.h>
#include <alcor2/types.h>
#include <fs/ext2/internal.h>

#include <stdlib.h>
#include <string.h>

#define STORE_BLOCKS 16
#define BLOCK_SZ     1024u

static u8 g_store[STORE_BLOCKS][BLOCK_SZ];

static int reset(void **s)
{
  (void)s;
  memset(g_store, 0, sizeof(g_store));
  return 0;
}

void *kmalloc(u64 n)  { return malloc((size_t)n); }
void  kfree(void *p)  { free(p); }
void *kmemcpy(void *d, const void *s, u64 n) { return memcpy(d, s, n); }
void  kzero(void *d, u64 n) { memset(d, 0, n); }
u8   *cache_get_block(u32 s) { (void)s; return NULL; }
void  cache_put_block(u8 *p) { (void)p; }

i64 vol_read_block(const ext2_volume_t *v, u32 blk, void *buf)
{
  (void)v;
  if(blk >= STORE_BLOCKS) return -EIO;
  memcpy(buf, g_store[blk], BLOCK_SZ);
  return (i64)BLOCK_SZ;
}

i64 vol_write_block(const ext2_volume_t *v, u32 blk, const void *buf)
{
  (void)v;
  if(blk >= STORE_BLOCKS) return -EIO;
  memcpy(g_store[blk], buf, BLOCK_SZ);
  return (i64)BLOCK_SZ;
}

#include "../../src/fs/ext2/inode.c"

/* ── volume fixture ────────────────────────────────────────────────────────
 * 1 group, 8 inodes per block (1024 / 128), inode table at block 2.
 * Inodes 1..8 all live in the same on-disk block → RMW tests are in-scope. */

#define INODE_TABLE 2u

static ext2_group_desc_t g_gd;

static ext2_volume_t make_vol(void)
{
  ext2_volume_t v;
  memset(&v, 0, sizeof(v));
  v.block_size       = BLOCK_SZ;
  v.inode_size       = 128;
  v.inodes_per_group = 8;
  v.inodes_count     = 8;
  v.groups_count     = 1;
  memset(&g_gd, 0, sizeof(g_gd));
  g_gd.bg_inode_table = INODE_TABLE;
  v.groups            = &g_gd;
  return v;
}

/* Builds a recognisable inode for slot `id` so any byte-level corruption
 * between two inodes is visible. */
static void make_inode(ext2_inode_t *out, u32 id)
{
  memset(out, 0, sizeof(*out));
  out->i_mode         = (u16)(0x8000 | id);
  out->i_size         = id * 0x1000;
  out->i_links_count  = (u16)id;
  out->i_block[0]     = 0xDEAD0000 | id;
}

/* ── boundary validation ──────────────────────────────────────────────────*/

static void read_ino_zero_is_einval(void **state)
{
  (void)state;
  ext2_volume_t v = make_vol();
  ext2_inode_t  out;
  assert_int_equal(read_inode(&v, 0, &out), -EINVAL);
}

static void read_ino_above_count_is_einval(void **state)
{
  (void)state;
  ext2_volume_t v = make_vol();
  ext2_inode_t  out;
  assert_int_equal(read_inode(&v, 9, &out), -EINVAL);  /* count = 8 */
}

static void read_ino_at_count_succeeds(void **state)
{
  (void)state;
  ext2_volume_t v = make_vol();
  ext2_inode_t  src, dst;
  make_inode(&src, 8);
  assert_int_equal(write_inode(&v, 8, &src), 0);
  assert_int_equal(read_inode(&v, 8, &dst), 0);
  assert_int_equal(dst.i_mode, src.i_mode);
}

static void write_ino_zero_is_einval(void **state)
{
  (void)state;
  ext2_volume_t v = make_vol();
  ext2_inode_t  dummy;
  memset(&dummy, 0, sizeof(dummy));
  assert_int_equal(write_inode(&v, 0, &dummy), -EINVAL);
}

static void write_ino_above_count_is_einval(void **state)
{
  (void)state;
  ext2_volume_t v = make_vol();
  ext2_inode_t  dummy;
  memset(&dummy, 0, sizeof(dummy));
  assert_int_equal(write_inode(&v, 9, &dummy), -EINVAL);
}

/* ── RMW correctness ──────────────────────────────────────────────────────
 * These are the tests most likely to find a real bug.
 * Inodes 1..8 share a single 1 KiB table block.  Writing any one of them
 * must leave the others byte-for-byte identical.                           */

static void write_inode1_does_not_corrupt_inode2(void **state)
{
  (void)state;
  ext2_volume_t v = make_vol();

  ext2_inode_t a, b, readback_a, readback_b;
  make_inode(&a, 1);
  make_inode(&b, 2);

  assert_int_equal(write_inode(&v, 1, &a), 0);
  assert_int_equal(write_inode(&v, 2, &b), 0);

  assert_int_equal(read_inode(&v, 1, &readback_a), 0);
  assert_int_equal(read_inode(&v, 2, &readback_b), 0);

  /* Inode 1 must be intact after inode 2 was written. */
  assert_memory_equal(&readback_a, &a, sizeof(a));
  /* Inode 2 must be intact. */
  assert_memory_equal(&readback_b, &b, sizeof(b));
}

static void write_all_inodes_leaves_each_intact(void **state)
{
  (void)state;
  ext2_volume_t v = make_vol();

  ext2_inode_t src[8];
  for(u32 i = 0; i < 8; i++)
    make_inode(&src[i], i + 1);

  /* Write all 8 inodes sequentially — each write is a RMW on the same block. */
  for(u32 i = 0; i < 8; i++)
    assert_int_equal(write_inode(&v, i + 1, &src[i]), 0);

  /* Re-read all and verify none was corrupted by a later write. */
  for(u32 i = 0; i < 8; i++) {
    ext2_inode_t got;
    assert_int_equal(read_inode(&v, i + 1, &got), 0);
    assert_memory_equal(&got, &src[i], sizeof(got));
  }
}

static void overwrite_inode_updates_only_target_fields(void **state)
{
  (void)state;
  ext2_volume_t v = make_vol();

  /* Write initial inode 3 with one pattern, then overwrite with another. */
  ext2_inode_t first, second, readback;
  make_inode(&first,  3);
  make_inode(&second, 33);   /* different sentinel */

  assert_int_equal(write_inode(&v, 3, &first),  0);
  assert_int_equal(write_inode(&v, 3, &second), 0);
  assert_int_equal(read_inode(&v,  3, &readback), 0);

  /* Must reflect the second write, not the first. */
  assert_memory_equal(&readback, &second, sizeof(second));
}

/* Neighboring inodes must be byte-equal to what was originally written.
 * We write inodes 3, 4, 5, then overwrite 4, and verify 3 and 5 survive. */
static void overwrite_middle_inode_preserves_neighbors(void **state)
{
  (void)state;
  ext2_volume_t v = make_vol();

  ext2_inode_t i3, i4, i5, i4_new, got3, got5;
  make_inode(&i3,     3);
  make_inode(&i4,     4);
  make_inode(&i5,     5);
  make_inode(&i4_new, 44);

  assert_int_equal(write_inode(&v, 3, &i3), 0);
  assert_int_equal(write_inode(&v, 4, &i4), 0);
  assert_int_equal(write_inode(&v, 5, &i5), 0);

  assert_int_equal(write_inode(&v, 4, &i4_new), 0);

  assert_int_equal(read_inode(&v, 3, &got3), 0);
  assert_int_equal(read_inode(&v, 5, &got5), 0);

  assert_memory_equal(&got3, &i3, sizeof(i3));
  assert_memory_equal(&got5, &i5, sizeof(i5));
}

int main(void)
{
  const struct CMUnitTest tests[] = {
      cmocka_unit_test_setup(read_ino_zero_is_einval, reset),
      cmocka_unit_test_setup(read_ino_above_count_is_einval, reset),
      cmocka_unit_test_setup(read_ino_at_count_succeeds, reset),
      cmocka_unit_test_setup(write_ino_zero_is_einval, reset),
      cmocka_unit_test_setup(write_ino_above_count_is_einval, reset),
      cmocka_unit_test_setup(write_inode1_does_not_corrupt_inode2, reset),
      cmocka_unit_test_setup(write_all_inodes_leaves_each_intact, reset),
      cmocka_unit_test_setup(overwrite_inode_updates_only_target_fields, reset),
      cmocka_unit_test_setup(overwrite_middle_inode_preserves_neighbors, reset),
  };
  return cmocka_run_group_tests(tests, NULL, NULL);
}
