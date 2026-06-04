/* Unit tests for src/kernel/ktermios.c — default termios initialisation.
 * Every field is pinned to a documented value; divergence from the musl ABI
 * would silently break userspace TTY behaviour. */

#include "test_common.h"

#include <alcor2/ktermios.h>

static void default_termios_size_is_abi_frozen(void **state)
{
  (void)state;
  assert_int_equal(sizeof(k_termios_t), 60);
}

static void default_iflags(void **state)
{
  (void)state;
  k_termios_t t;
  ktermios_init_default(&t);
  assert_int_equal(t.c_iflag, KTERM_ICRNL);
}

static void default_oflags(void **state)
{
  (void)state;
  k_termios_t t;
  ktermios_init_default(&t);
  assert_int_equal(t.c_oflag, KTERM_ONLCR);
}

static void default_cflags(void **state)
{
  (void)state;
  k_termios_t t;
  ktermios_init_default(&t);
  assert_int_equal(t.c_cflag, KTERM_CS8 | KTERM_CREAD | KTERM_CLOCAL);
}

static void default_lflags(void **state)
{
  (void)state;
  k_termios_t t;
  ktermios_init_default(&t);
  assert_int_equal(
      t.c_lflag, KTERM_ISIG | KTERM_ICANON | KTERM_ECHO | KTERM_IEXTEN
  );
}

static void default_speeds(void **state)
{
  (void)state;
  k_termios_t t;
  ktermios_init_default(&t);
  assert_int_equal(t.__c_ispeed, KTERM_B38400);
  assert_int_equal(t.__c_ospeed, KTERM_B38400);
}

static void default_control_chars(void **state)
{
  (void)state;
  k_termios_t t;
  ktermios_init_default(&t);
  assert_int_equal(t.c_cc[KTERM_VINTR], '\x03');
  assert_int_equal(t.c_cc[KTERM_VQUIT], 0x1c);
  assert_int_equal(t.c_cc[KTERM_VERASE], 0x7f);
  assert_int_equal(t.c_cc[KTERM_VKILL], 0x15);
  assert_int_equal(t.c_cc[KTERM_VEOF], '\x04');
  assert_int_equal(t.c_cc[KTERM_VMIN], 1);
  assert_int_equal(t.c_cc[KTERM_VTIME], 0);
}

static void default_zeros_unset_cc_slots(void **state)
{
  (void)state;
  k_termios_t t;
  /* Poison the buffer, then verify kzero clears the whole struct first. */
  __builtin_memset(&t, 0xFF, sizeof(t));
  ktermios_init_default(&t);
  /* Slots 7..31 are unspecified — must be zero, not leftover garbage. */
  for(int i = 7; i < KTERM_MUSL_NCCS; i++)
    assert_int_equal(t.c_cc[i], 0);
}

static void default_line_discipline_is_zero(void **state)
{
  (void)state;
  k_termios_t t;
  ktermios_init_default(&t);
  assert_int_equal(t.c_line, 0);
}

int main(void)
{
  const struct CMUnitTest tests[] = {
      cmocka_unit_test(default_termios_size_is_abi_frozen),
      cmocka_unit_test(default_iflags),
      cmocka_unit_test(default_oflags),
      cmocka_unit_test(default_cflags),
      cmocka_unit_test(default_lflags),
      cmocka_unit_test(default_speeds),
      cmocka_unit_test(default_control_chars),
      cmocka_unit_test(default_zeros_unset_cc_slots),
      cmocka_unit_test(default_line_discipline_is_zero),
  };
  return cmocka_run_group_tests(tests, NULL, NULL);
}
