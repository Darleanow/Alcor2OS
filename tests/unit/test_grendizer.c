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

/* gr__find_cmd: null name → returns NULL (line 529) */
static void test_gr_find_cmd_null_name_returns_null(void **state) {
  (void)state;
  gr_cmd cmds[] = {{"foo", NULL, NULL, NULL, NULL, 0}};
  const gr_cmd *r = gr__find_cmd(cmds, 1, NULL);
  assert_null(r);
}

/* gr__path_join: null/empty seg → early return (line 541) */
static void test_gr_path_join_null_seg_noop(void **state) {
  (void)state;
  char dst[64] = "original";
  gr__path_join(dst, sizeof(dst), "prefix", NULL);
  /* dst should be unchanged since seg is NULL */
  assert_string_equal(dst, "original");
  gr__path_join(dst, sizeof(dst), "prefix", "");
  assert_string_equal(dst, "original");
}

/* gr_parse: --flag with null storage → GR_ERR propagated (line 435) */
static void test_gr_parse_flag_null_storage_propagates_error(void **state) {
  (void)state;
  /* FLAG option with storage=NULL → gr__apply_flag returns GR_ERR → line 435 */
  gr_opt opts[] = {
    {.kind = GR_KIND_FLAG, .short_name = 'v', .long_name = "verbose",
     .storage = NULL, .help = "verbose"},
    GR_END
  };
  gr_spec spec = {"prog", "usage", opts, NULL};
  gr_rest rest = {0};
  char errbuf[64];
  char *argv[] = {"prog", "--verbose"};
  int rc = gr_parse(&spec, 2, argv, &rest, errbuf, sizeof(errbuf));
  assert_int_equal(rc, GR_ERR);
}

static int help_walk_dummy_run(void *ud, int argc, char **argv) {
  (void)argc; (void)argv; (void)ud; return 0;
}

/* gr_dispatch: "help" with no further args → "requires a command name" (lines 616-618) */
static void test_gr_dispatch_help_walk_argc_zero(void **state) {
  (void)state;
  gr_cmd subcmds[] = {{"sub", "A subcommand", NULL, help_walk_dummy_run, NULL, 0}};
  gr_app app = {
    .program       = "prog",
    .blurb         = "blurb",
    .commands      = subcmds,
    .command_count = 1,
    .userdata      = NULL
  };
  /* "help" alone → general help, returns 0 (already shows help) */
  char *argv[] = {"prog", "help"};
  int rc = gr_dispatch(&app, 2, argv);
  assert_int_equal(rc, 0);

  /* "help sub" where "sub" is a leaf command with no subcommands:
   * gr__help_walk called → prints help for "sub" → returns 0 */
  char *argv2[] = {"prog", "help", "sub"};
  rc = gr_dispatch(&app, 3, argv2);
  assert_int_equal(rc, 0);
}

static void test_gr_needs_value_true_via_usage(void **state) {
  (void)state;
  long          n   = 0;
  double        f   = 0.0;
  unsigned long u   = 0;
  const char   *s   = NULL;
  gr_opt opts[] = {
      GR_INT  ('n', "num",   &n, "N",   "int opt"),
      GR_FLOAT('f', "float", &f, "F",   "float opt"),
      GR_UINT ('u', "uint",  &u, "U",   "uint opt"),
      GR_STR  ('s', "str",   &s, "STR", "str opt"),
      GR_END,
  };
  gr_spec spec = {"prog", "[opts]", opts, NULL};
  gr_usage(&spec, stdout);
}

/* gr__parse_int: null text → GR_ERR (branch !text) */
static void test_gr_parse_int_null_text(void **state) {
  (void)state;
  long n = 0;
  char buf[64];
  assert_int_equal(gr__parse_int(NULL, &n, buf, sizeof buf, "-n"), GR_ERR);
}

/* gr__parse_int: ERANGE (overflow) → GR_ERR */
static void test_gr_parse_int_overflow(void **state) {
  (void)state;
  long n = 0;
  char buf[64];
  assert_int_equal(
      gr__parse_int("99999999999999999999999999", &n, buf, sizeof buf, "-n"),
      GR_ERR
  );
}

/* gr__parse_uint: null text → GR_ERR */
static void test_gr_parse_uint_null_text(void **state) {
  (void)state;
  unsigned long n = 0;
  char          buf[64];
  assert_int_equal(gr__parse_uint(NULL, &n, buf, sizeof buf, "-u"), GR_ERR);
}

