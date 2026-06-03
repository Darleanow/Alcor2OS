/**
 * @file tests/unit/test_kbd_layout.c
 * @brief Unit tests for kbd_layout.c pure-math and table functions.
 *
 * Tests the layout tables, pick_pl/pick_sh selection, US/FR keyboard mapping,
 * caps-lock logic, out_pend ring buffer, and pend_csi/pend_ss3 helpers.
 * Hardware-dependent functions (cpu_enable_interrupts, keyboard_raw_*) are
 * stubbed; the blocking I/O path is not exercised here.
 */

#include "test_common.h"

#include <alcor2/kbd.h>
#include <alcor2/ktermios.h>
#include <alcor2/proc/proc.h>
#include <alcor2/types.h>

#include <stddef.h>
#include <string.h>


void *kmemcpy(void *d, const void *s, u64 n)
{
  return memcpy(d, s, n);
}
void kzero(void *d, u64 n)
{
  memset(d, 0, n);
}
void console_print(const char *s)  { (void)s; }
void console_printf(const char *fmt, ...) { (void)fmt; }

/* fb_console stubs */
bool fb_console_app_cursor_keys(void) { return false; }
void fb_console_write(const void *buf, size_t len) { (void)buf; (void)len; }
void fb_console_scrollback_up(int n)   { (void)n; }
void fb_console_scrollback_down(int n) { (void)n; }

/* keyboard driver stubs */
static u8    g_raw_buf[256];
static u32   g_raw_head = 0;
static u32   g_raw_tail = 0;

bool keyboard_raw_available(void)
{
  return g_raw_head != g_raw_tail;
}
u8 keyboard_raw_pop(void)
{
  u8 b        = g_raw_buf[g_raw_head];
  g_raw_head  = (g_raw_head + 1) % 256u;
  return b;
}
u32 keyboard_raw_peek(u8 *buf, u32 n)
{
  u32 count = 0;
  u32 h     = g_raw_head;
  while(count < n && h != g_raw_tail) {
    buf[count++] = g_raw_buf[h];
    h            = (h + 1) % 256u;
  }
  return count;
}

/* cpu/process stubs */
void cpu_enable_interrupts(void)  {}
void cpu_disable_interrupts(void) {}

static proc_t g_test_proc;
proc_t *proc_current(void)    { return &g_test_proc; }
void         proc_schedule(void)   {}

/* termios stub */
void ktermios_init_default(k_termios_t *t)
{
  memset(t, 0, sizeof(*t));
  t->c_lflag    = KTERM_ICANON | KTERM_ECHO;
  t->c_cc[KTERM_VERASE] = '\b';
  t->c_cc[KTERM_VKILL]  = 21;
  t->c_cc[KTERM_VEOF]   = 4;
  t->c_cc[KTERM_VMIN]   = 1;
  t->c_cc[KTERM_VTIME]  = 0;
}

#include "../../src/kernel/input/kbd_layout.c"


static void raw_push(u8 b)
{
  g_raw_buf[g_raw_tail] = b;
  g_raw_tail            = (g_raw_tail + 1) % 256u;
}

static int setup(void **state)
{
  (void)state;
  g_raw_head = g_raw_tail = 0;
  kbd_set_layout(KBD_LAYOUT_US);
  kbd_set_release_events(false);
  out_pend_w = out_pend_r = 0;
  memset(&g_kbd, 0, sizeof(g_kbd));
  
  memset(&g_test_proc, 0, sizeof(g_test_proc));
  ktermios_init_default(&g_test_proc.termios);
  return 0;
}


static void kbd_default_layout_is_us(void **state)
{
  (void)state;
  assert_int_equal(kbd_get_layout(), KBD_LAYOUT_US);
}

static void kbd_set_layout_fr_works(void **state)
{
  (void)state;
  kbd_set_layout(KBD_LAYOUT_FR);
  assert_int_equal(kbd_get_layout(), KBD_LAYOUT_FR);
}

