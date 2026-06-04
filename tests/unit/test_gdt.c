/* Unit tests for src/arch/x86_64/gdt.c descriptor encoding. */

#include "test_common.h"

#include <alcor2/arch/gdt.h>
#include <arch/x86_64/gdt_internal.h>

/* gdt_load is the asm routine that actually executes LGDT; stub it so
 * gdt_init() can run on the host. We capture the pointer to inspect the
 * table it built. */
static gdt_ptr_t *g_loaded;
void              gdt_load(gdt_ptr_t *gdtr)
{
  g_loaded = gdtr;
}

/* Mirror of the packed GDT layout that gdt_init() fills. */
typedef struct PACKED
{
  gdt_entry_t     null;
  gdt_entry_t     reserved[4];
  gdt_entry_t     kernel_code;
  gdt_entry_t     kernel_data;
  gdt_entry_t     user_data;
  gdt_entry_t     user_code;
  gdt_tss_entry_t tss;
} gdt_layout_t;

static u64 tss_base_of(const gdt_tss_entry_t *e)
{
  return (u64)e->base_low | ((u64)e->base_mid << 16) |
         ((u64)e->base_high << 24) | ((u64)e->base_upper << 32);
}

/* Independent bit-field oracle for the access/flag values gdt_init uses. */
#define ACC_PRESENT   0x80
#define ACC_RING3     0x60
#define ACC_SEGMENT   0x10
#define ACC_EXEC      0x08
#define ACC_RW        0x02
#define ACC_TSS       0x09
#define FLAG_LONG     0x02
#define FLAG_GRANULAR 0x08

/* gdt_set_entry */

static void set_entry_packs_flat_limit(void **state)
{
  (void)state;
  gdt_entry_t e;
  gdt_set_entry(&e, 0, 0);
  assert_int_equal(e.limit_low, 0xFFFF);
  assert_int_equal(e.flags_limit & 0x0F, 0x0F);
  assert_int_equal(e.base_low, 0);
  assert_int_equal(e.base_mid, 0);
  assert_int_equal(e.base_high, 0);
}

static void set_entry_passes_access_byte(void **state)
{
  (void)state;
  gdt_entry_t e;
  gdt_set_entry(&e, 0x9A, 0);
  assert_int_equal(e.access, 0x9A);
}

static void set_entry_puts_flags_in_high_nibble(void **state)
{
  (void)state;
  gdt_entry_t e;
  gdt_set_entry(&e, 0, FLAG_LONG | FLAG_GRANULAR);
  assert_int_equal((e.flags_limit >> 4) & 0x0F, 0x0A);
}

static void set_entry_flags_overflow_stays_in_nibble(void **state)
{
  (void)state;
  gdt_entry_t e;
  gdt_set_entry(&e, 0, 0xFF);
  assert_int_equal(e.flags_limit & 0x0F, 0x0F);
}

static void kernel_code_access_and_flags(void **state)
{
  (void)state;
  gdt_entry_t e;
  gdt_set_entry(
      &e, ACC_PRESENT | ACC_SEGMENT | ACC_EXEC | ACC_RW,
      FLAG_LONG | FLAG_GRANULAR
  );
  assert_int_equal(e.access, 0x9A);
  assert_int_equal(e.flags_limit, 0xAF);
}

static void kernel_data_access_and_flags(void **state)
{
  (void)state;
  gdt_entry_t e;
  gdt_set_entry(&e, ACC_PRESENT | ACC_SEGMENT | ACC_RW, FLAG_GRANULAR);
  assert_int_equal(e.access, 0x92);
  assert_int_equal(e.flags_limit, 0x8F);
}

static void user_code_is_ring3_long_mode(void **state)
{
  (void)state;
  gdt_entry_t e;
  gdt_set_entry(
      &e, ACC_PRESENT | ACC_RING3 | ACC_SEGMENT | ACC_EXEC | ACC_RW,
      FLAG_LONG | FLAG_GRANULAR
  );
  assert_int_equal(e.access, 0xFA);
  assert_int_equal((e.access >> 5) & 0x3, 3);
}

static void user_data_is_ring3(void **state)
{
  (void)state;
  gdt_entry_t e;
  gdt_set_entry(
      &e, ACC_PRESENT | ACC_RING3 | ACC_SEGMENT | ACC_RW, FLAG_GRANULAR
  );
  assert_int_equal(e.access, 0xF2);
  assert_int_equal((e.access >> 5) & 0x3, 3);
}

/* gdt_set_tss */

static void set_tss_limit_is_sizeof_tss_minus_one(void **state)
{
  (void)state;
  gdt_tss_entry_t e;
  gdt_set_tss(&e, 0);
  assert_int_equal(e.limit_low, sizeof(tss_t) - 1);
  assert_int_equal(e.flags_limit, 0);
}

