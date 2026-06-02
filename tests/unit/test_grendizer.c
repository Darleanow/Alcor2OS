#include "test_common.h"
#include "../../user/lib/grendizer.c"

static void test_gr_basename_with_slash(void **state)
{
  (void)state;
  assert_string_equal(gr__basename("foo/bar"), "bar");
}

static void test_gr_basename_no_slash(void **state)
{
  (void)state;
  assert_string_equal(gr__basename("foo"), "foo");
}

static void test_gr_basename_empty(void **state)
{
  (void)state;
  assert_string_equal(gr__basename(""), "program");
}

static void test_gr_basename_null(void **state)
{
  (void)state;
  assert_string_equal(gr__basename(NULL), "program");
}

static void test_gr_parse_flag_short(void **state)
{
  (void)state;
  int     verbose = 0;
  gr_opt  opts[]  = {GR_FLAG('v', "verbose", &verbose, "verbose"), GR_END};
  gr_spec spec    = {"prog", "usage", opts, NULL};
  gr_rest rest    = {0};
  char   *argv[]  = {"prog", "-v"};
  int rc = gr_parse(&spec, 2, argv, &rest, NULL, 0);
  assert_int_equal(rc, GR_OK);
  assert_int_equal(verbose, 1);
  assert_int_equal(rest.argc, 0);
}

static void test_gr_parse_flag_long(void **state)
{
  (void)state;
  int     verbose = 0;
  gr_opt  opts[]  = {GR_FLAG('v', "verbose", &verbose, "verbose"), GR_END};
  gr_spec spec    = {"prog", "usage", opts, NULL};
  gr_rest rest    = {0};
  char   *argv[]  = {"prog", "--verbose"};
  assert_int_equal(gr_parse(&spec, 2, argv, &rest, NULL, 0), GR_OK);
  assert_int_equal(verbose, 1);
}

static void test_gr_parse_count(void **state)
{
  (void)state;
  int     cnt    = 0;
  gr_opt  opts[] = {GR_COUNT('v', "verbose", &cnt, "verbosity"), GR_END};
  gr_spec spec   = {"prog", "usage", opts, NULL};
  gr_rest rest   = {0};
  char   *argv[] = {"prog", "-v", "-v", "-v"};
  assert_int_equal(gr_parse(&spec, 4, argv, &rest, NULL, 0), GR_OK);
  assert_int_equal(cnt, 3);
}

static void test_gr_parse_count_long(void **state)
{
  (void)state;
  int     cnt    = 0;
  gr_opt  opts[] = {GR_COUNT('v', "verbose", &cnt, "verbosity"), GR_END};
  gr_spec spec   = {"prog", "usage", opts, NULL};
  gr_rest rest   = {0};
  char   *argv[] = {"prog", "--verbose", "--verbose"};
  assert_int_equal(gr_parse(&spec, 3, argv, &rest, NULL, 0), GR_OK);
  assert_int_equal(cnt, 2);
}

static void test_gr_parse_str_short(void **state)
{
  (void)state;
  const char *out   = NULL;
  gr_opt      opts[] = {GR_STR('o', "output", &out, "PATH", "output"), GR_END};
  gr_spec     spec   = {"prog", "usage", opts, NULL};
  gr_rest     rest   = {0};
  char       *argv[] = {"prog", "-o", "myfile.txt"};
  assert_int_equal(gr_parse(&spec, 3, argv, &rest, NULL, 0), GR_OK);
  assert_string_equal(out, "myfile.txt");
}

static void test_gr_parse_str_long_eq(void **state)
{
  (void)state;
  const char *out   = NULL;
  gr_opt      opts[] = {GR_STR('o', "output", &out, "PATH", "output"), GR_END};
  gr_spec     spec   = {"prog", "usage", opts, NULL};
  gr_rest     rest   = {0};
  char       *argv[] = {"prog", "--output=myfile.txt"};
  assert_int_equal(gr_parse(&spec, 2, argv, &rest, NULL, 0), GR_OK);
  assert_string_equal(out, "myfile.txt");
}

static void test_gr_parse_str_long_space(void **state)
{
  (void)state;
  const char *out   = NULL;
  gr_opt      opts[] = {GR_STR('o', "output", &out, "PATH", "output"), GR_END};
  gr_spec     spec   = {"prog", "usage", opts, NULL};
  gr_rest     rest   = {0};
  char       *argv[] = {"prog", "--output", "myfile.txt"};
  assert_int_equal(gr_parse(&spec, 3, argv, &rest, NULL, 0), GR_OK);
  assert_string_equal(out, "myfile.txt");
}

static void test_gr_parse_str_value_missing(void **state)
{
  (void)state;
  const char *out   = NULL;
  gr_opt      opts[] = {GR_STR('o', "output", &out, "PATH", "output"), GR_END};
  gr_spec     spec   = {"prog", "usage", opts, NULL};
  gr_rest     rest   = {0};
  char        errbuf[128];
  char       *argv[] = {"prog", "--output"};
  assert_int_equal(gr_parse(&spec, 2, argv, &rest, errbuf, sizeof(errbuf)), GR_ERR);
}