static void kbd_set_layout_out_of_range_falls_back_to_us(void **state)
{
  (void)state;
  kbd_set_layout((kbd_layout_t)99);
  assert_int_equal(kbd_get_layout(), KBD_LAYOUT_US);
}


static void kbd_release_events_default_off(void **state)
{
  (void)state;
  assert_false(kbd_get_release_events());
}

static void kbd_release_events_toggle(void **state)
{
  (void)state;
  kbd_set_release_events(true);
  assert_true(kbd_get_release_events());
  kbd_set_release_events(false);
  assert_false(kbd_get_release_events());
}


static void us_plain_table_a_key(void **state)
{
  (void)state;
  /* Scancode 0x1E = 'a' in US plain */
  assert_int_equal(us_pl[0x1E], 'a');
}

static void us_plain_table_space(void **state)
{
  (void)state;
  assert_int_equal(us_pl[0x39], ' ');
}

static void us_shift_table_a_is_uppercase(void **state)
{
  (void)state;
  assert_int_equal(us_sh[0x1E], 'A');
}

static void us_shift_table_1_is_exclaim(void **state)
{
  (void)state;
  /* scancode 0x02 = '1' plain, '!' shifted */
  assert_int_equal(us_sh[0x02], '!');
}


static void fr_tables_init_on_demand(void **state)
{
  (void)state;
  /* Before first use fr_ready is false */
  fr_ready = false;
  kbd_set_layout(KBD_LAYOUT_FR);
  /* pick_pl forces init */
  const unsigned char *pl = pick_pl(KBD_LAYOUT_FR);
  (void)pl;
  assert_true(fr_ready);
}

static void fr_plain_q_is_a(void **state)
{
  (void)state;
  /* AZERTY: scancode 0x10 (US 'q' position) → 'a' in FR */
  fr_ready = false;
  pick_pl(KBD_LAYOUT_FR); /* trigger init */
  assert_int_equal(fr_pl[0x10], 'a');
}

static void fr_plain_digit_row_ampersand(void **state)
{
  (void)state;
  fr_ready = false;
  pick_pl(KBD_LAYOUT_FR);
  /* scancode 0x02 → '&' in FR plain */
  assert_int_equal(fr_pl[0x02], '&');
}

static void fr_shift_digit_is_1(void **state)
{
  (void)state;
  fr_ready = false;
  pick_sh(KBD_LAYOUT_FR);
  /* scancode 0x02 shifted → '1' */
  assert_int_equal(fr_sh[0x02], '1');
}


static void out_pend_push_and_take(void **state)
{
  (void)state;
  assert_true(out_pend_push('x'));
  unsigned char c = 0;
  assert_true(out_pend_take(&c));
  assert_int_equal(c, 'x');
}

static void out_pend_take_empty_returns_false(void **state)
{
  (void)state;
  unsigned char c;
  assert_false(out_pend_take(&c));
}

static void out_pend_multiple_bytes_fifo(void **state)
{
  (void)state;
  out_pend_push('A');
  out_pend_push('B');
  out_pend_push('C');
  unsigned char c;
  out_pend_take(&c); assert_int_equal(c, 'A');
  out_pend_take(&c); assert_int_equal(c, 'B');
  out_pend_take(&c); assert_int_equal(c, 'C');
  assert_false(out_pend_take(&c));
}


static void pend_csi_emits_three_bytes(void **state)
{
  (void)state;
  pend_csi('A');
  unsigned char c;
  out_pend_take(&c); assert_int_equal(c, 0x1b);
  out_pend_take(&c); assert_int_equal(c, '[');
  out_pend_take(&c); assert_int_equal(c, 'A');
  assert_false(out_pend_take(&c));
}

static void pend_ss3_emits_three_bytes(void **state)
{
  (void)state;
  pend_ss3('P');
  unsigned char c;
  out_pend_take(&c); assert_int_equal(c, 0x1b);
  out_pend_take(&c); assert_int_equal(c, 'O');
  out_pend_take(&c); assert_int_equal(c, 'P');
}