/* gr__parse_uint: end == text (no digit consumed) → GR_ERR */
static void test_gr_parse_uint_no_digit(void **state) {
  (void)state;
  unsigned long n = 0;
  char          buf[64];
  /* strtoul on "abc" sets end==text (no leading digits) */
  assert_int_equal(gr__parse_uint("abc", &n, buf, sizeof buf, "-u"), GR_ERR);
}

/* gr__parse_float: null text → GR_ERR */
static void test_gr_parse_float_null_text(void **state) {
  (void)state;
  double f = 0.0;
  char   buf[64];
  assert_int_equal(gr__parse_float(NULL, &f, buf, sizeof buf, "-f"), GR_ERR);
}

/* gr__parse_float: 'E' uppercase exponent → OK */
static void test_gr_parse_float_uppercase_exponent(void **state) {
  (void)state;
  double f = 0.0;
  char   buf[64];
  assert_int_equal(gr__parse_float("2E3", &f, buf, sizeof buf, "-f"), GR_OK);
  assert_true(f > 1999.0 && f < 2001.0);
}

/* gr__parse_float: '+' exponent sign → OK */
static void test_gr_parse_float_explicit_plus_exp(void **state) {
  (void)state;
  double f = 0.0;
  char   buf[64];
  assert_int_equal(gr__parse_float("1e+2", &f, buf, sizeof buf, "-f"), GR_OK);
  assert_true(f > 99.0 && f < 101.0);
}

/* gr_parse: !rest null → GR_ERR (third null-check branch) */
static void test_gr_parse_null_rest_direct(void **state) {
  (void)state;
  gr_opt  opts[] = {GR_END};
  gr_spec spec   = {"prog", NULL, opts, NULL};
  char   *argv[] = {"prog"};
  char    err[64];
  assert_int_equal(gr_parse(&spec, 1, argv, NULL, err, sizeof err), GR_ERR);
}

/* gr_parse: single '-' token → positional (tok[1]=='\0' branch) */
static void test_gr_parse_single_dash_positional(void **state) {
  (void)state;
  gr_opt  opts[] = {GR_END};
  gr_spec spec   = {"prog", NULL, opts, NULL};
  gr_rest rest   = {0};
  char   *argv[] = {"prog", "-"};
  assert_int_equal(gr_parse(&spec, 2, argv, &rest, NULL, 0), GR_OK);
  assert_int_equal(rest.argc, 1);
  assert_string_equal(rest.argv[0], "-");
}

/* gr__path_join: empty prefix → snprintf(dst, cap, "%s", seg) branch */
static void test_gr_path_join_empty_prefix(void **state) {
  (void)state;
  char dst[64] = {0};
  gr__path_join(dst, sizeof dst, "", "myseg");
  assert_string_equal(dst, "myseg");
  char dst2[64] = {0};
  gr__path_join(dst2, sizeof dst2, NULL, "myseg");
  assert_string_equal(dst2, "myseg");
}

/* gr__help_walk: argc==0 → "requires a command name" → returns 2 */
static void test_gr_help_walk_argc_zero_direct(void **state) {
  (void)state;
  gr_cmd cmds[] = {{"sub", "sub", NULL, help_walk_dummy_run, NULL, 0}};
  gr_app app    = {"prog", NULL, cmds, 1, NULL};
  /* call gr__help_walk indirectly: gr_dispatch("help") with argc==1 passes
   * argc-1==0 to gr__help_walk — but gr_dispatch handles "help" alone as
   * top-level listing. Call directly instead. */
  char  buf[64];
  char *argv_empty[] = {};
  int   r = gr__help_walk("prog", &app, cmds, 1, "", 0, argv_empty);
  assert_int_equal(r, 2);
  /* verify the error message was printed to stderr — return code is enough */
  (void)buf;
}

/* gr__by_long: option with null long_name → skipped (long_name==NULL branch) */
static void test_gr_by_long_null_longname(void **state) {
  (void)state;
  int    v = 0;
  gr_opt opts[] = {
      /* short-only option: long_name is NULL → gr__by_long skips it */
      GR_FLAG('v', NULL, &v, "v"),
      GR_END,
  };
  gr_spec spec   = {"prog", NULL, opts, NULL};
  gr_rest rest   = {0};
  char   *argv[] = {"prog", "--verbose"};
  char    err[64];
  /* --verbose not found → GR_ERR (exercises the long_name==NULL branch) */
  assert_int_equal(gr_parse(&spec, 2, argv, &rest, err, sizeof err), GR_ERR);
}