static void test_gr_parse_int(void **state)
{
  (void)state;
  long    val    = 0;
  gr_opt  opts[] = {GR_INT('n', "count", &val, "N", "count"), GR_END};
  gr_spec spec   = {"prog", "usage", opts, NULL};
  gr_rest rest   = {0};
  char   *argv[] = {"prog", "-n", "42"};
  assert_int_equal(gr_parse(&spec, 3, argv, &rest, NULL, 0), GR_OK);
  assert_int_equal((int)val, 42);
}

static void test_gr_parse_int_negative(void **state)
{
  (void)state;
  long    val    = 0;
  gr_opt  opts[] = {GR_INT('n', "count", &val, "N", "count"), GR_END};
  gr_spec spec   = {"prog", "usage", opts, NULL};
  gr_rest rest   = {0};
  char   *argv[] = {"prog", "--count", "-7"};
  assert_int_equal(gr_parse(&spec, 3, argv, &rest, NULL, 0), GR_OK);
  assert_int_equal((int)val, -7);
}

static void test_gr_parse_int_invalid(void **state)
{
  (void)state;
  long    val    = 0;
  gr_opt  opts[] = {GR_INT('n', "count", &val, "N", "count"), GR_END};
  gr_spec spec   = {"prog", "usage", opts, NULL};
  gr_rest rest   = {0};
  char    errbuf[128];
  char   *argv[] = {"prog", "-n", "abc"};
  assert_int_equal(gr_parse(&spec, 3, argv, &rest, errbuf, sizeof(errbuf)), GR_ERR);
}

static void test_gr_parse_int_trailing_garbage(void **state)
{
  (void)state;
  long    val    = 0;
  gr_opt  opts[] = {GR_INT('n', "count", &val, "N", "count"), GR_END};
  gr_spec spec   = {"prog", "usage", opts, NULL};
  gr_rest rest   = {0};
  char    errbuf[128];
  char   *argv[] = {"prog", "-n", "42abc"};
  assert_int_equal(gr_parse(&spec, 3, argv, &rest, errbuf, sizeof(errbuf)), GR_ERR);
}

static void test_gr_parse_int_empty_value(void **state)
{
  (void)state;
  long    val    = 0;
  gr_opt  opts[] = {GR_INT('n', "count", &val, "N", "count"), GR_END};
  gr_spec spec   = {"prog", "usage", opts, NULL};
  gr_rest rest   = {0};
  char    errbuf[128];
  char   *argv[] = {"prog", "-n", ""};
  assert_int_equal(gr_parse(&spec, 3, argv, &rest, errbuf, sizeof(errbuf)), GR_ERR);
}

static void test_gr_parse_uint(void **state)
{
  (void)state;
  unsigned long val    = 0;
  gr_opt        opts[] = {GR_UINT('u', "uval", &val, "N", "uint"), GR_END};
  gr_spec       spec   = {"prog", "usage", opts, NULL};
  gr_rest       rest   = {0};
  char         *argv[] = {"prog", "-u", "100"};
  assert_int_equal(gr_parse(&spec, 3, argv, &rest, NULL, 0), GR_OK);
  assert_int_equal((int)val, 100);
}

static void test_gr_parse_uint_negative_rejected(void **state)
{
  (void)state;
  unsigned long val    = 0;
  gr_opt        opts[] = {GR_UINT('u', "uval", &val, "N", "uint"), GR_END};
  gr_spec       spec   = {"prog", "usage", opts, NULL};
  gr_rest       rest   = {0};
  char          errbuf[128];
  char         *argv[] = {"prog", "-u", "-5"};
  assert_int_equal(gr_parse(&spec, 3, argv, &rest, errbuf, sizeof(errbuf)), GR_ERR);
}

static void test_gr_parse_uint_empty_rejected(void **state)
{
  (void)state;
  unsigned long val    = 0;
  gr_opt        opts[] = {GR_UINT('u', "uval", &val, "N", "uint"), GR_END};
  gr_spec       spec   = {"prog", "usage", opts, NULL};
  gr_rest       rest   = {0};
  char          errbuf[128];
  char         *argv[] = {"prog", "-u", ""};
  assert_int_equal(gr_parse(&spec, 3, argv, &rest, errbuf, sizeof(errbuf)), GR_ERR);
}

static void test_gr_parse_uint_trailing_garbage(void **state)
{
  (void)state;
  unsigned long val    = 0;
  gr_opt        opts[] = {GR_UINT('u', "uval", &val, "N", "uint"), GR_END};
  gr_spec       spec   = {"prog", "usage", opts, NULL};
  gr_rest       rest   = {0};
  char          errbuf[128];
  char         *argv[] = {"prog", "-u", "42abc"};
  assert_int_equal(gr_parse(&spec, 3, argv, &rest, errbuf, sizeof(errbuf)), GR_ERR);
}

static void test_gr_parse_uint_out_of_range(void **state)
{
  (void)state;
  unsigned long val    = 0;
  gr_opt        opts[] = {GR_UINT('u', "uval", &val, "N", "uint"), GR_END};
  gr_spec       spec   = {"prog", "usage", opts, NULL};
  gr_rest       rest   = {0};
  char          errbuf[128];
  char         *argv[] = {"prog", "-u", "99999999999999999999"};
  assert_int_equal(gr_parse(&spec, 3, argv, &rest, errbuf, sizeof(errbuf)), GR_ERR);
}

