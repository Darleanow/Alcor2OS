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

/* pci_for_each / pci_find_device */

static void for_each_finds_device_by_class(void **state)
{
  (void)state;
  /* Place a device at bus=0, slot=0, func=0.
   * Layout: vendor=0x8086, device=0x1234, class=0x01 (storage), sub=0x00 */
  cfgspace[PCI_VENDOR_ID / 4] = 0x12348086u; /* device_id<<16 | vendor_id */
  /* class at byte 0x0B inside dword 0x08: bits [31:24] */
  cfgspace[0x08 / 4] = (0x01u << 24); /* class=0x01 */
  /* subclass at byte 0x0A: bits [23:16] */
  cfgspace[0x08 / 4] |= (0x00u << 16);
  /* header_type at 0x0E: single-function (bit7=0) */
  cfgspace[0x0C / 4] = 0x00;

  pci_device_t out;
  bool         found = pci_find_device(0x01, 0x00, &out);
  assert_true(found);
  assert_int_equal(out.vendor_id, 0x8086);
  assert_int_equal(out.device_id, 0x1234);
  assert_int_equal(out.class_code, 0x01);
}

static void for_each_returns_false_when_all_0xffff(void **state)
{
  (void)state;
  /* All vendor IDs = 0xFFFF (empty bus) */
  for(size_t i = 0; i < sizeof(cfgspace) / sizeof(cfgspace[0]); i++)
    cfgspace[i] = 0xFFFFFFFF;
  pci_device_t out;
  assert_false(pci_find_device(0x01, 0x00, &out));
}

/* pci_find_capability */

static void find_capability_present(void **state)
{
  (void)state;
  /* status register: bit 4 = capabilities list present */
  cfgspace[PCI_STATUS / 4] = PCI_STATUS_CAP_LIST << ((PCI_STATUS & 2) * 8);
  /* vendor_id must be non-0xFFFF */
  cfgspace[PCI_VENDOR_ID / 4] = 0x00018086u;

  /* cap ptr at 0x34 points to 0x40 */
  cfgspace[0x34 / 4] = 0x40;
  /* at 0x40: id=0x05 (MSI), next=0x00 */
  cfgspace[0x40 / 4] = 0x0005;

  pci_device_t dev = {0};
  dev.bus          = 0;
  dev.slot         = 0;
  dev.func         = 0;

  u8 off = 0;
  assert_true(pci_find_capability(&dev, 0x05, &off));
  assert_int_equal(off, 0x40);
}

static void find_capability_absent(void **state)
{
  (void)state;
  cfgspace[PCI_STATUS / 4]    = PCI_STATUS_CAP_LIST << ((PCI_STATUS & 2) * 8);
  cfgspace[PCI_VENDOR_ID / 4] = 0x00018086u;
  cfgspace[0x34 / 4]          = 0x40;
  cfgspace[0x40 / 4]          = 0x0004; /* id=0x04 (power mgmt), next=0x00 */

  pci_device_t dev = {0};
  dev.bus          = 0;
  dev.slot         = 0;
  dev.func         = 0;

  assert_false(pci_find_capability(&dev, 0x05, NULL));
}

static void find_capability_no_cap_list(void **state)
{
  (void)state;
  /* status bit 4 cleared */
  cfgspace[PCI_STATUS / 4] = 0;
  pci_device_t dev         = {0};
  assert_false(pci_find_capability(&dev, 0x05, NULL));
}

/* pci_find_capability_next: walk from a given cap to find the next one */
static void find_capability_next_present(void **state)
{
  (void)state;
  cfgspace[PCI_VENDOR_ID / 4] = 0x00018086u;
  /* cap at 0x40: id=0x04, next=0x50 */
  cfgspace[0x40 / 4] = 0x5004; /* next=0x50, id=0x04 */
  /* cap at 0x50: id=0x05, next=0x00 */
  cfgspace[0x50 / 4] = 0x0005;

  pci_device_t dev = {.bus=0, .slot=0, .func=0};
  u8 off = 0;
  /* Start walking from 0x40 looking for id=0x05 */
  assert_true(pci_find_capability_next(&dev, 0x40, 0x05, &off));
  assert_int_equal(off, 0x50);
}

static void find_capability_next_absent(void **state)
{
  (void)state;
  cfgspace[0x40 / 4] = 0x0004; /* id=0x04, next=0x00 */
  pci_device_t dev = {.bus=0, .slot=0, .func=0};
  assert_false(pci_find_capability_next(&dev, 0x40, 0x05, NULL));
}

/* pci_enable_bus_master: sets bus master + I/O + memory bits in CMD */
static void enable_bus_master_sets_bits(void **state)
{
  (void)state;
  cfgspace[PCI_VENDOR_ID / 4] = 0x00018086u;
  cfgspace[PCI_COMMAND / 4]   = 0; /* no bits set */
  pci_device_t dev = {.bus=0, .slot=0, .func=0};
  pci_enable_bus_master(&dev);
  u16 cmd = pci_read16(dev.bus, dev.slot, dev.func, PCI_COMMAND);
  assert_true(cmd & PCI_CMD_MASTER);
  assert_true(cmd & PCI_CMD_IO);
  assert_true(cmd & PCI_CMD_MEMORY);
}

/* pci_find_device: class match found */
static void find_device_by_class_found(void **state)
{
  (void)state;
  /* Set vendor=0x8086, device=0x1234 in slot 0 */
  cfgspace[PCI_VENDOR_ID / 4] = 0x12348086u;
  /* Set class=0x0101 (IDE), subclass in bits[23:16] */
  /* class code at offset 0x0A (bits 31:16 of dword 0x08) */
  cfgspace[0x08 / 4] = (0x01u << 24) | (0x01u << 16); /* class=0x01, sub=0x01 */

  pci_device_t dev;
  assert_true(pci_find_device(0x01, 0x01, &dev));
}

/* pci_find_device: all 0xFFFF — device absent */
static void find_device_all_ff_not_found(void **state)
{
  (void)state;
  /* All cfgspace is 0 which means vendor=0 → pci_for_each skips 0 too.
     Set vendor to 0xFFFF to simulate absent device. */
  cfgspace[PCI_VENDOR_ID / 4] = 0xFFFFFFFFu;
  pci_device_t dev;
  assert_false(pci_find_device(0x01, 0x01, &dev));
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
      cmocka_unit_test_setup(for_each_finds_device_by_class, reset),
      cmocka_unit_test_setup(for_each_returns_false_when_all_0xffff, reset),
      cmocka_unit_test_setup(find_capability_present, reset),
      cmocka_unit_test_setup(find_capability_absent, reset),
      cmocka_unit_test_setup(find_capability_no_cap_list, reset),
      cmocka_unit_test_setup(find_capability_next_present, reset),
      cmocka_unit_test_setup(find_capability_next_absent, reset),
      cmocka_unit_test_setup(enable_bus_master_sets_bits, reset),
      cmocka_unit_test_setup(find_device_by_class_found, reset),
      cmocka_unit_test_setup(find_device_all_ff_not_found, reset),
  };
  return cmocka_run_group_tests(tests, NULL, NULL);
}