/* gr__by_long: long name that shares a prefix but is longer → no match */
static void test_gr_by_long_prefix_no_match(void **state) {
  (void)state;
  int    v = 0;
  gr_opt opts[] = {GR_FLAG('v', "verb", &v, "v"), GR_END};
  gr_spec spec  = {"prog", NULL, opts, NULL};
  gr_rest rest  = {0};
  char    err[64];
  /* "verbose" shares prefix "verb" but is longer → opts->long_name[len]!= '\0'
   * → no match → unknown option → GR_ERR */
  char *argv[] = {"prog", "--verbose"};
  assert_int_equal(gr_parse(&spec, 2, argv, &rest, err, sizeof err), GR_ERR);
}

/* gr_dispatch leaf: inline -h help flag */
static void test_gr_dispatch_leaf_dash_h(void **state) {
  (void)state;
  gr_cmd cmds[] = {{"run", "run it", "details", run_hello, NULL, 0}};
  gr_app app    = {"prog", NULL, cmds, 1, NULL};
  char  *argv[] = {"prog", "run", "-h"};
  assert_int_equal(gr_dispatch(&app, 3, argv), 0);
}

/* gr_dispatch: argc == 0 → argc > 1 is false → passes 0 / NULL to gr__dispatch */
static void test_gr_dispatch_argc_zero(void **state) {
  (void)state;
  gr_cmd cmds[] = {{"run", "run it", NULL, run_hello, NULL, 0}};
  gr_app app    = {"prog", NULL, cmds, 1, NULL};
  /* argc=0 → gr_dispatch receives argc=0; argv may be NULL */
  assert_int_equal(gr_dispatch(&app, 0, NULL), 2);
}

/* gr__parse_float: exponent with no sign (not '-', not '+') → else branch */
static void test_gr_parse_float_no_sign_before_exp_digit(void **state) {
  (void)state;
  double f   = 0.0;
  char   buf[64];
  /* "2e3" — exponent starts directly with digit, no sign → falls through both
   * if(*p=='-') and else if(*p=='+'), covering the implicit else path */
  assert_int_equal(gr__parse_float("2e3", &f, buf, sizeof buf, "-f"), GR_OK);
  assert_true(f > 1999.0 && f < 2001.0);
}

/* gr_dispatch: commands != NULL but command_count == 0 → "no commands" */
static void test_gr_dispatch_commands_not_null_but_count_zero(void **state) {
  (void)state;
  gr_cmd cmds[] = {{"run", NULL, NULL, run_hello, NULL, 0}};
  gr_app app    = {"prog", NULL, cmds, 0, NULL}; /* count=0, commands!=NULL */
  char  *argv[] = {"prog", "run"};
  assert_int_equal(gr_dispatch(&app, 2, argv), 2);
}

/* gr__needs_value: exercise GR_KIND_UINT and GR_KIND_FLOAT true branches via
 * gr_usage column measure (the only public caller of gr__needs_value) */
static void test_gr_needs_value_uint_and_float_branches(void **state) {
  (void)state;
  unsigned long u = 0;
  double        f = 0.0;
  gr_opt opts[] = {
      GR_UINT (0, "size",  &u, "N", "uint"),
      GR_FLOAT(0, "ratio", &f, "F", "float"),
      GR_END,
  };
  gr_spec spec = {"prog", NULL, opts, NULL};
  gr_usage(&spec, stdout);
}

/* gr_usage: option with no short_name (short_name==0) */
static void test_gr_usage_no_short_name(void **state) {
  (void)state;
  int    v    = 0;
  gr_opt opts[] = {GR_FLAG(0, "verbose", &v, "verbose"), GR_END};
  gr_spec spec  = {"prog", NULL, opts, NULL};
  gr_usage(&spec, stdout);
}

/* gr_usage: option with no long_name (long_name==NULL) */
static void test_gr_usage_no_long_name(void **state) {
  (void)state;
  int    v    = 0;
  gr_opt opts[] = {GR_FLAG('v', NULL, &v, "verbose"), GR_END};
  gr_spec spec  = {"prog", NULL, opts, NULL};
  gr_usage(&spec, stdout);
}

/* gr_usage: option with NULL help text → falls back to "" */
static void test_gr_usage_no_help_text(void **state) {
  (void)state;
  int v = 0;
  gr_opt opts[] = {
      {.short_name = 'v', .long_name = "verbose", .storage = &v,
       .value_hint = NULL, .help = NULL, .kind = GR_KIND_FLAG},
      GR_END,
  };
  gr_spec spec = {"prog", NULL, opts, NULL};
  gr_usage(&spec, stdout);
}