static void test_gr_parse_float_basic(void **state)
{
  (void)state;
  double  val    = 0.0;
  gr_opt  opts[] = {GR_FLOAT('f', "fval", &val, "F", "float"), GR_END};
  gr_spec spec   = {"prog", "usage", opts, NULL};
  gr_rest rest   = {0};
  char   *argv[] = {"prog", "-f", "3.14"};
  assert_int_equal(gr_parse(&spec, 3, argv, &rest, NULL, 0), GR_OK);
  assert_true(val > 3.13 && val < 3.15);
}

static void test_gr_parse_float_negative(void **state)
{
  (void)state;
  double  val    = 0.0;
  gr_opt  opts[] = {GR_FLOAT('f', "fval", &val, "F", "float"), GR_END};
  gr_spec spec   = {"prog", "usage", opts, NULL};
  gr_rest rest   = {0};
  char   *argv[] = {"prog", "-f", "-1.5"};
  assert_int_equal(gr_parse(&spec, 3, argv, &rest, NULL, 0), GR_OK);
  assert_true(val < -1.4 && val > -1.6);
}

static void test_gr_parse_float_exponent(void **state)
{
  (void)state;
  double  val    = 0.0;
  gr_opt  opts[] = {GR_FLOAT('f', "fval", &val, "F", "float"), GR_END};
  gr_spec spec   = {"prog", "usage", opts, NULL};
  gr_rest rest   = {0};
  char   *argv[] = {"prog", "-f", "1e2"};
  assert_int_equal(gr_parse(&spec, 3, argv, &rest, NULL, 0), GR_OK);
  assert_true(val > 99.0 && val < 101.0);
}

static void test_gr_parse_float_neg_exponent(void **state)
{
  (void)state;
  double  val    = 0.0;
  gr_opt  opts[] = {GR_FLOAT('f', "fval", &val, "F", "float"), GR_END};
  gr_spec spec   = {"prog", "usage", opts, NULL};
  gr_rest rest   = {0};
  char   *argv[] = {"prog", "-f", "1e-1"};
  assert_int_equal(gr_parse(&spec, 3, argv, &rest, NULL, 0), GR_OK);
  assert_true(val > 0.09 && val < 0.11);
}

static void test_gr_parse_float_plus_sign(void **state)
{
  (void)state;
  double  val    = 0.0;
  gr_opt  opts[] = {GR_FLOAT('f', "fval", &val, "F", "float"), GR_END};
  gr_spec spec   = {"prog", "usage", opts, NULL};
  gr_rest rest   = {0};
  char   *argv[] = {"prog", "-f", "+2.5"};
  assert_int_equal(gr_parse(&spec, 3, argv, &rest, NULL, 0), GR_OK);
  assert_true(val > 2.4 && val < 2.6);
}

static void test_gr_parse_float_plus_exponent(void **state)
{
  (void)state;
  double  val    = 0.0;
  gr_opt  opts[] = {GR_FLOAT('f', "fval", &val, "F", "float"), GR_END};
  gr_spec spec   = {"prog", "usage", opts, NULL};
  gr_rest rest   = {0};
  char   *argv[] = {"prog", "-f", "1.5e+1"};
  assert_int_equal(gr_parse(&spec, 3, argv, &rest, NULL, 0), GR_OK);
  assert_true(val > 14.9 && val < 15.1);
}

static void test_gr_parse_float_invalid(void **state)
{
  (void)state;
  double  val    = 0.0;
  gr_opt  opts[] = {GR_FLOAT('f', "fval", &val, "F", "float"), GR_END};
  gr_spec spec   = {"prog", "usage", opts, NULL};
  gr_rest rest   = {0};
  char    errbuf[128];
  char   *argv[] = {"prog", "-f", "abc"};
  assert_int_equal(gr_parse(&spec, 3, argv, &rest, errbuf, sizeof(errbuf)), GR_ERR);
}

static void test_gr_parse_float_trailing_garbage(void **state)
{
  (void)state;
  double  val    = 0.0;
  gr_opt  opts[] = {GR_FLOAT('f', "fval", &val, "F", "float"), GR_END};
  gr_spec spec   = {"prog", "usage", opts, NULL};
  gr_rest rest   = {0};
  char    errbuf[128];
  char   *argv[] = {"prog", "-f", "1.0x"};
  assert_int_equal(gr_parse(&spec, 3, argv, &rest, errbuf, sizeof(errbuf)), GR_ERR);
}

static void test_gr_parse_float_empty(void **state)
{
  (void)state;
  double  val    = 0.0;
  gr_opt  opts[] = {GR_FLOAT('f', "fval", &val, "F", "float"), GR_END};
  gr_spec spec   = {"prog", "usage", opts, NULL};
  gr_rest rest   = {0};
  char    errbuf[128];
  char   *argv[] = {"prog", "-f", ""};
  assert_int_equal(gr_parse(&spec, 3, argv, &rest, errbuf, sizeof(errbuf)), GR_ERR);
}

static void test_gr_parse_float_malformed_exponent(void **state)
{
  (void)state;
  double  val    = 0.0;
  gr_opt  opts[] = {GR_FLOAT('f', "fval", &val, "F", "float"), GR_END};
  gr_spec spec   = {"prog", "usage", opts, NULL};
  gr_rest rest   = {0};
  char    errbuf[128];
  char   *argv[] = {"prog", "-f", "1eX"};
  assert_int_equal(gr_parse(&spec, 3, argv, &rest, errbuf, sizeof(errbuf)), GR_ERR);
}

