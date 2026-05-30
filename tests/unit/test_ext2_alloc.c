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

void *kmalloc(u64 n) { (void)n; return NULL; }
void  kfree(void *p) { (void)p; }
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
/* vol_read_block / vol_write_block live in super.c — stub as always-failing
 * I/O so the bitmap unit tests (pure bit-manipulation) link and run. */
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
  };
  return cmocka_run_group_tests(tests, NULL, NULL);
}