static void set_tss_access_byte(void **state)
{
  (void)state;
  gdt_tss_entry_t e;
  gdt_set_tss(&e, 0);
  assert_int_equal(e.access, ACC_PRESENT | ACC_TSS);
}

static void set_tss_splits_64bit_base(void **state)
{
  (void)state;
  gdt_tss_entry_t e;
  gdt_set_tss(&e, 0x1122334455667788ULL);
  assert_int_equal(e.base_low, 0x7788);
  assert_int_equal(e.base_mid, 0x66);
  assert_int_equal(e.base_high, 0x55);
  assert_int_equal(e.base_upper, 0x11223344u);
  assert_int_equal(e.reserved, 0);
}

static void set_tss_base_round_trips(void **state)
{
  (void)state;
  gdt_tss_entry_t e;
  u64             base = 0xDEADBEEFCAFEF00DULL;
  gdt_set_tss(&e, base);
  assert_int_equal(tss_base_of(&e), base);
}

/* gdt_init integration (via gdt_load stub) */

static void init_gdtr_limit_and_base(void **state)
{
  (void)state;
  g_loaded = NULL;
  gdt_init();
  assert_non_null(g_loaded);
  assert_int_equal(g_loaded->limit, sizeof(gdt_layout_t) - 1);
  assert_int_not_equal(g_loaded->base, 0);
}

static void init_null_and_reserved_are_zero(void **state)
{
  (void)state;
  gdt_init();
  const gdt_layout_t *g = (const gdt_layout_t *)g_loaded->base;
  const u8           *p = (const u8 *)g;
  for(size_t i = 0; i < sizeof(gdt_entry_t) * 5; i++)
    assert_int_equal(p[i], 0);
}

static void init_kernel_selectors_at_expected_offsets(void **state)
{
  (void)state;
  gdt_init();
  const gdt_layout_t *g = (const gdt_layout_t *)g_loaded->base;
  assert_int_equal((u8 *)&g->kernel_code - (u8 *)g, GDT_KERNEL_CODE);
  assert_int_equal((u8 *)&g->kernel_data - (u8 *)g, GDT_KERNEL_DATA);
  assert_int_equal((u8 *)&g->tss - (u8 *)g, GDT_TSS);
}

static void init_user_data_immediately_before_user_code(void **state)
{
  (void)state;
  /* SYSRET requires user_data at base, user_code at base+16. */
  gdt_init();
  const gdt_layout_t *g = (const gdt_layout_t *)g_loaded->base;
  assert_int_equal(
      (u8 *)&g->user_code - (u8 *)&g->user_data, (int)sizeof(gdt_entry_t)
  );
  assert_int_equal(((u8 *)&g->user_data - (u8 *)g) | 3, GDT_USER_DATA);
  assert_int_equal(((u8 *)&g->user_code - (u8 *)g) | 3, GDT_USER_CODE);
}

static void tss_rsp0_writes_into_live_tss(void **state)
{
  (void)state;
  gdt_init();
  const gdt_layout_t *g    = (const gdt_layout_t *)g_loaded->base;
  const tss_t        *live = (const tss_t *)tss_base_of(&g->tss);
  tss_set_rsp0(0xFFFF800000001000ULL);
  assert_int_equal(live->rsp0, 0xFFFF800000001000ULL);
  tss_set_rsp0(0);
  assert_int_equal(live->rsp0, 0);
}

int main(void)
{
  const struct CMUnitTest tests[] = {
      cmocka_unit_test(set_entry_packs_flat_limit),
      cmocka_unit_test(set_entry_passes_access_byte),
      cmocka_unit_test(set_entry_puts_flags_in_high_nibble),
      cmocka_unit_test(set_entry_flags_overflow_stays_in_nibble),
      cmocka_unit_test(kernel_code_access_and_flags),
      cmocka_unit_test(kernel_data_access_and_flags),
      cmocka_unit_test(user_code_is_ring3_long_mode),
      cmocka_unit_test(user_data_is_ring3),
      cmocka_unit_test(set_tss_limit_is_sizeof_tss_minus_one),
      cmocka_unit_test(set_tss_access_byte),
      cmocka_unit_test(set_tss_splits_64bit_base),
      cmocka_unit_test(set_tss_base_round_trips),
      cmocka_unit_test(init_gdtr_limit_and_base),
      cmocka_unit_test(init_null_and_reserved_are_zero),
      cmocka_unit_test(init_kernel_selectors_at_expected_offsets),
      cmocka_unit_test(init_user_data_immediately_before_user_code),
      cmocka_unit_test(tss_rsp0_writes_into_live_tss),
  };
  return cmocka_run_group_tests(tests, NULL, NULL);
}