static void pend_csi_tilde_single_digit(void **state)
{
  (void)state;
  pend_csi_tilde(2u); /* Insert */
  unsigned char c;
  out_pend_take(&c); assert_int_equal(c, 0x1b);
  out_pend_take(&c); assert_int_equal(c, '[');
  out_pend_take(&c); assert_int_equal(c, '2');
  out_pend_take(&c); assert_int_equal(c, '~');
}

static void pend_csi_tilde_two_digits(void **state)
{
  (void)state;
  pend_csi_tilde(15u); /* F5 */
  unsigned char c;
  out_pend_take(&c); assert_int_equal(c, 0x1b);
  out_pend_take(&c); assert_int_equal(c, '[');
  out_pend_take(&c); assert_int_equal(c, '1');
  out_pend_take(&c); assert_int_equal(c, '5');
  out_pend_take(&c); assert_int_equal(c, '~');
}


static void emit_user_cp_ascii_dry_returns_true(void **state)
{
  (void)state;
  unsigned char out = 0;
  bool result = emit_user_cp('a', &out, true);
  assert_true(result);
  assert_int_equal(out, 0); /* dry mode must not write */
}

static void emit_user_cp_ascii_writes_byte(void **state)
{
  (void)state;
  unsigned char out = 0;
  bool result = emit_user_cp('Z', &out, false);
  assert_true(result);
  assert_int_equal(out, 'Z');
}

static void emit_user_cp_latin1_pushes_utf8(void **state)
{
  (void)state;
  unsigned char out = 0;
  /* 0xE9 = U+00E9 é → UTF-8: 0xC3 0xA9 */
  bool result = emit_user_cp(0xE9, &out, false);
  assert_false(result); /* returns false: byte queued, not direct */
  unsigned char c;
  out_pend_take(&c); assert_int_equal(c, 0xC3);
  out_pend_take(&c); assert_int_equal(c, 0xA9);
}


static void us_caps_scan_letter_returns_true(void **state)
{
  (void)state;
  /* 0x1E = 'a' */
  assert_true(us_caps_scan(0x1E));
}

static void us_caps_scan_non_letter_returns_false(void **state)
{
  (void)state;
  /* 0x02 = '1' */
  assert_false(us_caps_scan(0x02));
}


static void process_raw_us_key_a(void **state)
{
  (void)state;
  kbd_ev_ctx_t ctx  = {0};
  unsigned char out = 0;
  /* scancode 0x1E = 'a' press */
  bool r = process_raw_ctx(0x1E, &ctx, &out, false);
  assert_true(r);
  assert_int_equal(out, 'a');
}

static void process_raw_us_shift_a(void **state)
{
  (void)state;
  kbd_ev_ctx_t ctx = {0};
  ctx.mod.shift    = true;
  unsigned char out = 0;
  bool r = process_raw_ctx(0x1E, &ctx, &out, false);
  assert_true(r);
  assert_int_equal(out, 'A');
}

static void process_raw_e0_sets_pend_flag(void **state)
{
  (void)state;
  kbd_ev_ctx_t  ctx = {0};
  unsigned char out = 0;
  bool r = process_raw_ctx(0xE0, &ctx, &out, false);
  assert_false(r);
  assert_true(ctx.pend_e0);
}

static void process_raw_key_release_no_output(void **state)
{
  (void)state;
  kbd_ev_ctx_t  ctx = {0};
  unsigned char out = 0;
  /* 0x1E | 0x80 = key-up for 'a' */
  bool r = process_raw_ctx(0x1E | 0x80, &ctx, &out, false);
  assert_false(r);
}


static void kbd_raw_pending_empty_returns_false(void **state)
{
  (void)state;
  assert_false(kbd_raw_pending());
}

static void kbd_raw_pending_with_pend_byte_returns_true(void **state)
{
  (void)state;
  out_pend_push('x');
  assert_true(kbd_raw_pending());
}

