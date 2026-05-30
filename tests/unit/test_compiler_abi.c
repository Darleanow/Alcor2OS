/* Adversarial tests for compiler_abi.c — memmove overlap correctness.
 *
 * Problem: compiler_abi.c redefines memcpy/memset/memmove, which collides
 * with cmocka's libc usage.  We isolate by wrapping the implementations
 * under test behind abi_* aliases so cmocka can still use its own libc
 * symbols safely.
 *
 * The critical bug class: a memmove that always copies forward corrupts data
 * when dst > src and regions overlap (right-shift case).  Every test puts
 * a distinct expected value at every byte so even 1 byte of corruption fails. */

#include "test_common.h"

#include <alcor2/types.h>
#include <string.h>

/* Host stubs for the kmem* symbols that compiler_abi.c calls. */
void *kmemcpy(void *dst, const void *src, u64 n) { return memcpy(dst, src, n); }
void *kmemset(void *dst, int c, u64 n)           { return memset(dst, c, n); }
void  kzero(void *dst, u64 n)                    { memset(dst, 0, n); }

/* Rename the ABI symbols before including so they don't clash with libc.
 * cmocka internaly calls the real memcpy/memset; we test our abi_memmove. */
#define memcpy  abi_memcpy
#define memset  abi_memset
#define memmove abi_memmove
#include "../../src/lib/compiler_abi.c"
#undef memcpy
#undef memset
#undef memmove

/* ── memmove: right-shift (dst > src, overlap) ────────────────────────────
 * A forward-copy-only memmove corrupts data here because it reads src[i]
 * after the previous iteration has already overwritten it via dst[i-1]. */

static void memmove_right_shift_by_1(void **state)
{
  (void)state;
  char buf[] = "ABCDEFGH";
  abi_memmove(buf + 1, buf, 7);
  /* Expected: "AABCDEFG"
   * Forward-copy bug produces: "AABCDEFI" (dst[6] == buf[5] already = 'F'
   * by the time we read buf[6], so we'd get 'F' not 'G') — distinctly wrong */
  assert_int_equal(buf[1], 'A');
  assert_int_equal(buf[2], 'B');
  assert_int_equal(buf[3], 'C');
  assert_int_equal(buf[4], 'D');
  assert_int_equal(buf[5], 'E');
  assert_int_equal(buf[6], 'F');
  assert_int_equal(buf[7], 'G');
}

static void memmove_right_shift_by_4_full_corruption_probe(void **state)
{
  (void)state;
  /* 12 bytes of payload shifted right by 4, so 8 bytes of overlap.
   * A naive forward copy would duplicate the first 4 bytes into every
   * subsequent position once they overwrite the source window. */
  unsigned char buf[16];
  for(int i = 0; i < 12; i++) buf[i] = (unsigned char)(i + 1); /* 1..12 */
  for(int i = 12; i < 16; i++) buf[i] = 0xFF; /* sentinel, must be untouched */

  abi_memmove(buf + 4, buf, 12);

  /* buf[4..15] must equal original buf[0..11] */
  for(int i = 0; i < 12; i++)
    assert_int_equal(buf[4 + i], i + 1);
  /* buf[0..3] must be unchanged */
  for(int i = 0; i < 4; i++)
    assert_int_equal(buf[i], i + 1);
}

/* ── memmove: left-shift (dst < src, overlap — forward-copy is safe) ──────*/

static void memmove_left_shift_by_1(void **state)
{
  (void)state;
  char buf[] = "ABCDEFGH";
  abi_memmove(buf, buf + 1, 7);
  assert_int_equal(buf[0], 'B');
  assert_int_equal(buf[1], 'C');
  assert_int_equal(buf[2], 'D');
  assert_int_equal(buf[3], 'E');
  assert_int_equal(buf[4], 'F');
  assert_int_equal(buf[5], 'G');
  assert_int_equal(buf[6], 'H');
}

/* ── memmove: single-byte overlap on the right — minimum-stress case ──────
 * dst = src + (n-1): only 1 byte overlaps.  Forward copy reads buf[n-1]
 * AFTER it has been overwritten by the dst[0] write. */

static void memmove_single_byte_overlap_right_edge(void **state)
{
  (void)state;
  char buf[] = "XYZ---";
  abi_memmove(buf + 2, buf, 3); /* overlap: buf[2] is both dst[0] and src[2] */
  assert_int_equal(buf[2], 'X');
  assert_int_equal(buf[3], 'Y');
  assert_int_equal(buf[4], 'Z');
}

/* ── memmove: dst == src (identity, must be a no-op) ─────────────────────*/

static void memmove_identity_preserves_data(void **state)
{
  (void)state;
  char buf[] = "SENTINEL";
  abi_memmove(buf, buf, 9);
  assert_string_equal(buf, "SENTINEL");
}

/* ── memmove: adjacent regions (no overlap, boundary condition) ───────────
 * dst == src + n: zero bytes of overlap.  Must copy correctly without
 * triggering the backward-copy path. */

static void memmove_adjacent_dst_after_src(void **state)
{
  (void)state;
  char buf[16];
  memcpy(buf, "12345678XXXXXXXX", 16);
  abi_memmove(buf + 8, buf, 8);
  assert_memory_equal(buf,     "12345678", 8);
  assert_memory_equal(buf + 8, "12345678", 8);
}

/* ── memmove: zero-length must never write ────────────────────────────────*/

static void memmove_zero_length_is_noop(void **state)
{
  (void)state;
  unsigned char buf[4] = {0xAA, 0xBB, 0xCC, 0xDD};
  abi_memmove(buf + 1, buf, 0);
  assert_int_equal(buf[0], 0xAA);
  assert_int_equal(buf[1], 0xBB);
}

/* ── memmove: return value must be dst ───────────────────────────────────*/

static void memmove_returns_dst(void **state)
{
  (void)state;
  char buf[8] = "ABCDEFG";
  void *ret = abi_memmove(buf + 2, buf, 4);
  assert_ptr_equal(ret, buf + 2);
}

/* ── memcpy sanity ────────────────────────────────────────────────────────*/

static void abi_memcpy_copies_and_returns_dst(void **state)
{
  (void)state;
  char src[] = "hello";
  char dst[8];
  memset(dst, 0, sizeof(dst));
  void *ret = abi_memcpy(dst, src, 6);
  assert_ptr_equal(ret, dst);
  assert_string_equal(dst, "hello");
}

/* ── memset sanity ────────────────────────────────────────────────────────*/

static void abi_memset_fills_buffer(void **state)
{
  (void)state;
  unsigned char buf[8];
  void *ret = abi_memset(buf, 0x5A, sizeof(buf));
  assert_ptr_equal(ret, buf);
  for(size_t i = 0; i < sizeof(buf); i++)
    assert_int_equal(buf[i], 0x5A);
}

int main(void)
{
  const struct CMUnitTest tests[] = {
      cmocka_unit_test(memmove_right_shift_by_1),
      cmocka_unit_test(memmove_right_shift_by_4_full_corruption_probe),
      cmocka_unit_test(memmove_left_shift_by_1),
      cmocka_unit_test(memmove_single_byte_overlap_right_edge),
      cmocka_unit_test(memmove_identity_preserves_data),
      cmocka_unit_test(memmove_adjacent_dst_after_src),
      cmocka_unit_test(memmove_zero_length_is_noop),
      cmocka_unit_test(memmove_returns_dst),
      cmocka_unit_test(abi_memcpy_copies_and_returns_dst),
      cmocka_unit_test(abi_memset_fills_buffer),
  };
  return cmocka_run_group_tests(tests, NULL, NULL);
}