static void test_gr_parse_float_huge_exponent(void **state)
{
  (void)state;
  double  val    = 0.0;
  gr_opt  opts[] = {GR_FLOAT('f', "fval", &val, "F", "float"), GR_END};
  gr_spec spec   = {"prog", "usage", opts, NULL};
  gr_rest rest   = {0};
  char    errbuf[128];
  char   *argv[] = {"prog", "-f", "1e1000001"};
  assert_int_equal(gr_parse(&spec, 3, argv, &rest, errbuf, sizeof(errbuf)), GR_ERR);
}

static void test_gr_parse_positionals(void **state)
{
  (void)state;
  gr_opt  opts[] = {GR_END};
  gr_spec spec   = {"prog", "usage", opts, NULL};
  gr_rest rest   = {0};
  char   *argv[] = {"prog", "a", "b", "c"};
  assert_int_equal(gr_parse(&spec, 4, argv, &rest, NULL, 0), GR_OK);
  assert_int_equal(rest.argc, 3);
  assert_string_equal(rest.argv[0], "a");
}

static void test_gr_parse_double_dash(void **state)
{
  (void)state;
  int     flag   = 0;
  gr_opt  opts[] = {GR_FLAG('v', "verbose", &flag, "verbose"), GR_END};
  gr_spec spec   = {"prog", "usage", opts, NULL};
  gr_rest rest   = {0};
  char   *argv[] = {"prog", "--", "-v"};
  assert_int_equal(gr_parse(&spec, 3, argv, &rest, NULL, 0), GR_OK);
  assert_int_equal(flag, 0);
  assert_string_equal(rest.argv[0], "-v");
}

static void test_gr_parse_too_many_positionals(void **state)
{
  (void)state;
  gr_opt  opts[] = {GR_END};
  gr_spec spec   = {"prog", "usage", opts, NULL};
  gr_rest rest   = {0};
  char    errbuf[256];
  static char *argv[515];
  argv[0] = "prog";
  for(int i = 1; i <= 513; i++) argv[i] = "x";
  argv[514] = NULL;
  assert_int_equal(gr_parse(&spec, 514, argv, &rest, errbuf, sizeof(errbuf)), GR_ERR);
}

static void test_gr_parse_help_short(void **state)
{
  (void)state;
  gr_opt  opts[] = {GR_END};
  gr_spec spec   = {"prog", "usage", opts, NULL};
  gr_rest rest   = {0};
  char   *argv[] = {"prog", "-h"};
  assert_int_equal(gr_parse(&spec, 2, argv, &rest, NULL, 0), GR_HELP);
}

static void test_gr_parse_help_long(void **state)
{
  (void)state;
  gr_opt  opts[] = {GR_END};
  gr_spec spec   = {"prog", "usage", opts, NULL};
  gr_rest rest   = {0};
  char   *argv[] = {"prog", "--help"};
  assert_int_equal(gr_parse(&spec, 2, argv, &rest, NULL, 0), GR_HELP);
}

static void test_gr_parse_unknown_short(void **state)
{
  (void)state;
  gr_opt  opts[] = {GR_END};
  gr_spec spec   = {"prog", "usage", opts, NULL};
  gr_rest rest   = {0};
  char    errbuf[128];
  char   *argv[] = {"prog", "-z"};
  assert_int_equal(gr_parse(&spec, 2, argv, &rest, errbuf, sizeof(errbuf)), GR_ERR);
}

static void test_gr_parse_unknown_long(void **state)
{
  (void)state;
  gr_opt  opts[] = {GR_END};
  gr_spec spec   = {"prog", "usage", opts, NULL};
  gr_rest rest   = {0};
  char    errbuf[128];
  char   *argv[] = {"prog", "--no-such-option"};
  assert_int_equal(gr_parse(&spec, 2, argv, &rest, errbuf, sizeof(errbuf)), GR_ERR);
}

static void test_gr_parse_value_required_missing(void **state)
{
  (void)state;
  long    val    = 0;
  gr_opt  opts[] = {GR_INT('n', "count", &val, "N", "count"), GR_END};
  gr_spec spec   = {"prog", "usage", opts, NULL};
  gr_rest rest   = {0};
  char    errbuf[128];
  char   *argv[] = {"prog", "-n"};
  assert_int_equal(gr_parse(&spec, 2, argv, &rest, errbuf, sizeof(errbuf)), GR_ERR);
}

static void test_gr_parse_flag_with_eq_rejected(void **state)
{
  (void)state;
  int     flag   = 0;
  gr_opt  opts[] = {GR_FLAG('v', "verbose", &flag, "verbose"), GR_END};
  gr_spec spec   = {"prog", "usage", opts, NULL};
  gr_rest rest   = {0};
  char    errbuf[128];
  char   *argv[] = {"prog", "--verbose=yes"};
  assert_int_equal(gr_parse(&spec, 2, argv, &rest, errbuf, sizeof(errbuf)), GR_ERR);
}

static void test_gr_parse_null_args(void **state)
{
  (void)state;
  gr_opt  opts[] = {GR_END};
  gr_spec spec   = {"prog", "usage", opts, NULL};
  gr_rest rest   = {0};
  assert_int_equal(gr_parse(NULL, 1, (char *[]){(char *)"prog"}, &rest, NULL, 0), GR_ERR);
  assert_int_equal(gr_parse(&spec, 1, NULL, &rest, NULL, 0), GR_ERR);
  assert_int_equal(gr_parse(&spec, 0, (char *[]){(char *)"prog"}, &rest, NULL, 0), GR_ERR);
}