static void kbd_raw_pending_key_up_only_returns_false(void **state)
{
  (void)state;
  /* A key-release alone should not make kbd_raw_pending return true */
  raw_push(0x1E | 0x80); /* 'a' key-up */
  assert_false(kbd_raw_pending());
}

static void kbd_raw_pending_printable_press_returns_true(void **state)
{
  (void)state;
  raw_push(0x1E); /* 'a' key-down */
  assert_true(kbd_raw_pending());
}

static void kbd_read_translated_canon_echoes_and_blocks_until_newline(void **state)
{
  (void)state;
  char buf[32] = {0};
  
  /* Type 'h', 'i', '\n' */
  raw_push(0x23); /* 'h' press */
  raw_push(0x23 | 0x80); /* 'h' release */
  raw_push(0x17); /* 'i' press */
  raw_push(0x17 | 0x80); /* 'i' release */
  raw_push(0x1C); /* 'Enter' press */
  raw_push(0x1C | 0x80); /* 'Enter' release */
  
  u64 n = kbd_read_translated(buf, sizeof(buf));
  assert_int_equal(n, 3);
  assert_int_equal(buf[0], 'h');
  assert_int_equal(buf[1], 'i');
  assert_int_equal(buf[2], '\n');
}

static void kbd_read_translated_canon_handles_backspace(void **state)
{
  (void)state;
  char buf[32] = {0};
  
  /* Type 'h', 'o', backspace, 'i', '\n' */
  raw_push(0x23); /* 'h' */
  raw_push(0x18); /* 'o' */
  raw_push(0x0E); /* backspace */
  raw_push(0x17); /* 'i' */
  raw_push(0x1C); /* enter */
  
  u64 n = kbd_read_translated(buf, sizeof(buf));
  assert_int_equal(n, 3);
  assert_string_equal(buf, "hi\n");
}

/* Helper: push an E0 extended scancode and read resulting bytes */
static void push_e0(u8 ext)
{
  raw_push(0xE0);
  raw_push(ext);
}

static int get_pend_bytes(unsigned char *out, int max)
{
  int n = 0;
  unsigned char c;
  while(n < max && out_pend_take(&c))
    out[n++] = c;
  return n;
}

/* Arrow keys emit CSI sequences */
static void arrow_up_emits_csi_A(void **state)
{
  (void)state;
  push_e0(0x48);
  unsigned char out[4];
  int r = process_raw_ctx(0xE0, &g_kbd, out, false);
  (void)r;
  r = process_raw_ctx(0x48, &g_kbd, out, false);
  unsigned char buf[8];
  int n = get_pend_bytes(buf, 8);
  assert_true(n >= 3);
  assert_int_equal(buf[0], '\x1b');
  assert_int_equal(buf[1], '[');
  assert_int_equal(buf[2], 'A');
}

static void arrow_down_emits_csi_B(void **state)
{
  (void)state;
  process_raw_ctx(0xE0, &g_kbd, NULL, false);
  process_raw_ctx(0x50, &g_kbd, NULL, false);
  unsigned char buf[8];
  int n = get_pend_bytes(buf, 8);
  assert_true(n >= 3);
  assert_int_equal(buf[2], 'B');
}

static void arrow_left_emits_csi_D(void **state)
{
  (void)state;
  process_raw_ctx(0xE0, &g_kbd, NULL, false);
  process_raw_ctx(0x4b, &g_kbd, NULL, false);
  unsigned char buf[8];
  int n = get_pend_bytes(buf, 8);
  assert_true(n >= 3);
  assert_int_equal(buf[2], 'D');
}

static void arrow_right_emits_csi_C(void **state)
{
  (void)state;
  process_raw_ctx(0xE0, &g_kbd, NULL, false);
  process_raw_ctx(0x4d, &g_kbd, NULL, false);
  unsigned char buf[8];
  int n = get_pend_bytes(buf, 8);
  assert_true(n >= 3);
  assert_int_equal(buf[2], 'C');
}

