/* Unit tests for src/drivers/mouse_ps2/mouse_ps2.c — packet decode logic.
 * All hardware I/O is stubbed. We drive mouse_ps2_irq directly by feeding
 * simulated status+data byte sequences through the inb stub. */

#include "test_common.h"

#include <alcor2/mouse.h>
#include <alcor2/types.h>

#include <string.h>

/* io.h stubs — inb feeds from a scripted sequence, outb is silent. */
#define ALCOR2_IO_H

static u8  inb_seq[16];
static int inb_idx;
static int inb_len;

#define inb(p)     ((u8)(inb_idx < inb_len ? inb_seq[inb_idx++] : 0))
#define outb(p, v) ((void)(p), (void)(v))
#define outw(p, v) ((void)(p), (void)(v))
#define inw(p)     ((u16)0)
#define outl(p, v) ((void)(p), (void)(v))
#define inl(p)     ((u32)0)
#define io_wait()  ((void)0)

/* Capture mouse events posted by mouse_ps2_irq. */
static i32 ev_dx, ev_dy;
static u8  ev_buttons;
static int ev_count;

void       mouse_post_event(i32 dx, i32 dy, i16 dwheel, u8 buttons)
{
  (void)dwheel;
  ev_dx      = dx;
  ev_dy      = dy;
  ev_buttons = buttons;
  ev_count++;
}

/* Other link-only stubs. */
void console_print(const char *s)
{
  (void)s;
}
void irq_register(u8 irq, void (*h)(u8))
{
  (void)irq;
  (void)h;
}
void pic_unmask(u8 irq)
{
  (void)irq;
}

#include "../../src/drivers/mouse_ps2/mouse_ps2.c"

/* PS2_STATUS port is 0x64, PS2_DATA port is 0x60.
 * mouse_ps2_irq reads status first, then data.
 * Status needs OUTPUT_FULL (0x01) | AUX_DATA (0x20) = 0x21 to proceed. */
#define STATUS_MOUSE_READY 0x21

static void __attribute__((unused)) feed3(u8 b0, u8 b1, u8 b2)
{
  /* Each byte call: status read (0x64) then data read (0x60) */
  inb_idx    = 0;
  inb_len    = 6;
  inb_seq[0] = STATUS_MOUSE_READY;
  inb_seq[1] = b0;
  inb_seq[2] = STATUS_MOUSE_READY;
  inb_seq[3] = b1;
  inb_seq[4] = STATUS_MOUSE_READY;
  inb_seq[5] = b2;
}

static int reset(void **state)
{
  (void)state;
  ev_count = ev_dx = ev_dy = ev_buttons = 0;
  s_phase                               = 0;
  memset(inb_seq, 0, sizeof(inb_seq));
  inb_idx = inb_len = 0;
  return 0;
}

static void call_irq(u8 status_byte, u8 data_byte)
{
  inb_idx    = 0;
  inb_len    = 2;
  inb_seq[0] = status_byte;
  inb_seq[1] = data_byte;
  mouse_ps2_irq(12);
}

/* Tests */

static void basic_move_right(void **state)
{
  (void)state;
  /* flags=0x08 (bit3 always 1, no buttons, no sign), dx=10, dy=0. */
  call_irq(STATUS_MOUSE_READY, 0x08);
  call_irq(STATUS_MOUSE_READY, 10);
  call_irq(STATUS_MOUSE_READY, 0);
  assert_int_equal(ev_count, 1);
  assert_int_equal(ev_dx, 10);
  assert_int_equal(ev_dy, 0);
}

static void basic_move_up_inverts_y(void **state)
{
  (void)state;
  /* PS/2 +Y = up, screen +Y = down → dy must be negated. */
  call_irq(STATUS_MOUSE_READY, 0x08);
  call_irq(STATUS_MOUSE_READY, 0);
  call_irq(STATUS_MOUSE_READY, 5);
  assert_int_equal(ev_dy, -5);
}

static void x_sign_extension(void **state)
{
  (void)state;
  /* PKT_X_SIGN=0x10 → dx = raw - 256. raw=250 → dx = -6. */
  call_irq(STATUS_MOUSE_READY, 0x08 | 0x10);
  call_irq(STATUS_MOUSE_READY, 250);
  call_irq(STATUS_MOUSE_READY, 0);
  assert_int_equal(ev_dx, -6);
}