static void test_gr_parse_error_truncated(void **state)
{
  (void)state;
  gr_opt  opts[] = {GR_END};
  gr_spec spec   = {"prog", "usage", opts, NULL};
  gr_rest rest   = {0};
  char    tiny[8];
  char   *argv[] = {"prog", "--no-such-very-long-option-name"};
  assert_int_equal(gr_parse(&spec, 2, argv, &rest, tiny, sizeof(tiny)), GR_ERR);
  assert_true(strlen(tiny) > 0);
}

static void test_gr_parse_progname_from_argv0(void **state)
{
  (void)state;
  gr_opt  opts[] = {GR_END};
  gr_spec spec   = {NULL, "usage", opts, NULL};
  gr_rest rest   = {0};
  char   *argv[] = {"/usr/bin/myprog"};
  assert_int_equal(gr_parse(&spec, 1, argv, &rest, NULL, 0), GR_OK);
}

static void test_gr_parse_clustered_flags(void **state)
{
  (void)state;
  int     a = 0, b = 0, c = 0;
  gr_opt  opts[] = {
      GR_FLAG('a', NULL, &a, "a"), GR_FLAG('b', NULL, &b, "b"),
      GR_FLAG('c', NULL, &c, "c"), GR_END};
  gr_spec spec   = {"prog", "usage", opts, NULL};
  gr_rest rest   = {0};
  char   *argv[] = {"prog", "-abc"};
  assert_int_equal(gr_parse(&spec, 2, argv, &rest, NULL, 0), GR_OK);
  assert_int_equal(a, 1); assert_int_equal(b, 1); assert_int_equal(c, 1);
}

static void test_gr_parse_mixed_cluster_with_value(void **state)
{
  (void)state;
  int     flag   = 0;
  long    val    = 0;
  gr_opt  opts[] = {
      GR_FLAG('v', NULL, &flag, "verbose"),
      GR_INT('n', NULL, &val, "N", "count"), GR_END};
  gr_spec spec   = {"prog", "usage", opts, NULL};
  gr_rest rest   = {0};
  char   *argv[] = {"prog", "-vn42"};
  assert_int_equal(gr_parse(&spec, 2, argv, &rest, NULL, 0), GR_OK);
  assert_int_equal(flag, 1); assert_int_equal((int)val, 42);
}

static void test_gr_parse_apply_flag_rc_error(void **state)
{
  (void)state;
  long    val    = 0;
  gr_opt  opts[] = {GR_INT('n', "count", &val, "N", "count"), GR_END};
  gr_spec spec   = {"prog", "usage", opts, NULL};
  gr_rest rest   = {0};
  char    errbuf[128];
  char   *argv[] = {"prog", "--count=notanumber"};
  assert_int_equal(gr_parse(&spec, 2, argv, &rest, errbuf, sizeof(errbuf)), GR_ERR);
}

static void test_gr_parse_apply_value_rc_error_short(void **state)
{
  (void)state;
  long    val    = 0;
  gr_opt  opts[] = {GR_INT('n', "count", &val, "N", "count"), GR_END};
  gr_spec spec   = {"prog", "usage", opts, NULL};
  gr_rest rest   = {0};
  char    errbuf[128];
  char   *argv[] = {"prog", "-nbad"};
  assert_int_equal(gr_parse(&spec, 2, argv, &rest, errbuf, sizeof(errbuf)), GR_ERR);
}

static void test_gr_usage_no_crash(void **state)
{
  (void)state;
  int     flag   = 0;
  long    val    = 0;
  gr_opt  opts[] = {
      GR_FLAG('v', "verbose", &flag, "Be verbose"),
      GR_INT('n', "count", &val, "N", "Repeat N times"), GR_END};
  gr_spec spec = {"mytool", "[options] <input>", opts, "Examples:\n  mytool -v"};
  gr_usage(&spec, NULL);
  gr_usage(&spec, stdout);
}

static void test_gr_usage_no_options(void **state)
{
  (void)state;
  gr_opt  opts[] = {GR_END};
  gr_spec spec   = {"tool", "[args]", opts, NULL};
  gr_usage(&spec, NULL);
}

static void test_gr_usage_with_epilog(void **state)
{
  (void)state;
  long    val    = 0;
  gr_opt  opts[] = {GR_INT('n', "count", &val, "N", "count"), GR_END};
  gr_spec spec   = {"tool", "[opts]", opts, "See docs at example.com"};
  gr_usage(&spec, stdout);
}

static void test_gr_usage_long_option_col_capped(void **state)
{
  (void)state;
  int     flag = 0;
  gr_opt  opts[] = {
      GR_FLAG(0, "this-is-a-very-long-option-name-exceeding-thirty-two-chars",
              &flag, "help"),
      GR_END};
  gr_spec spec = {"prog", "usage", opts, NULL};
  gr_usage(&spec, stdout);
}

static int run_hello(void *ud, int argc, char **argv)
{
  (void)ud; (void)argc; (void)argv; return 7;
}
static int run_world(void *ud, int argc, char **argv)
{
  (void)ud; (void)argc; (void)argv; return 8;
}
static int run_child(void *ud, int argc, char **argv)
{
  (void)ud; (void)argc; (void)argv; return 99;
}