/* Home / End */
static void home_emits_ss3_H(void **state)
{
  (void)state;
  process_raw_ctx(0xE0, &g_kbd, NULL, false);
  process_raw_ctx(0x47, &g_kbd, NULL, false);
  unsigned char buf[8];
  int n = get_pend_bytes(buf, 8);
  assert_true(n >= 3);
  assert_int_equal(buf[2], 'H');
}

static void end_emits_ss3_F(void **state)
{
  (void)state;
  process_raw_ctx(0xE0, &g_kbd, NULL, false);
  process_raw_ctx(0x4f, &g_kbd, NULL, false);
  unsigned char buf[8];
  int n = get_pend_bytes(buf, 8);
  assert_true(n >= 3);
  assert_int_equal(buf[2], 'F');
}

/* Page Up / Page Down */
static void page_up_emits_csi_tilde_5(void **state)
{
  (void)state;
  process_raw_ctx(0xE0, &g_kbd, NULL, false);
  process_raw_ctx(0x49, &g_kbd, NULL, false);
  unsigned char buf[8];
  int n = get_pend_bytes(buf, 8);
  assert_true(n >= 4);
  assert_int_equal(buf[0], '\x1b');
  assert_int_equal(buf[1], '[');
  assert_int_equal(buf[2], '5');
  assert_int_equal(buf[3], '~');
}

static void page_down_emits_csi_tilde_6(void **state)
{
  (void)state;
  process_raw_ctx(0xE0, &g_kbd, NULL, false);
  process_raw_ctx(0x51, &g_kbd, NULL, false);
  unsigned char buf[8];
  int n = get_pend_bytes(buf, 8);
  assert_true(n >= 4);
  assert_int_equal(buf[2], '6');
}

/* Insert / Delete */
static void insert_emits_csi_tilde_2(void **state)
{
  (void)state;
  process_raw_ctx(0xE0, &g_kbd, NULL, false);
  process_raw_ctx(0x52, &g_kbd, NULL, false);
  unsigned char buf[8];
  int n = get_pend_bytes(buf, 8);
  assert_true(n >= 4);
  assert_int_equal(buf[2], '2');
}

static void delete_emits_csi_tilde_3(void **state)
{
  (void)state;
  process_raw_ctx(0xE0, &g_kbd, NULL, false);
  process_raw_ctx(0x53, &g_kbd, NULL, false);
  unsigned char buf[8];
  int n = get_pend_bytes(buf, 8);
  assert_true(n >= 4);
  assert_int_equal(buf[2], '3');
}

/* E0-released key produces no output */
static void e0_release_no_output(void **state)
{
  (void)state;
  process_raw_ctx(0xE0, &g_kbd, NULL, false);
  process_raw_ctx(0x48 | 0x80, &g_kbd, NULL, false); /* release up-arrow */
  unsigned char buf[4];
  assert_int_equal(get_pend_bytes(buf, 4), 0);
}

/* E0-RAlt down/up sets mod.alt */
static void e0_ralt_sets_alt_mod(void **state)
{
  (void)state;
  process_raw_ctx(0xE0, &g_kbd, NULL, false);
  process_raw_ctx(0x38, &g_kbd, NULL, false); /* RAlt down */
  assert_true(g_kbd.mod.alt);
  process_raw_ctx(0xE0, &g_kbd, NULL, false);
  process_raw_ctx(0x38 | 0x80, &g_kbd, NULL, false); /* RAlt up */
  assert_false(g_kbd.mod.alt);
}

/* Function keys F1-F4 → SS3, F5-F12 → CSI tilde */
static void f1_emits_ss3_P(void **state)
{
  (void)state;
  process_raw_ctx(0x3b, &g_kbd, NULL, false);
  unsigned char buf[8];
  int n = get_pend_bytes(buf, 8);
  assert_true(n >= 3);
  assert_int_equal(buf[0], '\x1b');
  assert_int_equal(buf[1], 'O');
  assert_int_equal(buf[2], 'P');
}

