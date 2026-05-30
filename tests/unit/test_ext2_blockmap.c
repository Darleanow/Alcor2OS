/* Adversarial tests for blockmap.c — targeting off-by-ones at every
 * level boundary, hole propagation, and indirect-tree traversal correctness.
 *
 * Each test is structured so that a plausible implementation bug (wrong
 * comparison operator, wrong subtraction, wrong index) produces a distinct
 * wrong value, not just a crash. */

#include "test_common.h"

#include <alcor2/errno.h>
#include <alcor2/fs/ext2.h>
#include <alcor2/types.h>
#include <fs/ext2/internal.h>

#include <stdlib.h>
#include <string.h>

#define STORE_BLOCKS 64
#define BLOCK_SZ     1024u
#define PPB          (BLOCK_SZ / sizeof(u32)) /* 256 */

static u8 g_store[STORE_BLOCKS][BLOCK_SZ];

void *kmalloc(u64 n)  { return malloc((size_t)n); }
void  kfree(void *p)  { free(p); }
void *kmemcpy(void *d, const void *s, u64 n) { return memcpy(d, s, n); }
void  kzero(void *d, u64 n)  { memset(d, 0, n); }
u8   *cache_get_block(u32 s) { (void)s; return NULL; }
void  cache_put_block(u8 *p) { (void)p; }
i64   ata_read(u8 d, u64 l, u32 c, void *b)
{ (void)d;(void)l;(void)c;(void)b; return -1; }
i64   ata_write(u8 d, u64 l, u32 c, const void *b)
{ (void)d;(void)l;(void)c;(void)b; return -1; }
i64   vol_read_block(const ext2_volume_t *v, u32 blk, void *buf)
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
i64 flush_metadata(ext2_volume_t *v) { (void)v; return 0; }
u32 alloc_block(ext2_volume_t *v, u32 g) { (void)v;(void)g; return 0; }
i64 free_block(ext2_volume_t *v, u32 b) { (void)v;(void)b; return 0; }
i64 write_inode(const ext2_volume_t *v, u32 i, const ext2_inode_t *n)
{ (void)v;(void)i;(void)n; return 0; }

#include "../../src/fs/ext2/blockmap.c"

static int reset(void **s) { (void)s; memset(g_store,0,sizeof(g_store)); return 0; }

static ext2_volume_t make_vol(void)
{
  ext2_volume_t v;
  memset(&v, 0, sizeof(v));
  v.block_size       = BLOCK_SZ;
  v.blocks_per_group = 8192;
  v.inodes_per_group = 1024;
  v.inode_size       = 128;
  v.groups_count     = 1;
  v.inodes_count     = 1024;
  v.blocks_count     = STORE_BLOCKS;
  v.first_data_block = 1;
  return v;
}

/* ── direct/indirect boundary ─────────────────────────────────────────────
 * file_block 11 MUST come from i_block[11] directly.
 * file_block 12 MUST go through the indirect block.
 * Putting DIFFERENT sentinel values in both ensures the wrong branch is
 * immediately caught rather than accidentally passing. */

static void direct_last_slot_not_routed_through_indirect(void **state)
{
  (void)state;
  ext2_volume_t v = make_vol();
  ext2_inode_t  inode;
  memset(&inode, 0, sizeof(inode));

  inode.i_block[11]            = 0xDEAD; /* direct sentinel */
  inode.i_block[EXT2_IND_BLOCK] = 5;

  /* If the code mistakenly uses <= instead of < it routes fb=11 through
   * indirect and reads slot 11 of block 5, which is 0, not 0xDEAD. */
  assert_int_equal(get_block_num(&v, &inode, 11), 0xDEAD);
}

static void indirect_first_slot_not_routed_direct(void **state)
{
  (void)state;
  ext2_volume_t v = make_vol();
  ext2_inode_t  inode;
  memset(&inode, 0, sizeof(inode));

  inode.i_block[EXT2_IND_BLOCK] = 5;

  u32 *ind = (u32 *)g_store[5];
  ind[0]   = 0xBEEF; /* slot 0 of indirect block */

  /* If code uses i_block[12] directly (treating 12 as a direct index),
   * it returns the BLOCK NUMBER of the indirect block (5), not 0xBEEF. */
  assert_int_equal(get_block_num(&v, &inode, EXT2_NDIR_BLOCKS), 0xBEEF);
}

/* ── single/double-indirect boundary ─────────────────────────────────────
 * file_block = 12 + PPB - 1 = 267 → last single-indirect slot
 * file_block = 12 + PPB     = 268 → first double-indirect slot          */

static void single_indirect_last_slot_correct(void **state)
{
  (void)state;
  ext2_volume_t v   = make_vol();
  ext2_inode_t  inode;
  memset(&inode, 0, sizeof(inode));

  inode.i_block[EXT2_IND_BLOCK]  = 5;
  inode.i_block[EXT2_DIND_BLOCK] = 6; /* should NOT be reached */

  u32 *ind = (u32 *)g_store[5];
  ind[PPB - 1] = 0xAAAA; /* last slot of single-indirect */

  u32 *dind = (u32 *)g_store[6];
  dind[0]    = 7;
  u32 *ind2  = (u32 *)g_store[7];
  ind2[0]    = 0xBBBB; /* first dind slot — must NOT be returned */

  u32 fb = EXT2_NDIR_BLOCKS + (u32)PPB - 1;
  assert_int_equal(get_block_num(&v, &inode, fb), 0xAAAA);
}

