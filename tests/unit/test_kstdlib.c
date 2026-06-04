/* Unit tests for src/lib/kstdlib.c (kernel micro stdlib). */

#include "test_common.h"

#include <alcor2/kstdlib.h>
#include <string.h>

/* kmemcpy */

static void kmemcpy_copies_and_returns_dst(void **state)
{
  (void)state;
  char src[] = "hello world";
  char dst[32];
  memset(dst, 0xAA, sizeof(dst));

  assert_ptr_equal(kmemcpy(dst, src, sizeof("hello world")), dst);
  assert_string_equal(dst, "hello world");
  assert_int_equal((unsigned char)dst[sizeof("hello world")], 0xAA);
}

static void kmemcpy_zero_len_touches_nothing(void **state)
{
  (void)state;
  char dst[4] = {1, 2, 3, 4};
  char src[4] = {9, 9, 9, 9};
  kmemcpy(dst, src, 0);
  assert_int_equal(dst[0], 1);
}

static void kmemcpy_handles_full_byte_range(void **state)
{
  (void)state;
  unsigned char src[256], dst[256];
  for(int i = 0; i < 256; i++)
    src[i] = (unsigned char)(255 - i);
  kmemcpy(dst, src, 256);
  assert_memory_equal(dst, src, 256);
}

/* kmemset / kzero */

static void kmemset_fills_and_returns_dst(void **state)
{
  (void)state;
  unsigned char buf[16];
  assert_ptr_equal(kmemset(buf, 0x5A, sizeof(buf)), buf);
  for(size_t i = 0; i < sizeof(buf); i++)
    assert_int_equal(buf[i], 0x5A);
}

static void kmemset_truncates_value_to_byte(void **state)
{
  (void)state;
  unsigned char buf[4];
  kmemset(buf, 0x1FF, sizeof(buf));
  for(size_t i = 0; i < sizeof(buf); i++)
    assert_int_equal(buf[i], 0xFF);
}

static void kmemset_zero_len_touches_nothing(void **state)
{
  (void)state;
  unsigned char buf[2] = {1, 2};
  kmemset(buf, 0xFF, 0);
  assert_int_equal(buf[0], 1);
}

static void kzero_clears_region(void **state)
{
  (void)state;
  unsigned char buf[8];
  memset(buf, 0xFF, sizeof(buf));
  kzero(buf, sizeof(buf));
  for(size_t i = 0; i < sizeof(buf); i++)
    assert_int_equal(buf[i], 0);
}

/* kstrlen */

static void kstrlen_counts_chars(void **state)
{
  (void)state;
  assert_int_equal(kstrlen(""), 0);
  assert_int_equal(kstrlen("a"), 1);
  assert_int_equal(kstrlen("hello"), 5);
}

/* kstrncpy */

static void kstrncpy_copies_when_it_fits(void **state)
{
  (void)state;
  char dst[16];
  assert_ptr_equal(kstrncpy(dst, "abc", sizeof(dst)), dst);
  assert_string_equal(dst, "abc");
}

static void kstrncpy_truncates_and_terminates(void **state)
{
  (void)state;
  char dst[4];
  memset(dst, 'X', sizeof(dst));
  kstrncpy(dst, "abcdef", 4);
  assert_string_equal(dst, "abc");
  assert_int_equal(dst[3], '\0');
}

static void kstrncpy_exact_fit_terminates(void **state)
{
  (void)state;
  char dst[4];
  kstrncpy(dst, "abc", 4);
  assert_string_equal(dst, "abc");
  assert_int_equal(dst[3], '\0');
}

static void kstrncpy_max_one_writes_only_nul(void **state)
{
  (void)state;
  char dst[2] = {'Z', 'Z'};
  kstrncpy(dst, "abc", 1);
  assert_int_equal(dst[0], '\0');
  assert_int_equal(dst[1], 'Z');
}

static void kstrncpy_max_zero_writes_nothing(void **state)
{
  (void)state;
  char canary[8];
  memset(canary, 'C', sizeof(canary));
  kstrncpy(canary, "abc", 0);
  for(size_t i = 0; i < sizeof(canary); i++)
    assert_int_equal(canary[i], 'C');
}

/* kstrcmp / kstreq */

static void kstrcmp_equal_strings_return_zero(void **state)
{
  (void)state;
  assert_int_equal(kstrcmp("abc", "abc"), 0);
  assert_int_equal(kstrcmp("", ""), 0);
}

static void kstrcmp_orders_lexicographically(void **state)
{
  (void)state;
  assert_true(kstrcmp("abc", "abd") < 0);
  assert_true(kstrcmp("abd", "abc") > 0);
  assert_true(kstrcmp("ab", "abc") < 0);
  assert_true(kstrcmp("abc", "ab") > 0);
}

static void kstrcmp_compares_bytes_unsigned(void **state)
{
  (void)state;
  assert_true(kstrcmp("\xff", "a") > 0);
}

static void kstreq_matches_only_equal(void **state)
{
  (void)state;
  assert_true(kstreq("x", "x"));
  assert_false(kstreq("x", "y"));
  assert_true(kstreq("", ""));
}

/* kstrncmp */

static void kstrncmp_zero_n_returns_zero(void **state)
{
  (void)state;
  assert_int_equal(kstrncmp("abc", "xyz", 0), 0);
}

static void kstrncmp_equal_within_n(void **state)
{
  (void)state;
  assert_int_equal(kstrncmp("abcXX", "abcYY", 3), 0);
}

static void kstrncmp_differ_within_n(void **state)
{
  (void)state;
  assert_true(kstrncmp("abc", "abd", 3) < 0);
  assert_true(kstrncmp("abd", "abc", 3) > 0);
}