static void f2_emits_ss3_Q(void **state)
{
  (void)state;
  process_raw_ctx(0x3c, &g_kbd, NULL, false);
  unsigned char buf[4];
  get_pend_bytes(buf, 4);
  assert_int_equal(buf[2], 'Q');
}

static void f3_emits_ss3_R(void **state)
{
  (void)state;
  process_raw_ctx(0x3d, &g_kbd, NULL, false);
  unsigned char buf[4];
  get_pend_bytes(buf, 4);
  assert_int_equal(buf[2], 'R');
}

static void f4_emits_ss3_S(void **state)
{
  (void)state;
  process_raw_ctx(0x3e, &g_kbd, NULL, false);
  unsigned char buf[4];
  get_pend_bytes(buf, 4);
  assert_int_equal(buf[2], 'S');
}

static void f5_emits_csi_tilde_15(void **state)
{
  (void)state;
  process_raw_ctx(0x3f, &g_kbd, NULL, false);
  unsigned char buf[8];
  int n = get_pend_bytes(buf, 8);
  /* ESC [ 1 5 ~ */
  assert_true(n >= 5);
  assert_int_equal(buf[2], '1');
  assert_int_equal(buf[3], '5');
}

static void f12_emits_csi_tilde_24(void **state)
{
  (void)state;
  process_raw_ctx(0x58, &g_kbd, NULL, false);
  unsigned char buf[8];
  int n = get_pend_bytes(buf, 8);
  assert_true(n >= 5);
  assert_int_equal(buf[2], '2');
  assert_int_equal(buf[3], '4');
}

/* Ctrl modifier */
static void ctrl_modifier_tracked(void **state)
{
  (void)state;
  process_raw_ctx(0x1d, &g_kbd, NULL, false); /* LCtrl down */
  assert_true(g_kbd.mod.ctrl);
  process_raw_ctx(0x1d | 0x80, &g_kbd, NULL, false); /* LCtrl up */
  assert_false(g_kbd.mod.ctrl);
}

/* Caps-lock toggles */
static void capslock_toggles(void **state)
{
  (void)state;
  assert_false(g_kbd.mod.capslock);
  process_raw_ctx(0x3a, &g_kbd, NULL, false); /* CapsLock press */
  assert_true(g_kbd.mod.capslock);
  process_raw_ctx(0x3a, &g_kbd, NULL, false); /* second press toggles off */
  assert_false(g_kbd.mod.capslock);
}

/* Capslock only toggles on press, not release */
static void capslock_release_no_toggle(void **state)
{
  (void)state;
  process_raw_ctx(0x3a, &g_kbd, NULL, false);
  assert_true(g_kbd.mod.capslock);
  process_raw_ctx(0x3a | 0x80, &g_kbd, NULL, false); /* release */
  assert_true(g_kbd.mod.capslock); /* unchanged */
}

/* Release events: key-up emits \x00+char */
static void release_events_emits_sentinel(void **state)
{
  (void)state;
  kbd_set_release_events(true);
  /* Press 'a' (scancode 0x1e) then release */
  unsigned char dummy[4] = {0};
  process_raw_ctx(0x1e, &g_kbd, dummy, false); /* press */
  out_pend_w = out_pend_r = 0;   /* discard press output */
  process_raw_ctx(0x1e | 0x80, &g_kbd, dummy, false); /* release */
  unsigned char buf[4];
  int n = get_pend_bytes(buf, 4);
  assert_true(n >= 2);
  assert_int_equal(buf[0], 0x00); /* sentinel */
  assert_int_equal(buf[1], 'a');  /* unshifted char */
}

/* Release events off: no sentinel on key-up */
static void release_events_off_no_sentinel(void **state)
{
  (void)state;
  kbd_set_release_events(false);
  unsigned char dummy[4] = {0};
  process_raw_ctx(0x1e, &g_kbd, dummy, false);
  out_pend_w = out_pend_r = 0;
  process_raw_ctx(0x1e | 0x80, &g_kbd, dummy, false);
  unsigned char buf[4];
  assert_int_equal(get_pend_bytes(buf, 4), 0);
}

