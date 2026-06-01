#include "vega_mocks.h"

#include "../../user/sdk/vega/fntab.c"

static void test_fntab_set_and_get(void **state)
{
  (void)state;
  char *name = strdup("myfunc");
  int   rc   = fntab_set(name, NULL, 0, NULL);
  assert_int_equal(rc, 0);

  const fn_entry_t *e = fntab_get("myfunc");
  assert_non_null(e);
  assert_string_equal(e->name, "myfunc");
}

static void test_fntab_overwrite(void **state)
{
  (void)state;
  char *n1 = strdup("overwrite");
  char *n2 = strdup("overwrite");
  fntab_set(n1, NULL, 0, NULL);
  int rc = fntab_set(n2, NULL, 0, NULL);
  assert_int_equal(rc, 0);

  const fn_entry_t *e = fntab_get("overwrite");
  assert_non_null(e);
}

static void test_fntab_get_missing(void **state)
{
  (void)state;
  const fn_entry_t *e = fntab_get("no-such-function");
  assert_null(e);
}

static void test_fntab_overwrite_with_arg_names(void **state)
{
  (void)state;
  /* Register fn with arg_names, then overwrite → free_entry frees arg_names */
  char **names1 = malloc(2 * sizeof(char *));
  names1[0]     = strdup("x");
  names1[1]     = strdup("y");
  char *n1      = strdup("withargs");

  int rc = fntab_set(n1, names1, 2, NULL);
  assert_int_equal(rc, 0);

  /* Overwrite with a simpler registration */
  char *n2 = strdup("withargs");
  rc       = fntab_set(n2, NULL, 0, NULL);
  assert_int_equal(rc, 0);

  const fn_entry_t *e = fntab_get("withargs");
  assert_non_null(e);
  assert_int_equal(e->n_args, 0);
}

int main(void)
{
  const struct CMUnitTest tests[] = {
      cmocka_unit_test(test_fntab_set_and_get),
      cmocka_unit_test(test_fntab_overwrite),
      cmocka_unit_test(test_fntab_get_missing),
      cmocka_unit_test(test_fntab_overwrite_with_arg_names),
  };
  return cmocka_run_group_tests(tests, NULL, NULL);
}