static void y_sign_extension(void **state)
{
  (void)state;
  /* PKT_Y_SIGN=0x20 → dy_raw = raw - 256, then negated. raw=10 → dy_raw=-246,
   * negated=246. */
  call_irq(STATUS_MOUSE_READY, 0x08 | 0x20);
  call_irq(STATUS_MOUSE_READY, 0);
  call_irq(STATUS_MOUSE_READY, 10);
  assert_int_equal(ev_dy, 246);
}

static void overflow_packet_dropped(void **state)
{
  (void)state;
  /* PKT_X_OVERFLOW=0x40 → packet must be dropped. */
  call_irq(STATUS_MOUSE_READY, 0x08 | 0x40);
  call_irq(STATUS_MOUSE_READY, 0);
  call_irq(STATUS_MOUSE_READY, 0);
  assert_int_equal(ev_count, 0);
}

static void y_overflow_packet_dropped(void **state)
{
  (void)state;
  call_irq(STATUS_MOUSE_READY, 0x08 | 0x80);
  call_irq(STATUS_MOUSE_READY, 0);
  call_irq(STATUS_MOUSE_READY, 0);
  assert_int_equal(ev_count, 0);
}

static void stray_byte_without_bit3_resyncs(void **state)
{
  (void)state;
  /* First byte without bit 3 set → discarded, phase stays 0. */
  call_irq(STATUS_MOUSE_READY, 0x00); /* stray, bit3=0 */
  assert_int_equal(s_phase, 0);
  assert_int_equal(ev_count, 0);
  /* Valid packet follows: 3 bytes needed. */
  call_irq(STATUS_MOUSE_READY, 0x08);
  call_irq(STATUS_MOUSE_READY, 1);
  call_irq(STATUS_MOUSE_READY, 0);
  assert_int_equal(ev_count, 1);
}

static void left_button_mapped(void **state)
{
  (void)state;
  call_irq(STATUS_MOUSE_READY, 0x08 | 0x01); /* PKT_BTN_LEFT */
  call_irq(STATUS_MOUSE_READY, 0);
  call_irq(STATUS_MOUSE_READY, 0);
  assert_int_equal(ev_buttons & ALCOR2_MOUSE_BTN_LEFT, ALCOR2_MOUSE_BTN_LEFT);
  assert_int_equal(ev_buttons & ALCOR2_MOUSE_BTN_RIGHT, 0);
}

static void right_button_mapped(void **state)
{
  (void)state;
  call_irq(STATUS_MOUSE_READY, 0x08 | 0x02); /* PKT_BTN_RIGHT */
  call_irq(STATUS_MOUSE_READY, 0);
  call_irq(STATUS_MOUSE_READY, 0);
  assert_int_equal(ev_buttons & ALCOR2_MOUSE_BTN_RIGHT, ALCOR2_MOUSE_BTN_RIGHT);
}

static void middle_button_mapped(void **state)
{
  (void)state;
  call_irq(STATUS_MOUSE_READY, 0x08 | 0x04); /* PKT_BTN_MIDDLE */
  call_irq(STATUS_MOUSE_READY, 0);
  call_irq(STATUS_MOUSE_READY, 0);
  assert_int_equal(
      ev_buttons & ALCOR2_MOUSE_BTN_MIDDLE, ALCOR2_MOUSE_BTN_MIDDLE
  );
}

static void non_aux_byte_ignored(void **state)
{
  (void)state;
  /* Status without AUX_DATA (0x20) bit → byte must be ignored. */
  call_irq(0x01, 0x08); /* OUTPUT_FULL but no AUX_DATA */
  assert_int_equal(s_phase, 0);
  assert_int_equal(ev_count, 0);
}

int main(void)
{
  const struct CMUnitTest tests[] = {
      cmocka_unit_test_setup(basic_move_right, reset),
      cmocka_unit_test_setup(basic_move_up_inverts_y, reset),
      cmocka_unit_test_setup(x_sign_extension, reset),
      cmocka_unit_test_setup(y_sign_extension, reset),
      cmocka_unit_test_setup(overflow_packet_dropped, reset),
      cmocka_unit_test_setup(y_overflow_packet_dropped, reset),
      cmocka_unit_test_setup(stray_byte_without_bit3_resyncs, reset),
      cmocka_unit_test_setup(left_button_mapped, reset),
      cmocka_unit_test_setup(right_button_mapped, reset),
      cmocka_unit_test_setup(middle_button_mapped, reset),
      cmocka_unit_test_setup(non_aux_byte_ignored, reset),
  };
  return cmocka_run_group_tests(tests, NULL, NULL);
}
