#include "test_common.h"
#include <stdio.h>
#include <string.h>

#include "../../user/apps/shell/platform/history.c"

static void test_hist_empty_at_start(void **state)
{
  (void)state;
  assert_int_equal(sh_hist_count(), 0);
  assert_null(sh_hist_at(0));
}

static void test_hist_push_one(void **state)
{
  (void)state;
  sh_hist_push("ls -l");
  assert_int_equal(sh_hist_count(), 1);
  assert_string_equal(sh_hist_at(0), "ls -l");
}

static void test_hist_push_duplicate_skipped(void **state)
{
  (void)state;
  int before = sh_hist_count();
  sh_hist_push("pwd_unique_dedup");
  sh_hist_push("pwd_unique_dedup");
  /* second push of the same line is skipped — count grows by exactly 1 */
  assert_int_equal(sh_hist_count(), before + 1);
}

static void test_hist_push_empty_skipped(void **state)
{
  (void)state;
  int before = sh_hist_count();
  sh_hist_push("");
  sh_hist_push(NULL);
  assert_int_equal(sh_hist_count(), before);
}

static void test_hist_push_multiple(void **state)
{
  (void)state;
  sh_hist_push("echo a");
  sh_hist_push("echo b");
  sh_hist_push("echo c");
  int n = sh_hist_count();
  assert_true(n >= 3);
  assert_string_equal(sh_hist_at(n - 1), "echo c");
  assert_string_equal(sh_hist_at(n - 2), "echo b");
  assert_string_equal(sh_hist_at(n - 3), "echo a");
}

static void test_hist_at_out_of_range(void **state)
{
  (void)state;
  assert_null(sh_hist_at(-1));
  assert_null(sh_hist_at(sh_hist_count()));
  assert_null(sh_hist_at(9999));
}

static void test_hist_eviction_when_full(void **state)
{
  (void)state;
  /* Push 128 unique entries to fill the ring, then push one more.
   * The oldest entry (entry 0) should be evicted. */
  char buf[16];
  for(int i = 0; i < 128; i++) {
    snprintf(buf, sizeof(buf), "cmd%d", i);
    sh_hist_push(buf);
  }
  int n = sh_hist_count();
  assert_int_equal(n, 128);

  sh_hist_push("new_entry");
  /* count stays at 128 (oldest dropped) */
  assert_int_equal(sh_hist_count(), 128);
  /* newest is at the end */
  assert_string_equal(sh_hist_at(127), "new_entry");
  /* "cmd0" (the oldest) is gone; entry 0 is now "cmd1" */
  assert_string_equal(sh_hist_at(0), "cmd1");
}

int main(void)
{
  const struct CMUnitTest tests[] = {
      cmocka_unit_test(test_hist_empty_at_start),
      cmocka_unit_test(test_hist_push_one),
      cmocka_unit_test(test_hist_push_duplicate_skipped),
      cmocka_unit_test(test_hist_push_empty_skipped),
      cmocka_unit_test(test_hist_push_multiple),
      cmocka_unit_test(test_hist_at_out_of_range),
      cmocka_unit_test(test_hist_eviction_when_full),
  };
  return cmocka_run_group_tests(tests, NULL, NULL);
}
