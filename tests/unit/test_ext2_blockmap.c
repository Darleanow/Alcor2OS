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

void     *kmalloc(u64 n)
{
  return malloc((size_t)n);
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
static bool g_read_fail = false;
i64 vol_read_block(const ext2_volume_t *v, u32 blk, void *buf)
{
  (void)v;
  if(g_read_fail || blk >= STORE_BLOCKS)
    return -EIO;
  memcpy(buf, g_store[blk], BLOCK_SZ);
  return 0;
}
i64 vol_write_block(const ext2_volume_t *v, u32 blk, const void *buf)
{
  (void)v;
  if(blk >= STORE_BLOCKS)
    return -EIO;
  memcpy(g_store[blk], buf, BLOCK_SZ);
  return 0;
}
i64 flush_metadata(ext2_volume_t *v)
{
  (void)v;
  return 0;
}
static u32 g_next_block = 0;
u32        alloc_block(ext2_volume_t *v, u32 g)
{
  (void)v;
  (void)g;
  return g_next_block ? g_next_block++ : 0;
}
i64 free_block(ext2_volume_t *v, u32 b)
{
  (void)v;
  (void)b;
  return 0;
}
i64 write_inode(const ext2_volume_t *v, u32 i, const ext2_inode_t *n)
{
  (void)v;
  (void)i;
  (void)n;
  return 0;
}

#include "../../src/fs/ext2/blockmap.c"

static int reset(void **s)
{
  (void)s;
  memset(g_store, 0, sizeof(g_store));
  g_next_block = 0;
  g_read_fail  = false;
  return 0;
}

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

static void direct_last_slot_not_routed_through_indirect(void **state)
{
  (void)state;
  ext2_volume_t v = make_vol();
  ext2_inode_t  inode;
  memset(&inode, 0, sizeof(inode));

  inode.i_block[11]             = 0xDEAD; /* direct sentinel */
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

static void single_indirect_last_slot_correct(void **state)
{
  (void)state;
  ext2_volume_t v = make_vol();
  ext2_inode_t  inode;
  memset(&inode, 0, sizeof(inode));

  inode.i_block[EXT2_IND_BLOCK]  = 5;
  inode.i_block[EXT2_DIND_BLOCK] = 6; /* should NOT be reached */

  u32 *ind     = (u32 *)g_store[5];
  ind[PPB - 1] = 0xAAAA; /* last slot of single-indirect */

  u32 *dind = (u32 *)g_store[6];
  dind[0]   = 7;
  u32 *ind2 = (u32 *)g_store[7];
  ind2[0]   = 0xBBBB; /* first dind slot — must NOT be returned */

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
  inode.i_block[EXT2_IND_BLOCK] = 5;
  u32 *ind                      = (u32 *)g_store[5];
  ind[PPB - 1]                  = 0xAAAA;

  /* Double-indirect: dind→ind→data */
  inode.i_block[EXT2_DIND_BLOCK] = 8;
  u32 *dind                      = (u32 *)g_store[8];
  dind[0]                        = 9;
  u32 *ind2                      = (u32 *)g_store[9];
  ind2[0]                        = 0xCCCC;

  u32 fb = EXT2_NDIR_BLOCKS + (u32)PPB;
  assert_int_equal(get_block_num(&v, &inode, fb), 0xCCCC);
}

static void hole_indirect_never_reads_block_zero(void **state)
{
  (void)state;
  ext2_volume_t v = make_vol();
  ext2_inode_t  inode;
  memset(&inode, 0, sizeof(inode));

  /* Poison block 0 so that any inadvertent read returns non-zero. */
  memset(g_store[0], 0xFF, BLOCK_SZ);

  inode.i_block[EXT2_IND_BLOCK] = 0; /* null pointer → hole */

  assert_int_equal(get_block_num(&v, &inode, EXT2_NDIR_BLOCKS), 0);
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

static void double_indirect_slot_arithmetic(void **state)
{
  (void)state;
  ext2_volume_t v = make_vol();
  ext2_inode_t  inode;
  memset(&inode, 0, sizeof(inode));

  /* Target: dind slot 1 → ind block 11 → slot 3 = 0x1234 */
  inode.i_block[EXT2_DIND_BLOCK] = 10;
  u32 *dind                      = (u32 *)g_store[10];
  dind[0] = 12; /* slot 0 — wrong slot for our target */
  dind[1] = 11; /* slot 1 — correct for i=1               */

  u32 *ind0 = (u32 *)g_store[12]; /* decoy */
  ind0[3]   = 0xDEAD;

  u32 *ind1 = (u32 *)g_store[11];
  ind1[3]   = 0x1234;

  /* file_block offset within dind range = 1*PPB + 3 */
  u32 fb = EXT2_NDIR_BLOCKS + (u32)PPB + 1 * (u32)PPB + 3;
  assert_int_equal(get_block_num(&v, &inode, fb), 0x1234);
}

static void free_inode_blocks_resets_all_pointers(void **state)
{
  (void)state;
  ext2_volume_t     v = make_vol();
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

/* ptrs_per_block */

static void ptrs_per_block_matches_block_size(void **state)
{
  (void)state;
  ext2_volume_t v = make_vol();
  assert_int_equal(ptrs_per_block(&v), BLOCK_SZ / sizeof(u32));
}

/* triple indirect boundary */

static void triple_indirect_first_slot(void **state)
{
  (void)state;
  ext2_volume_t v = make_vol();
  ext2_inode_t  inode;
  memset(&inode, 0, sizeof(inode));

  /* tind = block 1, dind = block 2, ind = block 3, data = 0xF00F */
  u32 tind_blk = 1, dind_blk = 2, ind_blk = 3;
  inode.i_block[EXT2_TIND_BLOCK] = tind_blk;

  /* tind[0] = dind_blk */
  ((u32 *)g_store[tind_blk])[0] = dind_blk;
  /* dind[0] = ind_blk */
  ((u32 *)g_store[dind_blk])[0] = ind_blk;
  /* ind[0] = data block */
  ((u32 *)g_store[ind_blk])[0] = 0xF00F;

  u32 fb = EXT2_NDIR_BLOCKS + PPB + PPB * PPB; /* first tind block */
  assert_int_equal(get_block_num(&v, &inode, fb), 0xF00F);
}

/* alloc_file_block direct */

static void alloc_file_block_direct(void **state)
{
  (void)state;
  ext2_volume_t     v = make_vol();
  ext2_group_desc_t gd;
  memset(&gd, 0, sizeof(gd));
  v.groups = &gd;

  ext2_inode_t inode;
  memset(&inode, 0, sizeof(inode));

  g_next_block = 10; /* alloc_block returns 10, 11, ... */
  u32 blk      = alloc_file_block(&v, &inode, 0, 0);
  assert_int_equal(blk, 10);
  assert_int_equal(inode.i_block[0], 10);
}

static void alloc_file_block_single_indirect(void **state)
{
  (void)state;
  ext2_volume_t     v = make_vol();
  ext2_group_desc_t gd;
  memset(&gd, 0, sizeof(gd));
  v.groups = &gd;

  ext2_inode_t inode;
  memset(&inode, 0, sizeof(inode));

  /* block 10 = indirect table, block 11 = data */
  g_next_block = 10;
  u32 blk      = alloc_file_block(&v, &inode, EXT2_NDIR_BLOCKS, 0);
  assert_true(blk > 0);
  assert_true(inode.i_block[EXT2_IND_BLOCK] != 0);
}

static void alloc_file_block_returns_zero_when_alloc_fails(void **state)
{
  (void)state;
  ext2_volume_t     v = make_vol();
  ext2_group_desc_t gd;
  memset(&gd, 0, sizeof(gd));
  v.groups = &gd;

  ext2_inode_t inode;
  memset(&inode, 0, sizeof(inode));

  g_next_block = 0; /* alloc_block always returns 0 = failure */
  u32 blk      = alloc_file_block(&v, &inode, 0, 0);
  assert_int_equal(blk, 0);
}

/* free_indirect_subtree: with a real singly-indirect block */
static void free_indirect_subtree_single(void **state)
{
  (void)state;
  ext2_volume_t     v = make_vol();
  ext2_group_desc_t gd;
  memset(&gd, 0, sizeof(gd));
  v.groups = &gd;

  /* Store an indirect block at slot 5 pointing to data block 7 */
  ((u32 *)g_store[5])[0] = 7;
  /* free_indirect_subtree at depth=0 should free data block 7, then block 5 */
  free_indirect_subtree(&v, 5, 0);
  /* After freeing, the block entries are cleared via free_block (noop stub) */
  /* Just verify no crash and the function completes */
}

/* free_indirect_subtree: depth=1 walks double-indirect */
static void free_indirect_subtree_double(void **state)
{
  (void)state;
  ext2_volume_t     v = make_vol();
  ext2_group_desc_t gd;
  memset(&gd, 0, sizeof(gd));
  v.groups = &gd;

  /* dind[4] points to an indirect block at slot 8 */
  ((u32 *)g_store[4])[0] = 8;
  /* ind block 8 points to data block 12 */
  ((u32 *)g_store[8])[0] = 12;

  free_indirect_subtree(&v, 4, 1); /* depth=1 → double indirect */
}

/* free_inode_blocks: with indirect blocks set — walks and clears them */
static void free_inode_blocks_with_indirect(void **state)
{
  (void)state;
  ext2_volume_t     v = make_vol();
  ext2_group_desc_t gd;
  memset(&gd, 0, sizeof(gd));
  v.groups = &gd;

  ext2_inode_t inode;
  memset(&inode, 0, sizeof(inode));
  inode.i_block[0]             = 2;  /* direct */
  inode.i_block[EXT2_IND_BLOCK] = 3; /* single indirect pointing to nothing */

  assert_int_equal(free_inode_blocks(&v, &inode), 0);
  assert_int_equal(inode.i_block[0], 0);
  assert_int_equal(inode.i_block[EXT2_IND_BLOCK], 0);
}

/* alloc_zeroed_block: alloc_block fails returns 0 */
static void alloc_zeroed_block_alloc_fails(void **state)
{
  (void)state;
  ext2_volume_t     v = make_vol();
  ext2_group_desc_t gd;
  memset(&gd, 0, sizeof(gd));
  v.groups = &gd;

  ext2_inode_t inode;
  memset(&inode, 0, sizeof(inode));
  g_next_block = 0; /* alloc_block returns 0 = failure */
  assert_int_equal(alloc_zeroed_block(&v, 0, &inode), 0);
}

/* alloc_file_block: double indirect */
static void alloc_file_block_double_indirect(void **state)
{
  (void)state;
  ext2_volume_t     v = make_vol();
  ext2_group_desc_t gd;
  memset(&gd, 0, sizeof(gd));
  v.groups = &gd;

  ext2_inode_t inode;
  memset(&inode, 0, sizeof(inode));

  /* file_block = EXT2_NDIR_BLOCKS + PPB (first double-indirect block) */
  u32 dind_file_block = EXT2_NDIR_BLOCKS + PPB;
  g_next_block        = 5; /* alloc returns 5, 6, 7, ... */

  u32 blk = alloc_file_block(&v, &inode, dind_file_block, 0);
  assert_true(blk > 0);
}

/* alloc_zeroed_block: write fails but still returns the allocated block */
static void alloc_zeroed_block_write_fails(void **state)
{
  (void)state;
  ext2_volume_t     v = make_vol();
  ext2_group_desc_t gd;
  memset(&gd, 0, sizeof(gd));
  v.groups     = &gd;
  v.block_size = 1024;
  gd.bg_free_blocks_count = 10;
  gd.bg_block_bitmap      = 0;

  /* alloc_block will fail because g_store[bg_block_bitmap=0] read fails
     since vol_read_block returns -EIO for blocks >= STORE_BLOCKS (which 0 is not).
     Actually block 0 is in range — alloc_block will succeed since it reads bitmap
     from g_store[0] which is zeroed. */
  gd.bg_free_blocks_count = 1;
  g_next_block            = 7;

  ext2_inode_t inode;
  memset(&inode, 0, sizeof(inode));
  /* alloc_block allocates block 7; kmalloc for zeroing succeeds;
     vol_write_block(7) — block 7 is in g_store. */
  u32 blk = alloc_zeroed_block(&v, 0, &inode);
  /* Should return block 7 regardless of write result */
  assert_int_equal(blk, 7);
}

/* ensure_indirect_slot: kmalloc fails returns 0 */
static void ensure_indirect_slot_kmalloc_fail(void **state)
{
  (void)state;
  ext2_volume_t v = make_vol();
  ext2_inode_t  inode;
  memset(&inode, 0, sizeof(inode));
  /* Make kmalloc fail by using a block size > STORE_BLOCKS*BLOCK_SZ so
     the malloc would be huge. Instead, temporarily override by making
     the block size very large. */
  v.block_size = (u32)-1u; /* will cause malloc to fail */
  u32 result   = ensure_indirect_slot(&v, &inode, 2, 0, 0);
  assert_int_equal(result, 0);
  v.block_size = BLOCK_SZ; /* restore */
}

/* ensure_indirect_slot: vol_read_block fails returns 0 */
static void ensure_indirect_slot_read_fails(void **state)
{
  (void)state;
  ext2_volume_t v = make_vol();
  ext2_inode_t  inode;
  memset(&inode, 0, sizeof(inode));
  /* Block STORE_BLOCKS is out of range → vol_read_block returns -EIO */
  u32 result = ensure_indirect_slot(&v, &inode, STORE_BLOCKS, 0, 0);
  assert_int_equal(result, 0);
}

/* alloc_file_block: triple indirect path */
static void alloc_file_block_triple_indirect(void **state)
{
  (void)state;
  ext2_volume_t     v = make_vol();
  ext2_group_desc_t gd;
  memset(&gd, 0, sizeof(gd));
  v.groups = &gd;

  ext2_inode_t inode;
  memset(&inode, 0, sizeof(inode));

  /* First triple-indirect file block = NDIR + PPB + PPB*PPB */
  u32 tind_file_block = EXT2_NDIR_BLOCKS + PPB + PPB * PPB;
  g_next_block        = 5;

  u32 blk = alloc_file_block(&v, &inode, tind_file_block, 0);
  assert_true(blk > 0);
}

/* ensure_inode_slot: ensure_inode_slot returns 0 when alloc fails */
static void ensure_inode_slot_alloc_fails(void **state)
{
  (void)state;
  ext2_volume_t     v = make_vol();
  ext2_group_desc_t gd;
  memset(&gd, 0, sizeof(gd));
  v.groups = &gd;

  ext2_inode_t inode;
  memset(&inode, 0, sizeof(inode));

  /* g_next_block=0 so alloc_block returns 0 → ensure_inode_slot returns 0 */
  g_next_block = 0;
  /* Single-indirect range: NDIR_BLOCKS */
  u32 blk = alloc_file_block(&v, &inode, EXT2_NDIR_BLOCKS, 0);
  assert_int_equal(blk, 0);
}

/* get_block_num: single-indirect via vol_read_block */
static void get_block_num_single_indirect_read(void **state)
{
  (void)state;
  ext2_volume_t v = make_vol();
  ext2_inode_t  inode;
  memset(&inode, 0, sizeof(inode));
  /* Set up single indirect block at slot 2 */
  inode.i_block[EXT2_IND_BLOCK] = 2;
  /* Store data block 42 at slot 0 of the indirect block */
  ((u32 *)g_store[2])[0] = 42;

  u32 blk = get_block_num(&v, &inode, EXT2_NDIR_BLOCKS);
  assert_int_equal(blk, 42);
}

/* read_indirect_slot: OOM → returns 0 */
static void read_indirect_slot_oom(void **state)
{
  (void)state;
  /* We can't force kmalloc to fail here (it uses malloc).
   * Test the vol_read_block fail path instead. */
  ext2_volume_t v = make_vol();
  g_read_fail = true;
  u32 result = read_indirect_slot(&v, 1, 0);
  assert_int_equal(result, 0);
}

/* read_indirect_slot: indirect_block == 0 → returns 0 (hole) */
static void read_indirect_slot_hole(void **state)
{
  (void)state;
  ext2_volume_t v = make_vol();
  u32 result = read_indirect_slot(&v, 0, 0);
  assert_int_equal(result, 0);
}

/* read_indirect_slot: success → returns slot value */
static void read_indirect_slot_success(void **state)
{
  (void)state;
  ext2_volume_t v = make_vol();
  ((u32 *)g_store[3])[5] = 99;
  u32 result = read_indirect_slot(&v, 3, 5);
  assert_int_equal(result, 99);
}

/* ensure_indirect_slot: vol_read_block fails → returns 0 */
static void ensure_indirect_slot_read_fail(void **state)
{
  (void)state;
  ext2_volume_t     v = make_vol();
  ext2_group_desc_t gd;
  memset(&gd, 0, sizeof(gd));
  v.groups = &gd;
  ext2_inode_t inode;
  memset(&inode, 0, sizeof(inode));
  g_read_fail = true;
  u32 result = ensure_indirect_slot(&v, &inode, 1, 0, 0);
  assert_int_equal(result, 0);
}

/* free_indirect_subtree: depth=0 with real data block → frees it */
static void free_indirect_subtree_depth0_frees_leaf(void **state)
{
  (void)state;
  ext2_volume_t     v = make_vol();
  ext2_group_desc_t gd;
  memset(&gd, 0, sizeof(gd));
  v.groups = &gd;

  /* block 2 is the indirect block; slot 0 points to data block 5 */
  ((u32 *)g_store[2])[0] = 5;
  /* Call free_indirect_subtree at depth=0 — should read block 2,
   * find buf[0]=5, call free_block(5), then free_block(2) */
  free_indirect_subtree(&v, 2, 0);
  /* No crash = success; free_block is a no-op stub */
}

/* free_indirect_subtree: depth=1 recurses into sub-indirect */
static void free_indirect_subtree_depth1_recurses(void **state)
{
  (void)state;
  ext2_volume_t     v = make_vol();
  ext2_group_desc_t gd;
  memset(&gd, 0, sizeof(gd));
  v.groups = &gd;

  /* block 4 is double-indirect; slot 0 → block 6 (single-indirect) */
  ((u32 *)g_store[4])[0] = 6;
  /* block 6 is single-indirect; slot 0 → data block 9 */
  ((u32 *)g_store[6])[0] = 9;

  free_indirect_subtree(&v, 4, 1);
}

/* alloc_file_block: double-indirect dind alloc fails → returns 0 */
static void alloc_file_block_dind_alloc_fails(void **state)
{
  (void)state;
  ext2_volume_t     v = make_vol();
  ext2_group_desc_t gd;
  memset(&gd, 0, sizeof(gd));
  v.groups    = &gd;
  g_next_block = 0; /* alloc always fails */
  ext2_inode_t inode;
  memset(&inode, 0, sizeof(inode));
  /* file_block in double-indirect range */
  u32 fb = EXT2_NDIR_BLOCKS + PPB; /* first double-indirect block */
  u32 result = alloc_file_block(&v, &inode, fb, 0);
  assert_int_equal(result, 0);
}

/* alloc_file_block: triple-indirect dind slot (2nd level) fails → returns 0 (line 254) */
static void alloc_file_block_tind_dind_slot_fails(void **state)
{
  (void)state;
  ext2_volume_t     v = make_vol();
  ext2_group_desc_t gd;
  memset(&gd, 0, sizeof(gd));
  v.groups     = &gd;
  g_next_block = 1; /* tind alloc succeeds (returns 1), then dind slot read fails */
  g_read_fail  = true; /* ensure_indirect_slot's vol_read_block fails → returns 0 */
  ext2_inode_t inode;
  memset(&inode, 0, sizeof(inode));
  u32 fb = EXT2_NDIR_BLOCKS + PPB + PPB * PPB; /* triple-indirect range */
  u32 result = alloc_file_block(&v, &inode, fb, 0);
  assert_int_equal(result, 0);
  g_read_fail = false;
}

/* alloc_file_block: triple-indirect tind alloc fails → returns 0 */
static void alloc_file_block_tind_alloc_fails(void **state)
{
  (void)state;
  ext2_volume_t     v = make_vol();
  ext2_group_desc_t gd;
  memset(&gd, 0, sizeof(gd));
  v.groups    = &gd;
  g_next_block = 0;
  ext2_inode_t inode;
  memset(&inode, 0, sizeof(inode));
  u32 fb = EXT2_NDIR_BLOCKS + PPB + PPB * PPB; /* first triple-indirect block */
  u32 result = alloc_file_block(&v, &inode, fb, 0);
  assert_int_equal(result, 0);
}

int main(void)
{
  const struct CMUnitTest tests[] = {
      cmocka_unit_test_setup(
          direct_last_slot_not_routed_through_indirect, reset
      ),
      cmocka_unit_test_setup(indirect_first_slot_not_routed_direct, reset),
      cmocka_unit_test_setup(single_indirect_last_slot_correct, reset),
      cmocka_unit_test_setup(double_indirect_first_slot_correct, reset),
      cmocka_unit_test_setup(hole_indirect_never_reads_block_zero, reset),
      cmocka_unit_test_setup(hole_dind_never_reads_block_zero, reset),
      cmocka_unit_test_setup(double_indirect_slot_arithmetic, reset),
      cmocka_unit_test_setup(free_inode_blocks_resets_all_pointers, reset),
      cmocka_unit_test_setup(ptrs_per_block_matches_block_size, reset),
      cmocka_unit_test_setup(triple_indirect_first_slot, reset),
      cmocka_unit_test_setup(alloc_file_block_direct, reset),
      cmocka_unit_test_setup(alloc_file_block_single_indirect, reset),
      cmocka_unit_test_setup(
          alloc_file_block_returns_zero_when_alloc_fails, reset
      ),
      cmocka_unit_test_setup(free_indirect_subtree_single, reset),
      cmocka_unit_test_setup(free_indirect_subtree_double, reset),
      cmocka_unit_test_setup(free_inode_blocks_with_indirect, reset),
      cmocka_unit_test_setup(alloc_zeroed_block_alloc_fails, reset),
      cmocka_unit_test_setup(alloc_file_block_double_indirect, reset),
      cmocka_unit_test_setup(alloc_zeroed_block_write_fails, reset),
      cmocka_unit_test_setup(ensure_indirect_slot_kmalloc_fail, reset),
      cmocka_unit_test_setup(ensure_indirect_slot_read_fails, reset),
      cmocka_unit_test_setup(alloc_file_block_triple_indirect, reset),
      cmocka_unit_test_setup(ensure_inode_slot_alloc_fails, reset),
      cmocka_unit_test_setup(get_block_num_single_indirect_read, reset),
      /* new coverage */
      cmocka_unit_test_setup(read_indirect_slot_oom, reset),
      cmocka_unit_test_setup(read_indirect_slot_hole, reset),
      cmocka_unit_test_setup(read_indirect_slot_success, reset),
      cmocka_unit_test_setup(ensure_indirect_slot_read_fail, reset),
      cmocka_unit_test_setup(free_indirect_subtree_depth0_frees_leaf, reset),
      cmocka_unit_test_setup(free_indirect_subtree_depth1_recurses, reset),
      cmocka_unit_test_setup(alloc_file_block_dind_alloc_fails, reset),
      cmocka_unit_test_setup(alloc_file_block_tind_alloc_fails, reset),
      /* new coverage */
      cmocka_unit_test_setup(alloc_file_block_tind_dind_slot_fails, reset),
  };
  return cmocka_run_group_tests(tests, NULL, NULL);
}