static void test_gr_dispatch_leaf(void **state)
{
  (void)state;
  gr_cmd cmds[] = {{"hello", NULL, NULL, run_hello, NULL, 0},
                   {"world", NULL, NULL, run_world, NULL, 0}};
  gr_app app   = {"prog", "blurb", cmds, 2, NULL};
  char  *argv[] = {"prog", "hello"};
  assert_int_equal(gr_dispatch(&app, 2, argv), 7);
}

static void test_gr_dispatch_second_leaf(void **state)
{
  (void)state;
  gr_cmd cmds[] = {{"hello", NULL, NULL, run_hello, NULL, 0},
                   {"world", NULL, NULL, run_world, NULL, 0}};
  gr_app app   = {"prog", "blurb", cmds, 2, NULL};
  char  *argv[] = {"prog", "world"};
  assert_int_equal(gr_dispatch(&app, 2, argv), 8);
}

static void test_gr_dispatch_no_args_shows_help(void **state)
{
  (void)state;
  gr_cmd cmds[] = {{"hello", "say hello", NULL, run_hello, NULL, 0}};
  gr_app app    = {"prog", "blurb", cmds, 1, NULL};
  char  *argv[] = {"prog"};
  assert_int_equal(gr_dispatch(&app, 1, argv), 0);
}

static void test_gr_dispatch_unknown_cmd(void **state)
{
  (void)state;
  gr_cmd cmds[] = {{"hello", "say hello", NULL, run_hello, NULL, 0}};
  gr_app app    = {"prog", "blurb", cmds, 1, NULL};
  char  *argv[] = {"prog", "nope"};
  assert_int_equal(gr_dispatch(&app, 2, argv), 2);
}

static void test_gr_dispatch_help_keyword(void **state)
{
  (void)state;
  gr_cmd cmds[] = {{"hello", "say hello", "details", run_hello, NULL, 0}};
  gr_app app    = {"prog", "blurb", cmds, 1, NULL};
  char  *argv[] = {"prog", "help"};
  assert_int_equal(gr_dispatch(&app, 2, argv), 0);
}

static void test_gr_dispatch_help_subcommand(void **state)
{
  (void)state;
  gr_cmd cmds[] = {{"hello", "say hello", "details", run_hello, NULL, 0}};
  gr_app app    = {"prog", "blurb", cmds, 1, NULL};
  char  *argv[] = {"prog", "help", "hello"};
  assert_int_equal(gr_dispatch(&app, 3, argv), 0);
}

static void test_gr_dispatch_inline_help_flag(void **state)
{
  (void)state;
  gr_cmd cmds[] = {{"hello", "say hello", "details", run_hello, NULL, 0}};
  gr_app app    = {"prog", "blurb", cmds, 1, NULL};
  char  *argv[] = {"prog", "hello", "--help"};
  assert_int_equal(gr_dispatch(&app, 3, argv), 0);
}

static void test_gr_dispatch_top_help_flag(void **state)
{
  (void)state;
  gr_cmd cmds[] = {{"hello", "say hello", NULL, run_hello, NULL, 0}};
  gr_app app    = {"prog", "blurb", cmds, 1, NULL};
  char  *argv[] = {"prog", "--help"};
  assert_int_equal(gr_dispatch(&app, 2, argv), 0);
}

static void test_gr_dispatch_null_args(void **state)
{
  (void)state;
  gr_cmd cmds[] = {{"hello", "say hello", NULL, run_hello, NULL, 0}};
  gr_app app    = {"prog", "blurb", cmds, 1, NULL};
  assert_int_equal(gr_dispatch(NULL, 1, (char *[]){(char *)"prog"}), 2);
  assert_int_equal(gr_dispatch(&app, 1, NULL), 2);
}

static void test_gr_dispatch_no_commands(void **state)
{
  (void)state;
  gr_app app   = {"prog", "blurb", NULL, 0, NULL};
  char  *argv[] = {"prog", "hello"};
  assert_int_equal(gr_dispatch(&app, 2, argv), 2);
}

static void test_gr_dispatch_nested(void **state)
{
  (void)state;
  gr_cmd children[] = {{"sub", "sub", NULL, run_child, NULL, 0}};
  gr_cmd cmds[]     = {{"parent", "parent", NULL, NULL, children, 1}};
  gr_app app        = {"prog", "blurb", cmds, 1, NULL};
  char  *argv[]     = {"prog", "parent", "sub"};
  assert_int_equal(gr_dispatch(&app, 3, argv), 99);
}

static void test_gr_dispatch_nested_help(void **state)
{
  (void)state;
  gr_cmd children[] = {{"sub", "sub", "details", run_child, NULL, 0}};
  gr_cmd cmds[]     = {{"parent", "parent", NULL, NULL, children, 1}};
  gr_app app        = {"prog", "blurb", cmds, 1, NULL};
  char  *argv[]     = {"prog", "help", "parent", "sub"};
  assert_int_equal(gr_dispatch(&app, 4, argv), 0);
}

static void test_gr_dispatch_nested_unknown_sub(void **state)
{
  (void)state;
  gr_cmd children[] = {{"sub", "sub", NULL, run_child, NULL, 0}};
  gr_cmd cmds[]     = {{"parent", "parent", NULL, NULL, children, 1}};
  gr_app app        = {"prog", "blurb", cmds, 1, NULL};
  char  *argv[]     = {"prog", "parent", "nope"};
  assert_int_equal(gr_dispatch(&app, 3, argv), 2);
}

