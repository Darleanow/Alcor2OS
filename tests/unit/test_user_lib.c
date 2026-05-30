#include "test_common.h"
#include "../../user/lib/grendizer.c"

static void test_gr_basename(void **state) {
    (void)state;
    assert_string_equal(gr__basename("foo/bar"), "bar");
    assert_string_equal(gr__basename("foo"), "foo");
    assert_string_equal(gr__basename(""), "program");
    assert_string_equal(gr__basename(NULL), "program");
}

static void test_gr_parse_basic(void **state) {
    (void)state;
    long my_int = 0;
    gr_opt opts[] = {
        GR_INT('i', "int", &my_int, "NUM", "An integer"),
        GR_END
    };
    gr_spec spec = { "prog", "usage", opts, "epilog" };
    gr_rest rest = {0};
    char errbuf[256] = {0};
    
    char *argv[] = { "prog", "-i", "42" };
    int argc = 3;
    
    int rc = gr_parse(&spec, argc, argv, &rest, errbuf, sizeof(errbuf));
    assert_int_equal(rc, GR_OK);
    assert_int_equal(my_int, 42);
}

static int dummy_run(void *userdata, int argc, char **argv) {
    (void)userdata; (void)argc; (void)argv;
    return 42;
}

static void test_gr_dispatch_basic(void **state) {
    (void)state;
    gr_cmd cmds[] = {
        { "run", "run cmd", "run details", dummy_run, NULL, 0 }
    };
    gr_app app = { "prog", "blurb", cmds, 1, NULL };
    
    char *argv[] = { "prog", "run" };
    int argc = 2;
    
    int rc = gr_dispatch(&app, argc, argv);
    assert_int_equal(rc, 42);
}

int main(void) {
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_gr_basename),
        cmocka_unit_test(test_gr_parse_basic),
        cmocka_unit_test(test_gr_dispatch_basic),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