static void kstrncmp_equal_strings(void **state)
{
  (void)state;
  assert_int_equal(kstrncmp("abc", "abc", 3), 0);
  assert_int_equal(kstrncmp("abc", "abc", 100), 0);
}

static void kstrncmp_prefix_is_smaller(void **state)
{
  (void)state;
  assert_true(kstrncmp("ab", "abc", 3) < 0);
  assert_true(kstrncmp("abc", "ab", 3) > 0);
}

static void kstrncmp_n_one_compares_first_char(void **state)
{
  (void)state;
  assert_int_equal(kstrncmp("axxx", "ayyy", 1), 0);
  assert_true(kstrncmp("a", "b", 1) < 0);
}

/* kstrrchr */

static void kstrrchr_finds_last_match(void **state)
{
  (void)state;
  const char *s = "a/b/c";
  assert_ptr_equal(kstrrchr(s, '/'), s + 3);
}

static void kstrrchr_returns_null_when_absent(void **state)
{
  (void)state;
  assert_null(kstrrchr("abc", 'z'));
}

static void kstrrchr_nul_returns_terminator(void **state)
{
  (void)state;
  const char *s = "abc";
  assert_ptr_equal(kstrrchr(s, '\0'), s + 3);
}

static void kstrrchr_matches_first_char(void **state)
{
  (void)state;
  const char *s = "abc";
  assert_ptr_equal(kstrrchr(s, 'a'), s);
}

/* kstrlcat (BSD strlcat semantics) */

static void kstrlcat_appends(void **state)
{
  (void)state;
  char dst[16] = "foo";
  assert_int_equal(kstrlcat(dst, "bar", sizeof(dst)), 6);
  assert_string_equal(dst, "foobar");
}

static void kstrlcat_truncates_and_reports_would_be_length(void **state)
{
  (void)state;
  char dst[6] = "foo";
  assert_int_equal(kstrlcat(dst, "bar", sizeof(dst)), 6);
  assert_string_equal(dst, "fooba");
  assert_int_equal(dst[5], '\0');
}

static void kstrlcat_empty_dst(void **state)
{
  (void)state;
  char dst[8] = "";
  assert_int_equal(kstrlcat(dst, "abc", sizeof(dst)), 3);
  assert_string_equal(dst, "abc");
}

static void kstrlcat_empty_src(void **state)
{
  (void)state;
  char dst[8] = "abc";
  assert_int_equal(kstrlcat(dst, "", sizeof(dst)), 3);
  assert_string_equal(dst, "abc");
}

static void kstrlcat_cap_zero_returns_src_len(void **state)
{
  (void)state;
  char dst[4] = "ab";
  assert_int_equal(kstrlcat(dst, "xyz", 0), 3);
  assert_string_equal(dst, "ab");
}

static void kstrlcat_unterminated_dst_returns_cap_plus_src(void **state)
{
  (void)state;
  char dst[8];
  memset(dst, 'A', sizeof(dst));
  dst[7] = '\0';
  assert_int_equal(kstrlcat(dst, "xy", 4), 6);
}

int main(void)
{
  const struct CMUnitTest tests[] = {
      cmocka_unit_test(kmemcpy_copies_and_returns_dst),
      cmocka_unit_test(kmemcpy_zero_len_touches_nothing),
      cmocka_unit_test(kmemcpy_handles_full_byte_range),

      cmocka_unit_test(kmemset_fills_and_returns_dst),
      cmocka_unit_test(kmemset_truncates_value_to_byte),
      cmocka_unit_test(kmemset_zero_len_touches_nothing),
      cmocka_unit_test(kzero_clears_region),

      cmocka_unit_test(kstrlen_counts_chars),

      cmocka_unit_test(kstrncpy_copies_when_it_fits),
      cmocka_unit_test(kstrncpy_truncates_and_terminates),
      cmocka_unit_test(kstrncpy_exact_fit_terminates),
      cmocka_unit_test(kstrncpy_max_one_writes_only_nul),
      cmocka_unit_test(kstrncpy_max_zero_writes_nothing),

      cmocka_unit_test(kstrcmp_equal_strings_return_zero),
      cmocka_unit_test(kstrcmp_orders_lexicographically),
      cmocka_unit_test(kstrcmp_compares_bytes_unsigned),
      cmocka_unit_test(kstreq_matches_only_equal),

      cmocka_unit_test(kstrncmp_zero_n_returns_zero),
      cmocka_unit_test(kstrncmp_equal_within_n),
      cmocka_unit_test(kstrncmp_differ_within_n),
      cmocka_unit_test(kstrncmp_equal_strings),
      cmocka_unit_test(kstrncmp_prefix_is_smaller),
      cmocka_unit_test(kstrncmp_n_one_compares_first_char),

      cmocka_unit_test(kstrrchr_finds_last_match),
      cmocka_unit_test(kstrrchr_returns_null_when_absent),
      cmocka_unit_test(kstrrchr_nul_returns_terminator),
      cmocka_unit_test(kstrrchr_matches_first_char),

      cmocka_unit_test(kstrlcat_appends),
      cmocka_unit_test(kstrlcat_truncates_and_reports_would_be_length),
      cmocka_unit_test(kstrlcat_empty_dst),
      cmocka_unit_test(kstrlcat_empty_src),
      cmocka_unit_test(kstrlcat_cap_zero_returns_src_len),
      cmocka_unit_test(kstrlcat_unterminated_dst_returns_cap_plus_src),
  };

  return cmocka_run_group_tests(tests, NULL, NULL);
}
