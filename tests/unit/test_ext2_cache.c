/* Unit tests for src/fs/ext2/cache.c — single-block scratch pool.
 * kmalloc/kfree are stubbed with tracking stubs so we can assert fallback
 * behaviour without the real heap. */

#include "test_common.h"

#include <alcor2/fs/ext2.h>
#include <alcor2/types.h>
#include <fs/ext2/internal.h>

#include <string.h>

static unsigned malloc_calls;
static unsigned free_calls;
static u8       heap_area[8192];

void           *kmalloc(u64 size)
{
  (void)size;
  malloc_calls++;
  return heap_area;
}

void kfree(void *p)
{
  (void)p;
  free_calls++;
}

/* Reset pool state between tests. */
static int reset(void **state)
{
  (void)state;
  /* g_block_pool is file-static in cache.c; but since it's compiled as a
   * separate object (not test-by-inclusion) we clear it via cache_put_block
   * round-trips. Instead, just drain anything in use via get+put. */
  malloc_calls = free_calls = 0;
  /* Drain any leftover in-use slots by getting until we hit kmalloc, then
   * put everything back. Simpler: just call cache_get_block once per slot
   * beyond capacity to force a clean state isn't possible without access to
   * g_block_pool. So we keep state across tests by not needing global reset —
   * each test starts from a known state by construction. */
  return 0;
}

/* cache_get_block / cache_put_block */

static void get_small_returns_pool_slot(void **state)
{
  (void)state;
  u8 *p = cache_get_block(512);
  assert_non_null(p);
  assert_int_equal(malloc_calls, 0);
  cache_put_block(p);
}

static void get_oversized_falls_back_to_kmalloc(void **state)
{
  (void)state;
  u8 *p = cache_get_block(EXT2_MAX_BLOCK_SIZE + 1);
  assert_non_null(p);
  assert_int_equal(malloc_calls, 1);
  /* Pointer not in pool — put must call kfree. */
  cache_put_block(p);
  assert_int_equal(free_calls, 1);
}

static void put_pool_slot_does_not_kfree(void **state)
{
  (void)state;
  u8 *p = cache_get_block(1024);
  cache_put_block(p);
  assert_int_equal(free_calls, 0);
}

static void pool_exhaustion_falls_back_to_kmalloc(void **state)
{
  (void)state;
  u8 *slots[EXT2_BLOCK_CACHE_SIZE];
  for(int i = 0; i < EXT2_BLOCK_CACHE_SIZE; i++)
    slots[i] = cache_get_block(512);
  assert_int_equal(malloc_calls, 0);

  /* One more request must use kmalloc. */
  u8 *overflow = cache_get_block(512);
  assert_int_equal(malloc_calls, 1);

  /* Return the overflow pointer — must kfree it. */
  cache_put_block(overflow);
  assert_int_equal(free_calls, 1);

  /* Return pool slots — must not kfree any of them. */
  for(int i = 0; i < EXT2_BLOCK_CACHE_SIZE; i++)
    cache_put_block(slots[i]);
  assert_int_equal(free_calls, 1);
}

static void returned_slot_is_reusable(void **state)
{
  (void)state;
  u8 *first = cache_get_block(512);
  cache_put_block(first);
  u8 *second = cache_get_block(512);
  assert_ptr_equal(first, second);
  assert_int_equal(malloc_calls, 0);
  cache_put_block(second);
}

static void max_block_size_uses_pool(void **state)
{
  (void)state;
  u8 *p = cache_get_block(EXT2_MAX_BLOCK_SIZE);
  assert_non_null(p);
  assert_int_equal(malloc_calls, 0);
  cache_put_block(p);
}

int main(void)
{
  const struct CMUnitTest tests[] = {
      cmocka_unit_test_setup(get_small_returns_pool_slot, reset),
      cmocka_unit_test_setup(get_oversized_falls_back_to_kmalloc, reset),
      cmocka_unit_test_setup(put_pool_slot_does_not_kfree, reset),
      cmocka_unit_test_setup(pool_exhaustion_falls_back_to_kmalloc, reset),
      cmocka_unit_test_setup(returned_slot_is_reusable, reset),
      cmocka_unit_test_setup(max_block_size_uses_pool, reset),
  };
  return cmocka_run_group_tests(tests, NULL, NULL);
}
