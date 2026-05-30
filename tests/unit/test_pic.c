/* Unit tests for src/drivers/pic/pic.c — ICW sequence and mask logic.
 * All port access captured via macro override + test-by-inclusion. */

#include "test_common.h"

#include <alcor2/types.h>

#include <string.h>

#define ALCOR2_IO_H

#define PORT_HIST_SIZE 64
static u16         write_ports[PORT_HIST_SIZE];
static u8          write_vals[PORT_HIST_SIZE];
static int         write_count;

static u8          port_state[256]; /* readable port state (masks etc.) */

static inline void _outb_impl(u16 port, u8 val)
{
  if(write_count < PORT_HIST_SIZE) {
    write_ports[write_count] = port;
    write_vals[write_count]  = val;
    write_count++;
  }
  port_state[port & 0xFF] = val;
}
#define outb(port, val) _outb_impl((u16)(port), (u8)(val))
#define inb(port)       ((u8)port_state[(u8)(port)])
#define outw(p, v)      ((void)(p), (void)(v))
#define inw(p)          ((u16)0)
#define outl(p, v)      ((void)(p), (void)(v))
#define inl(p)          ((u32)0)
#define io_wait()       ((void)0)

#include "../../src/drivers/pic/pic.c"

static int reset(void **state)
{
  (void)state;
  memset(write_ports, 0, sizeof(write_ports));
  memset(write_vals, 0, sizeof(write_vals));
  memset(port_state, 0, sizeof(port_state));
  write_count = 0;
  return 0;
}

static int write_to(u16 port, int from)
{
  for(int i = from; i < write_count; i++)
    if(write_ports[i] == port)
      return i;
  return -1;
}

static u8 write_val_at(int idx)
{
  return write_vals[idx];
}

/* pic_init ICW sequence */

static void init_sends_icw1_to_both_pics(void **state)
{
  (void)state;
  pic_init();
  /* ICW1 = 0x11 to PIC1_CMD (0x20) and PIC2_CMD (0xA0). */
  int i1 = write_to(0x20, 0);
  int i2 = write_to(0xA0, 0);
  assert_true(i1 >= 0);
  assert_true(i2 >= 0);
  assert_int_equal(write_val_at(i1), 0x11);
  assert_int_equal(write_val_at(i2), 0x11);
}

static void init_remaps_master_to_0x20(void **state)
{
  (void)state;
  pic_init();
  /* ICW2: master offset at 0x20. */
  int i = write_to(0x21, 0); /* first write to PIC1_DATA */
  assert_true(i >= 0);
  assert_int_equal(write_val_at(i), 0x20);
}

static void init_remaps_slave_to_0x28(void **state)
{
  (void)state;
  pic_init();
  int i = write_to(0xA1, 0);
  assert_true(i >= 0);
  assert_int_equal(write_val_at(i), 0x28);
}

static void init_masks_all_irqs(void **state)
{
  (void)state;
  pic_init();
  /* Last writes to data ports must be 0xFF. */
  int m1 = -1, m2 = -1;
  for(int i = 0; i < write_count; i++) {
    if(write_ports[i] == 0x21)
      m1 = i;
    if(write_ports[i] == 0xA1)
      m2 = i;
  }
  assert_true(m1 >= 0);
  assert_true(m2 >= 0);
  assert_int_equal(write_val_at(m1), 0xFF);
  assert_int_equal(write_val_at(m2), 0xFF);
}

/* pic_eoi */

static void eoi_master_irq_sends_only_to_master(void **state)
{
  (void)state;
  write_count = 0;
  pic_eoi(0);
  /* Only PIC1_CMD (0x20) should get the EOI. */
  assert_true(write_to(0x20, 0) >= 0);
  assert_int_equal(write_to(0xA0, 0), -1);
}

static void eoi_slave_irq_sends_to_both(void **state)
{
  (void)state;
  write_count = 0;
  pic_eoi(8);
  assert_true(write_to(0xA0, 0) >= 0); /* slave first */
  assert_true(write_to(0x20, 0) >= 0); /* then master */
}

/* pic_unmask */

static void unmask_master_irq_clears_bit(void **state)
{
  (void)state;
  port_state[0x21] = 0xFF; /* all masked */
  pic_unmask(3);
  assert_int_equal(port_state[0x21] & (1 << 3), 0);
  /* Other bits stay set. */
  assert_int_equal(port_state[0x21] & ~(1 << 3), 0xFF & ~(1 << 3));
}

static void unmask_slave_irq_clears_bit_on_slave(void **state)
{
  (void)state;
  port_state[0xA1] = 0xFF;
  port_state[0x21] = 0xFF;
  pic_unmask(10); /* IRQ 10 → slave bit 2 */
  assert_int_equal(port_state[0xA1] & (1 << 2), 0);
}

static void unmask_slave_irq_also_unmasks_cascade_on_master(void **state)
{
  (void)state;
  port_state[0x21] = 0xFF;
  port_state[0xA1] = 0xFF;
  pic_unmask(9); /* any slave IRQ */
  /* Cascade line (IRQ 2, bit 2) on master must also be cleared. */
  assert_int_equal(port_state[0x21] & (1 << 2), 0);
}

int main(void)
{
  const struct CMUnitTest tests[] = {
      cmocka_unit_test_setup(init_sends_icw1_to_both_pics, reset),
      cmocka_unit_test_setup(init_remaps_master_to_0x20, reset),
      cmocka_unit_test_setup(init_remaps_slave_to_0x28, reset),
      cmocka_unit_test_setup(init_masks_all_irqs, reset),
      cmocka_unit_test_setup(eoi_master_irq_sends_only_to_master, reset),
      cmocka_unit_test_setup(eoi_slave_irq_sends_to_both, reset),
      cmocka_unit_test_setup(unmask_master_irq_clears_bit, reset),
      cmocka_unit_test_setup(unmask_slave_irq_clears_bit_on_slave, reset),
      cmocka_unit_test_setup(
          unmask_slave_irq_also_unmasks_cascade_on_master, reset
      ),
  };
  return cmocka_run_group_tests(tests, NULL, NULL);
}