static void double_indirect_first_slot_correct(void **state)
{
  (void)state;
  ext2_volume_t v = make_vol();
  ext2_inode_t  inode;
  memset(&inode, 0, sizeof(inode));

  /* Single-indirect block's last slot holds 0xAAAA — must NOT be returned. */
  inode.i_block[EXT2_IND_BLOCK]  = 5;
  u32 *ind = (u32 *)g_store[5];
  ind[PPB - 1] = 0xAAAA;

  /* Double-indirect: dind→ind→data */
  inode.i_block[EXT2_DIND_BLOCK] = 8;
  u32 *dind = (u32 *)g_store[8];
  dind[0]   = 9;
  u32 *ind2 = (u32 *)g_store[9];
  ind2[0]   = 0xCCCC;

  u32 fb = EXT2_NDIR_BLOCKS + (u32)PPB;
  assert_int_equal(get_block_num(&v, &inode, fb), 0xCCCC);
}

/* ── hole propagation ─────────────────────────────────────────────────────
 * When i_block[EXT2_IND_BLOCK] == 0, get_block_num must return 0 for ALL
 * file blocks in the single-indirect range, without dereferencing block 0.
 * A naive bug: read_indirect_slot(vol, 0, slot) → reads g_store[0] (the
 * superblock area), possibly returning garbage instead of 0. */

static void hole_indirect_never_reads_block_zero(void **state)
{
  (void)state;
  ext2_volume_t v = make_vol();
  ext2_inode_t  inode;
  memset(&inode, 0, sizeof(inode));

  /* Poison block 0 so that any inadvertent read returns non-zero. */
  memset(g_store[0], 0xFF, BLOCK_SZ);

  inode.i_block[EXT2_IND_BLOCK] = 0; /* null pointer → hole */

  assert_int_equal(get_block_num(&v, &inode, EXT2_NDIR_BLOCKS),     0);
  assert_int_equal(get_block_num(&v, &inode, EXT2_NDIR_BLOCKS + 1), 0);
}

static void hole_dind_never_reads_block_zero(void **state)
{
  (void)state;
  ext2_volume_t v = make_vol();
  ext2_inode_t  inode;
  memset(&inode, 0, sizeof(inode));

  memset(g_store[0], 0xFF, BLOCK_SZ);

  inode.i_block[EXT2_DIND_BLOCK] = 0;

  u32 fb = EXT2_NDIR_BLOCKS + (u32)PPB;
  assert_int_equal(get_block_num(&v, &inode, fb), 0);
}

/* ── double-indirect index arithmetic ────────────────────────────────────
 * Verify that the dind→ind decomposition uses the right slot index.
 * file_block (after removing direct+ppb offset) = i*ppb + j.
 * We place a unique value at dind[i][j] and verify it comes back. */

static void double_indirect_slot_arithmetic(void **state)
{
  (void)state;
  ext2_volume_t v = make_vol();
  ext2_inode_t  inode;
  memset(&inode, 0, sizeof(inode));

  /* Target: dind slot 1 → ind block 11 → slot 3 = 0x1234 */
  inode.i_block[EXT2_DIND_BLOCK] = 10;
  u32 *dind  = (u32 *)g_store[10];
  dind[0]    = 12; /* slot 0 — wrong slot for our target */
  dind[1]    = 11; /* slot 1 — correct for i=1               */

  u32 *ind0  = (u32 *)g_store[12]; /* decoy */
  ind0[3]    = 0xDEAD;

  u32 *ind1  = (u32 *)g_store[11];
  ind1[3]    = 0x1234;

  /* file_block offset within dind range = 1*PPB + 3 */
  u32 fb = EXT2_NDIR_BLOCKS + (u32)PPB + 1 * (u32)PPB + 3;
  assert_int_equal(get_block_num(&v, &inode, fb), 0x1234);
}

/* ── free_inode_blocks clears the right entries ───────────────────────────
 * After free_inode_blocks, every i_block[] entry must be 0 and i_blocks
 * must be 0.  Also verifies that non-existent indirect blocks (value 0)
 * are not dereferenced (would segfault or read poison). */

static void free_inode_blocks_resets_all_pointers(void **state)
{
  (void)state;
  ext2_volume_t     v  = make_vol();
  ext2_group_desc_t gd;
  memset(&gd, 0, sizeof(gd));
  v.groups = &gd;

  ext2_inode_t inode;
  memset(&inode, 0, sizeof(inode));
  for(u32 i = 0; i < EXT2_NDIR_BLOCKS; i++)
    inode.i_block[i] = i + 1;
  inode.i_block[EXT2_IND_BLOCK]  = 0; /* unallocated */
  inode.i_block[EXT2_DIND_BLOCK] = 0;
  inode.i_block[EXT2_TIND_BLOCK] = 0;
  inode.i_blocks                 = 24;

  assert_int_equal(free_inode_blocks(&v, &inode), 0);

  for(u32 i = 0; i < EXT2_N_BLOCKS; i++)
    assert_int_equal(inode.i_block[i], 0);
  assert_int_equal(inode.i_blocks, 0);
}

int main(void)
{
  const struct CMUnitTest tests[] = {
      cmocka_unit_test_setup(direct_last_slot_not_routed_through_indirect, reset),
      cmocka_unit_test_setup(indirect_first_slot_not_routed_direct, reset),
      cmocka_unit_test_setup(single_indirect_last_slot_correct, reset),
      cmocka_unit_test_setup(double_indirect_first_slot_correct, reset),
      cmocka_unit_test_setup(hole_indirect_never_reads_block_zero, reset),
      cmocka_unit_test_setup(hole_dind_never_reads_block_zero, reset),
      cmocka_unit_test_setup(double_indirect_slot_arithmetic, reset),
      cmocka_unit_test_setup(free_inode_blocks_resets_all_pointers, reset),
  };
  return cmocka_run_group_tests(tests, NULL, NULL);
}