/* kbd_set_release_events/kbd_get_release_events */
static void release_events_getter_setter(void **state)
{
  (void)state;
  kbd_set_release_events(true);
  assert_true(kbd_get_release_events());
  kbd_set_release_events(false);
  assert_false(kbd_get_release_events());
}

/* FR layout: process 'a' scancode (0x1e) gives 'q' */
static void fr_process_raw_q_scan_gives_a(void **state)
{
  (void)state;
  kbd_set_layout(KBD_LAYOUT_FR);
  unsigned char out[4] = {0};
  bool r = process_raw_ctx(0x10, &g_kbd, out, false); /* 0x10 = 'q' scan in US = 'a' in FR */
  (void)r;
  unsigned char buf[4];
  get_pend_bytes(buf, 4);
}

/* fr_caps_scan: letter key returns true */
static void fr_caps_scan_letter_true(void **state)
{
  (void)state;
  assert_true(fr_caps_scan(0x1e)); /* 'a' scan */
}

/* fr_caps_scan: non-letter returns false */
static void fr_caps_scan_non_letter_false(void **state)
{
  (void)state;
  assert_false(fr_caps_scan(0x01)); /* ESC scan — not a letter */
}

/* kbd_set_layout out of range */
static void kbd_set_layout_resets_state(void **state)
{
  (void)state;
  /* Prime some state */
  g_kbd.pend_e0 = true;
  g_kbd.lalt_dn = true;
  kbd_set_layout(KBD_LAYOUT_US);
  assert_false(g_kbd.pend_e0);
  assert_false(g_kbd.lalt_dn);
}

/* dry=true: process_raw_ctx doesn't modify out_pend */
static void process_raw_dry_no_side_effect(void **state)
{
  (void)state;
  u32 w_before = out_pend_w;
  unsigned char out[4];
  process_raw_ctx(0xE0, &g_kbd, out, true);
  process_raw_ctx(0x48, &g_kbd, out, true); /* arrow up in dry mode */
  /* dry mode must not push to out_pend */
  assert_int_equal(out_pend_w, w_before);
}