static void test_gr_dispatch_parent_group_no_args(void **state)
{
  (void)state;
  gr_cmd children[] = {{"sub", "sub", "details", run_child, NULL, 0}};
  gr_cmd cmds[]     = {{"parent", "parent", "details", NULL, children, 1}};
  gr_app app        = {"prog", "blurb", cmds, 1, NULL};
  char  *argv[]     = {"prog", "parent"};
  assert_int_equal(gr_dispatch(&app, 2, argv), 0);
}

static void test_gr_dispatch_help_unknown_subcmd(void **state)
{
  (void)state;
  gr_cmd cmds[] = {{"hello", "say hello", "details", run_hello, NULL, 0}};
  gr_app app    = {"prog", "blurb", cmds, 1, NULL};
  char  *argv[] = {"prog", "help", "nonexistent"};
  assert_int_equal(gr_dispatch(&app, 3, argv), 2);
}

static void test_gr_dispatch_help_no_cmd_arg(void **state)
{
  (void)state;
  gr_cmd cmds[] = {{"hello", "say hello", "details", run_hello, NULL, 0}};
  gr_app app    = {"prog", "blurb", cmds, 1, NULL};
  char  *argv[] = {"prog", "help"};
  assert_int_equal(gr_dispatch(&app, 2, argv), 0);
}

static void test_gr_dispatch_help_leaf_no_subcommands(void **state)
{
  (void)state;
  gr_cmd children[] = {{"sub", "sub", NULL, run_child, NULL, 0}};
  gr_cmd cmds[]     = {{"parent", "parent", NULL, NULL, children, 1}};
  gr_app app        = {"prog", "blurb", cmds, 1, NULL};
  char  *argv[]     = {"prog", "help", "parent", "sub", "extra"};
  assert_int_equal(gr_dispatch(&app, 5, argv), 2);
}

static void test_gr_dispatch_app_prog_fallback(void **state)
{
  (void)state;
  gr_cmd cmds[] = {{"hello", "say hello", NULL, run_hello, NULL, 0}};
  gr_app app    = {NULL, "blurb", cmds, 1, NULL};
  char  *argv[] = {"/usr/bin/myprog", "hello"};
  assert_int_equal(gr_dispatch(&app, 2, argv), 7);
}

static void test_gr_dispatch_leaf_no_run_handler(void **state)
{
  (void)state;
  gr_cmd cmds[] = {{"broken", "broken", NULL, NULL, NULL, 0}};
  gr_app app    = {"prog", "blurb", cmds, 1, NULL};
  char  *argv[] = {"prog", "broken"};
  assert_int_equal(gr_dispatch(&app, 2, argv), 2);
}

static void test_gr_print_group_col_capped(void **state)
{
  (void)state;
  gr_cmd cmds[] = {
      {"this-name-is-longer-than-twenty-four-characters", "long", NULL,
       run_hello, NULL, 0},
  };
  gr_app app   = {"prog", "blurb", cmds, 1, NULL};
  char  *argv[] = {"prog"};
  assert_int_equal(gr_dispatch(&app, 1, argv), 0);
}

static void test_gr_find_cmd_null_name_entry(void **state)
{
  (void)state;
  gr_cmd cmds[] = {
      {NULL, "sentinel", NULL, run_hello, NULL, 0},
      {"real", "real", NULL, run_hello, NULL, 0},
  };
  gr_app app   = {"prog", "blurb", cmds, 2, NULL};
  char  *argv[] = {"prog", "real"};
  assert_int_equal(gr_dispatch(&app, 2, argv), 7);
}

static void test_gr_print_group_null_name_skipped(void **state)
{
  (void)state;
  gr_cmd cmds[] = {
      {NULL, "skipped", NULL, NULL, NULL, 0},
      {"real", "real", NULL, run_hello, NULL, 0},
  };
  gr_app app   = {"prog", "blurb", cmds, 2, NULL};
  char  *argv[] = {"prog"};
  assert_int_equal(gr_dispatch(&app, 1, argv), 0);
}

static void test_gr_dispatch_parent_help_flag_argc1(void **state)
{
  (void)state;
  gr_cmd children[] = {{"sub", "sub", "details", run_child, NULL, 0}};
  gr_cmd cmds[]     = {{"parent", "parent", "details", NULL, children, 1}};
  gr_app app        = {"prog", "blurb", cmds, 1, NULL};
  char  *argv[]     = {"prog", "parent", "--help"};
  assert_int_equal(gr_dispatch(&app, 3, argv), 0);
}

static void test_gr_dispatch_help_walk_no_arg(void **state)
{
  (void)state;
  gr_cmd children[] = {{"sub", "sub", NULL, run_child, NULL, 0}};
  gr_cmd cmds[]     = {{"parent", "parent", NULL, NULL, children, 1}};
  gr_app app        = {"prog", "blurb", cmds, 1, NULL};
  char  *argv[]     = {"prog", "help", "parent"};
  assert_int_equal(gr_dispatch(&app, 3, argv), 0);
}

