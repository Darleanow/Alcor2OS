/* Unit tests for src/drivers/pci/pci.c pure logic.
 * pci_addr is static inline (pure computation), pci_bar_* operate on structs.
 * pci_read16/write16 have shift/mask logic testable with a config-space stub.
 */

#include "test_common.h"

#include <alcor2/drivers/pci.h>
#include <alcor2/types.h>

#include <string.h>

/* Config space: 256-byte array indexed by (bus<<16|slot<<11|func<<8|offset). */
static u32 cfgspace[256 / 4]; /* one device worth of 32-bit registers */
static u32 last_addr_write;

#define ALCOR2_IO_H
#define outl(port, val)                                                        \
  ((void)((port) == PCI_CONFIG_ADDR                                            \
              ? (last_addr_write = (u32)(val), 0)                              \
              : ((port) == PCI_CONFIG_DATA                                     \
                     ? (cfgspace[((last_addr_write) & 0xFF) >> 2] =            \
                            (u32)(val),                                        \
                        0)                                                     \
                     : 0)))
#define inl(port)                                                              \
  ((u32)((port) == PCI_CONFIG_DATA ? cfgspace[((last_addr_write) & 0xFF) >> 2] \
                                   : 0))
#define outb(p, v) ((void)(p), (void)(v))
#define inb(p)     ((u8)0)
#define outw(p, v) ((void)(p), (void)(v))
#define inw(p)     ((u16)0)
#define io_wait()  ((void)0)

void console_print(const char *s)
{
  (void)s;
}

#include "../../src/drivers/pci/pci.c"

static int reset(void **state)
{
  (void)state;
  memset(cfgspace, 0, sizeof(cfgspace));
  last_addr_write = 0;
  return 0;
}

/* pci_addr encoding */

static void pci_addr_enable_bit_set(void **state)
{
  (void)state;
  /* Bit 31 must always be set (config-space enable). */
  pci_read32(0, 0, 0, 0);
  assert_true(last_addr_write & 0x80000000u);
}

static void pci_addr_bus_in_bits_23_16(void **state)
{
  (void)state;
  pci_read32(5, 0, 0, 0);
  assert_int_equal((last_addr_write >> 16) & 0xFF, 5);
}

static void pci_addr_slot_in_bits_15_11(void **state)
{
  (void)state;
  pci_read32(0, 3, 0, 0);
  assert_int_equal((last_addr_write >> 11) & 0x1F, 3);
}

static void pci_addr_func_in_bits_10_8(void **state)
{
  (void)state;
  pci_read32(0, 0, 2, 0);
  assert_int_equal((last_addr_write >> 8) & 0x7, 2);
}

static void pci_addr_offset_aligned_to_4(void **state)
{
  (void)state;
  pci_read32(0, 0, 0, 6);
  assert_int_equal(last_addr_write & 0xFF, 4); /* 6 & 0xFC = 4 */
}

/* pci_read16 / pci_write16 shift logic */

static void read16_low_word(void **state)
{
  (void)state;
  cfgspace[0] = 0x12345678;
  assert_int_equal(pci_read16(0, 0, 0, 0), 0x5678);
}

static void read16_high_word(void **state)
{
  (void)state;
  cfgspace[0] = 0x12345678;
  assert_int_equal(pci_read16(0, 0, 0, 2), 0x1234);
}

static void write16_preserves_other_half(void **state)
{
  (void)state;
  cfgspace[0] = 0xAAAABBBB;
  pci_write16(0, 0, 0, 0, 0xCCCC);
  assert_int_equal(cfgspace[0], 0xAAAACCCC);
}

/* pci_bar_is_mmio */

static void bar_io_space_not_mmio(void **state)
{
  (void)state;
  pci_device_t dev = {0};
  dev.bar[0]       = 0x0001; /* bit 0 = I/O space */
  assert_false(pci_bar_is_mmio(&dev, 0));
}

static void bar_mmio(void **state)
{
  (void)state;
  pci_device_t dev = {0};
  dev.bar[0]       = 0xF0000000; /* bit 0 = 0 → MMIO */
  assert_true(pci_bar_is_mmio(&dev, 0));
}

static void bar_mmio_oob_index_returns_false(void **state)
{
  (void)state;
  pci_device_t dev = {0};
  assert_false(pci_bar_is_mmio(&dev, 6));
  assert_false(pci_bar_is_mmio(&dev, -1));
}

/* pci_bar_is_64 */

static void bar_32bit(void **state)
{
  (void)state;
  pci_device_t dev = {0};
  dev.bar[0]       = 0xF0000000; /* type bits [2:1] = 0b00 → 32-bit */
  assert_false(pci_bar_is_64(&dev, 0));
}

static void bar_64bit(void **state)
{
  (void)state;
  pci_device_t dev = {0};
  dev.bar[0]       = 0x4; /* type bits [2:1] = 0b10 → 64-bit */
  assert_true(pci_bar_is_64(&dev, 0));
}

/* pci_bar_base64 */

static void bar_base64_32bit(void **state)
{
  (void)state;
  pci_device_t dev = {0};
  dev.bar[0]       = 0xF0001000; /* MMIO 32-bit, base = strip lower 4 bits */
  assert_int_equal(pci_bar_base64(&dev, 0), 0xF0001000ULL);
}

static void bar_base64_64bit_combines_hi_lo(void **state)
{
  (void)state;
  pci_device_t dev = {0};
  dev.bar[0]       = 0x00001004; /* low, type=64-bit (bits [2:1]=0b10) */
  dev.bar[1]       = 0x00000002; /* high */
  assert_int_equal(pci_bar_base64(&dev, 0), 0x200001000ULL);
}

static void bar_base64_io_strips_lower_bits(void **state)
{
  (void)state;
  pci_device_t dev = {0};
  dev.bar[2]       = 0x0000C001; /* I/O port, addr in upper bits */
  assert_int_equal(pci_bar_base64(&dev, 2), 0xC000ULL);
}

static void bar_base64_oob_returns_zero(void **state)
{
  (void)state;
  pci_device_t dev = {0};
  assert_int_equal(pci_bar_base64(&dev, 6), 0);
  assert_int_equal(pci_bar_base64(&dev, -1), 0);
}

int main(void)
{
  const struct CMUnitTest tests[] = {
      cmocka_unit_test_setup(pci_addr_enable_bit_set, reset),
      cmocka_unit_test_setup(pci_addr_bus_in_bits_23_16, reset),
      cmocka_unit_test_setup(pci_addr_slot_in_bits_15_11, reset),
      cmocka_unit_test_setup(pci_addr_func_in_bits_10_8, reset),
      cmocka_unit_test_setup(pci_addr_offset_aligned_to_4, reset),
      cmocka_unit_test_setup(read16_low_word, reset),
      cmocka_unit_test_setup(read16_high_word, reset),
      cmocka_unit_test_setup(write16_preserves_other_half, reset),
      cmocka_unit_test_setup(bar_io_space_not_mmio, reset),
      cmocka_unit_test_setup(bar_mmio, reset),
      cmocka_unit_test_setup(bar_mmio_oob_index_returns_false, reset),
      cmocka_unit_test_setup(bar_32bit, reset),
      cmocka_unit_test_setup(bar_64bit, reset),
      cmocka_unit_test_setup(bar_base64_32bit, reset),
      cmocka_unit_test_setup(bar_base64_64bit_combines_hi_lo, reset),
      cmocka_unit_test_setup(bar_base64_io_strips_lower_bits, reset),
      cmocka_unit_test_setup(bar_base64_oob_returns_zero, reset),
  };
  return cmocka_run_group_tests(tests, NULL, NULL);
}