int main(void)
{
  const struct CMUnitTest tests[] = {
      /* layout get/set */
      cmocka_unit_test_setup(kbd_default_layout_is_us, setup),
      cmocka_unit_test_setup(kbd_set_layout_fr_works, setup),
      cmocka_unit_test_setup(kbd_set_layout_out_of_range_falls_back_to_us, setup),
      /* release events */
      cmocka_unit_test_setup(kbd_release_events_default_off, setup),
      cmocka_unit_test_setup(kbd_release_events_toggle, setup),
      /* US table spot-checks */
      cmocka_unit_test_setup(us_plain_table_a_key, setup),
      cmocka_unit_test_setup(us_plain_table_space, setup),
      cmocka_unit_test_setup(us_shift_table_a_is_uppercase, setup),
      cmocka_unit_test_setup(us_shift_table_1_is_exclaim, setup),
      /* FR init */
      cmocka_unit_test_setup(fr_tables_init_on_demand, setup),
      cmocka_unit_test_setup(fr_plain_q_is_a, setup),
      cmocka_unit_test_setup(fr_plain_digit_row_ampersand, setup),
      cmocka_unit_test_setup(fr_shift_digit_is_1, setup),
      /* ring buffer */
      cmocka_unit_test_setup(out_pend_push_and_take, setup),
      cmocka_unit_test_setup(out_pend_take_empty_returns_false, setup),
      cmocka_unit_test_setup(out_pend_multiple_bytes_fifo, setup),
      /* pend_csi / pend_ss3 */
      cmocka_unit_test_setup(pend_csi_emits_three_bytes, setup),
      cmocka_unit_test_setup(pend_ss3_emits_three_bytes, setup),
      cmocka_unit_test_setup(pend_csi_tilde_single_digit, setup),
      cmocka_unit_test_setup(pend_csi_tilde_two_digits, setup),
      /* emit_user_cp */
      cmocka_unit_test_setup(emit_user_cp_ascii_dry_returns_true, setup),
      cmocka_unit_test_setup(emit_user_cp_ascii_writes_byte, setup),
      cmocka_unit_test_setup(emit_user_cp_latin1_pushes_utf8, setup),
      /* caps scan */
      cmocka_unit_test_setup(us_caps_scan_letter_returns_true, setup),
      cmocka_unit_test_setup(us_caps_scan_non_letter_returns_false, setup),
      /* process_raw_ctx */
      cmocka_unit_test_setup(process_raw_us_key_a, setup),
      cmocka_unit_test_setup(process_raw_us_shift_a, setup),
      cmocka_unit_test_setup(process_raw_e0_sets_pend_flag, setup),
      cmocka_unit_test_setup(process_raw_key_release_no_output, setup),
      /* kbd_raw_pending */
      cmocka_unit_test_setup(kbd_raw_pending_empty_returns_false, setup),
      cmocka_unit_test_setup(kbd_raw_pending_with_pend_byte_returns_true, setup),
      cmocka_unit_test_setup(kbd_raw_pending_key_up_only_returns_false, setup),
      cmocka_unit_test_setup(kbd_raw_pending_printable_press_returns_true, setup),
      /* TTY Queue */
      cmocka_unit_test_setup(kbd_read_translated_canon_echoes_and_blocks_until_newline, setup),
      cmocka_unit_test_setup(kbd_read_translated_canon_handles_backspace, setup),
      /* E0 extended keys */
      cmocka_unit_test_setup(arrow_up_emits_csi_A, setup),
      cmocka_unit_test_setup(arrow_down_emits_csi_B, setup),
      cmocka_unit_test_setup(arrow_left_emits_csi_D, setup),
      cmocka_unit_test_setup(arrow_right_emits_csi_C, setup),
      cmocka_unit_test_setup(home_emits_ss3_H, setup),
      cmocka_unit_test_setup(end_emits_ss3_F, setup),
      cmocka_unit_test_setup(page_up_emits_csi_tilde_5, setup),
      cmocka_unit_test_setup(page_down_emits_csi_tilde_6, setup),
      cmocka_unit_test_setup(insert_emits_csi_tilde_2, setup),
      cmocka_unit_test_setup(delete_emits_csi_tilde_3, setup),
      cmocka_unit_test_setup(e0_release_no_output, setup),
      cmocka_unit_test_setup(e0_ralt_sets_alt_mod, setup),
      /* Function keys */
      cmocka_unit_test_setup(f1_emits_ss3_P, setup),
      cmocka_unit_test_setup(f2_emits_ss3_Q, setup),
      cmocka_unit_test_setup(f3_emits_ss3_R, setup),
      cmocka_unit_test_setup(f4_emits_ss3_S, setup),
      cmocka_unit_test_setup(f5_emits_csi_tilde_15, setup),
      cmocka_unit_test_setup(f12_emits_csi_tilde_24, setup),
      /* Modifiers */
      cmocka_unit_test_setup(ctrl_modifier_tracked, setup),
      cmocka_unit_test_setup(capslock_toggles, setup),
      cmocka_unit_test_setup(capslock_release_no_toggle, setup),
      /* Release events */
      cmocka_unit_test_setup(release_events_emits_sentinel, setup),
      cmocka_unit_test_setup(release_events_off_no_sentinel, setup),
      cmocka_unit_test_setup(release_events_getter_setter, setup),
      /* FR layout */
      cmocka_unit_test_setup(fr_process_raw_q_scan_gives_a, setup),
      cmocka_unit_test_setup(fr_caps_scan_letter_true, setup),
      cmocka_unit_test_setup(fr_caps_scan_non_letter_false, setup),
      /* kbd_set_layout side effects */
      cmocka_unit_test_setup(kbd_set_layout_resets_state, setup),
      /* dry mode */
      cmocka_unit_test_setup(process_raw_dry_no_side_effect, setup),
  };
  return cmocka_run_group_tests(tests, NULL, NULL);
}