int main(void)
{
  const struct CMUnitTest tests[] = {
      cmocka_unit_test(test_gr_basename_with_slash),
      cmocka_unit_test(test_gr_basename_no_slash),
      cmocka_unit_test(test_gr_basename_empty),
      cmocka_unit_test(test_gr_basename_null),
      cmocka_unit_test(test_gr_parse_flag_short),
      cmocka_unit_test(test_gr_parse_flag_long),
      cmocka_unit_test(test_gr_parse_count),
      cmocka_unit_test(test_gr_parse_count_long),
      cmocka_unit_test(test_gr_parse_str_short),
      cmocka_unit_test(test_gr_parse_str_long_eq),
      cmocka_unit_test(test_gr_parse_str_long_space),
      cmocka_unit_test(test_gr_parse_str_value_missing),
      cmocka_unit_test(test_gr_parse_int),
      cmocka_unit_test(test_gr_parse_int_negative),
      cmocka_unit_test(test_gr_parse_int_invalid),
      cmocka_unit_test(test_gr_parse_int_trailing_garbage),
      cmocka_unit_test(test_gr_parse_int_empty_value),
      cmocka_unit_test(test_gr_parse_uint),
      cmocka_unit_test(test_gr_parse_uint_negative_rejected),
      cmocka_unit_test(test_gr_parse_uint_empty_rejected),
      cmocka_unit_test(test_gr_parse_uint_trailing_garbage),
      cmocka_unit_test(test_gr_parse_uint_out_of_range),
      cmocka_unit_test(test_gr_parse_float_basic),
      cmocka_unit_test(test_gr_parse_float_negative),
      cmocka_unit_test(test_gr_parse_float_exponent),
      cmocka_unit_test(test_gr_parse_float_neg_exponent),
      cmocka_unit_test(test_gr_parse_float_plus_sign),
      cmocka_unit_test(test_gr_parse_float_plus_exponent),
      cmocka_unit_test(test_gr_parse_float_invalid),
      cmocka_unit_test(test_gr_parse_float_trailing_garbage),
      cmocka_unit_test(test_gr_parse_float_empty),
      cmocka_unit_test(test_gr_parse_float_malformed_exponent),
      cmocka_unit_test(test_gr_parse_float_huge_exponent),
      cmocka_unit_test(test_gr_parse_positionals),
      cmocka_unit_test(test_gr_parse_double_dash),
      cmocka_unit_test(test_gr_parse_too_many_positionals),
      cmocka_unit_test(test_gr_parse_help_short),
      cmocka_unit_test(test_gr_parse_help_long),
      cmocka_unit_test(test_gr_parse_unknown_short),
      cmocka_unit_test(test_gr_parse_unknown_long),
      cmocka_unit_test(test_gr_parse_value_required_missing),
      cmocka_unit_test(test_gr_parse_flag_with_eq_rejected),
      cmocka_unit_test(test_gr_parse_null_args),
      cmocka_unit_test(test_gr_parse_error_truncated),
      cmocka_unit_test(test_gr_parse_progname_from_argv0),
      cmocka_unit_test(test_gr_parse_clustered_flags),
      cmocka_unit_test(test_gr_parse_mixed_cluster_with_value),
      cmocka_unit_test(test_gr_parse_apply_flag_rc_error),
      cmocka_unit_test(test_gr_parse_apply_value_rc_error_short),
      cmocka_unit_test(test_gr_usage_no_crash),
      cmocka_unit_test(test_gr_usage_no_options),
      cmocka_unit_test(test_gr_usage_with_epilog),
      cmocka_unit_test(test_gr_usage_long_option_col_capped),
      cmocka_unit_test(test_gr_dispatch_leaf),
      cmocka_unit_test(test_gr_dispatch_second_leaf),
      cmocka_unit_test(test_gr_dispatch_no_args_shows_help),
      cmocka_unit_test(test_gr_dispatch_unknown_cmd),
      cmocka_unit_test(test_gr_dispatch_help_keyword),
      cmocka_unit_test(test_gr_dispatch_help_subcommand),
      cmocka_unit_test(test_gr_dispatch_inline_help_flag),
      cmocka_unit_test(test_gr_dispatch_top_help_flag),
      cmocka_unit_test(test_gr_dispatch_null_args),
      cmocka_unit_test(test_gr_dispatch_no_commands),
      cmocka_unit_test(test_gr_dispatch_nested),
      cmocka_unit_test(test_gr_dispatch_nested_help),
      cmocka_unit_test(test_gr_dispatch_nested_unknown_sub),
      cmocka_unit_test(test_gr_dispatch_parent_group_no_args),
      cmocka_unit_test(test_gr_dispatch_help_unknown_subcmd),
      cmocka_unit_test(test_gr_dispatch_help_no_cmd_arg),
      cmocka_unit_test(test_gr_dispatch_help_leaf_no_subcommands),
      cmocka_unit_test(test_gr_dispatch_app_prog_fallback),
      cmocka_unit_test(test_gr_dispatch_leaf_no_run_handler),
      cmocka_unit_test(test_gr_print_group_col_capped),
      cmocka_unit_test(test_gr_find_cmd_null_name_entry),
      cmocka_unit_test(test_gr_print_group_null_name_skipped),
      cmocka_unit_test(test_gr_dispatch_parent_help_flag_argc1),
      cmocka_unit_test(test_gr_dispatch_help_walk_no_arg),
  };
  return cmocka_run_group_tests(tests, NULL, NULL);
}