/* gr_usage: NULL program and NULL usage → fallback to "PROGRAM"/"[options]" */
static void test_gr_usage_null_program_and_usage(void **state) {
  (void)state;
  gr_opt  opts[] = {GR_END};
  gr_spec spec   = {NULL, NULL, opts, NULL};
  gr_usage(&spec, stdout);
}

/* gr__apply_flag: null storage → GR_ERR */
static void test_gr_apply_flag_null_storage(void **state) {
  (void)state;
  gr_opt o = {.kind = GR_KIND_FLAG, .storage = NULL};
  char buf[64];
  int r = gr__apply_flag(&o, buf, sizeof(buf));
  assert_int_equal(r, GR_ERR);
}

/* gr__apply_count: null storage → GR_ERR */
static void test_gr_apply_count_null_storage(void **state) {
  (void)state;
  gr_opt o = {.kind = GR_KIND_COUNT, .storage = NULL};
  char buf[64];
  int r = gr__apply_count(&o, buf, sizeof(buf));
  assert_int_equal(r, GR_ERR);
}

/* gr__apply_value: GR_KIND_STR with null storage → GR_ERR */
static void test_gr_apply_val_null_storage(void **state) {
  (void)state;
  gr_opt o = {.kind = GR_KIND_STR, .storage = NULL};
  char buf[64];
  int r = gr__apply_value(&o, "val", buf, sizeof(buf), "opt");
  assert_int_equal(r, GR_ERR);
}

/* gr__apply_value: unknown kind → GR_ERR */
static void test_gr_apply_val_unknown_kind(void **state) {
  (void)state;
  int storage = 0;
  gr_opt o = {.kind = 99, .storage = &storage}; /* unknown kind */
  char buf[64];
  int r = gr__apply_value(&o, "val", buf, sizeof(buf), "opt");
  assert_int_equal(r, GR_ERR);
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
      cmocka_unit_test(test_gr_apply_flag_null_storage),
      cmocka_unit_test(test_gr_apply_count_null_storage),
      cmocka_unit_test(test_gr_apply_val_null_storage),
      cmocka_unit_test(test_gr_apply_val_unknown_kind),
      cmocka_unit_test(test_gr_find_cmd_null_name_returns_null),
      cmocka_unit_test(test_gr_path_join_null_seg_noop),
      cmocka_unit_test(test_gr_dispatch_help_walk_argc_zero),
      cmocka_unit_test(test_gr_parse_flag_null_storage_propagates_error),
      cmocka_unit_test(test_gr_needs_value_true_via_usage),
      cmocka_unit_test(test_gr_parse_int_null_text),
      cmocka_unit_test(test_gr_parse_int_overflow),
      cmocka_unit_test(test_gr_parse_uint_null_text),
      cmocka_unit_test(test_gr_parse_uint_no_digit),
      cmocka_unit_test(test_gr_parse_float_null_text),
      cmocka_unit_test(test_gr_parse_float_uppercase_exponent),
      cmocka_unit_test(test_gr_parse_float_explicit_plus_exp),
      cmocka_unit_test(test_gr_parse_null_rest_direct),
      cmocka_unit_test(test_gr_parse_single_dash_positional),
      cmocka_unit_test(test_gr_path_join_empty_prefix),
      cmocka_unit_test(test_gr_help_walk_argc_zero_direct),
      cmocka_unit_test(test_gr_by_long_null_longname),
      cmocka_unit_test(test_gr_by_long_prefix_no_match),
      cmocka_unit_test(test_gr_dispatch_leaf_dash_h),
      cmocka_unit_test(test_gr_dispatch_argc_zero),
      cmocka_unit_test(test_gr_parse_float_no_sign_before_exp_digit),
      cmocka_unit_test(test_gr_dispatch_commands_not_null_but_count_zero),
      cmocka_unit_test(test_gr_needs_value_uint_and_float_branches),
      cmocka_unit_test(test_gr_usage_no_short_name),
      cmocka_unit_test(test_gr_usage_no_long_name),
      cmocka_unit_test(test_gr_usage_no_help_text),
      cmocka_unit_test(test_gr_usage_null_program_and_usage),
  };
  return cmocka_run_group_tests(tests, NULL, NULL);
}
