/* Unit tests for ATA driver pure logic: trim_string and the read-side block
 * cache. Transport (DMA/PIO/identify/init/irq) is hardware and goes to the
 * QEMU runner. The stubs below are link-only — none of the tested paths
 * execute them. */

#include "test_common.h"

#include <drivers/ata/ata_internal.h>

#include <string.h>

/* Link-only stubs for hardware/kernel symbols pulled in by untested paths. */
u8 inb(u16 p)
{
  (void)p;
  return 0;
}
void outb(u16 p, u8 v)
{
  (void)p;
  (void)v;
}
u16 inw(u16 p)
{
  (void)p;
  return 0;
}
void outw(u16 p, u16 v)
{
  (void)p;
  (void)v;
}
u32 inl(u16 p)
{
  (void)p;
  return 0;
}
void outl(u16 p, u32 v)
{
  (void)p;
  (void)v;
}
void console_print(const char *s)
{
  (void)s;
}
void console_printf(const char *fmt, ...)
{
  (void)fmt;
}
void  cpu_pause(void) {}
void  cpu_disable_interrupts(void) {}
void  cpu_enable_interrupts(void) {}
void *kmemcpy(void *d, const void *s, u64 n)
{
  return memcpy(d, s, n);
}
u64 vmm_get_hhdm(void)
{
  return 0;
}

struct proc;
struct proc *proc_current(void)
{
  return NULL;
}
void proc_block(struct proc *p)
{
  (void)p;
}
void proc_wake(struct proc *p)
{
  (void)p;
}
void proc_schedule(void) {}
u64  pit_get_ticks(void)
{
  return 0;
}
void pic_unmask(u8 irq)
{
  (void)irq;
}
void irq_register(u8 irq, void (*h)(u8))
{
  (void)irq;
  (void)h;
}
void *pmm_alloc(void)
{
  return NULL;
}
void *pmm_alloc_pages(u64 n)
{
  (void)n;
  return NULL;
}
void pmm_free(void *p)
{
  (void)p;
}
void pmm_free_pages(void *p, u64 n)
{
  (void)p;
  (void)n;
}
void *phys_to_virt(u64 p)
{
  (void)p;
  return NULL;
}
int pci_find_device(u8 c, u8 s, void *o)
{
  (void)c;
  (void)s;
  (void)o;
  return 0;
}
void pci_enable_bus_master(void *d)
{
  (void)d;
}

/* Reset cache state between tests. */
static int reset(void **state)
{
  (void)state;
  g_cache_inited  = 0;
  g_cache_counter = 0;
  memset(g_ata_cache, 0, sizeof(g_ata_cache));
  cache_init_once();
  return 0;
}

/* trim_string */

static void trim_trailing_spaces(void **state)
{
  (void)state;
  char s[] = "ab      ";
  trim_string(s, 8);
  assert_string_equal(s, "ab");
}

static void trim_trailing_nuls(void **state)
{
  (void)state;
  char s[8] = {'a', 'b', 0, 0, 0, 0, 0, 0};
  trim_string(s, 6);
  assert_string_equal(s, "ab");
}

static void trim_all_spaces_yields_empty(void **state)
{
  (void)state;
  char s[] = "    ";
  trim_string(s, 4);
  assert_string_equal(s, "");
}

static void trim_no_trailing_unchanged(void **state)
{
  (void)state;
  char s[] = "hello";
  trim_string(s, 5);
  assert_string_equal(s, "hello");
}

static void trim_len_zero_writes_nul_at_start(void **state)
{
  (void)state;
  char s[] = "abc";
  trim_string(s, 0);
  assert_int_equal(s[0], '\0');
}

/* cache_init_once */

static void init_marks_all_slots_invalid(void **state)
{
  (void)state;
  for(int i = 0; i < CACHE_NUM_ENTRIES; i++)
    assert_int_equal(g_ata_cache[i].block_lba, CACHE_INVALID_LBA);
}

static void init_is_idempotent(void **state)
{
  (void)state;
  g_ata_cache[0].block_lba = 8;
  cache_init_once();
  assert_int_equal(g_ata_cache[0].block_lba, 8);
}

/* cache_lookup */

static void lookup_miss_returns_null(void **state)
{
  (void)state;
  assert_null(cache_lookup(0, 0));
}

static void lookup_hit_returns_correct_entry(void **state)
{
  (void)state;
  g_ata_cache[7].block_lba = 64;
  g_ata_cache[7].drive     = 1;
  assert_ptr_equal(cache_lookup(1, 64), &g_ata_cache[7]);
}

static void lookup_hit_bumps_lru_counter(void **state)
{
  (void)state;
  g_ata_cache[3].block_lba = 16;
  g_ata_cache[3].drive     = 0;
  u64 before               = g_cache_counter;
  cache_lookup(0, 16);
  assert_true(g_ata_cache[3].last_used > before);
}

static void lookup_does_not_match_wrong_drive(void **state)
{
  (void)state;
  g_ata_cache[2].block_lba = 32;
  g_ata_cache[2].drive     = 0;
  assert_null(cache_lookup(1, 32));
  assert_non_null(cache_lookup(0, 32));
}

/* cache_alloc */

static void alloc_returns_free_slot_first(void **state)
{
  (void)state;
  assert_ptr_equal(cache_alloc(), &g_ata_cache[0]);
}

static void alloc_evicts_lru_when_all_full(void **state)
{
  (void)state;
  for(int i = 0; i < CACHE_NUM_ENTRIES; i++) {
    g_ata_cache[i].block_lba = (u64)i * CACHE_BLOCK_SECTORS;
    g_ata_cache[i].last_used = 1000 + (u64)i;
  }
  g_ata_cache[42].last_used = 1;
  assert_ptr_equal(cache_alloc(), &g_ata_cache[42]);
}

/* cache_invalidate_range */

static void invalidate_overlapping_block(void **state)
{
  (void)state;
  g_ata_cache[0].block_lba = 0;
  g_ata_cache[0].drive     = 0;
  cache_invalidate_range(0, 3, 2);
  assert_int_equal(g_ata_cache[0].block_lba, CACHE_INVALID_LBA);
}

static void invalidate_keeps_disjoint_block(void **state)
{
  (void)state;
  g_ata_cache[0].block_lba = 64;
  g_ata_cache[0].drive     = 0;
  cache_invalidate_range(0, 0, 8);
  assert_int_equal(g_ata_cache[0].block_lba, 64);
}

static void invalidate_keeps_adjacent_block(void **state)
{
  (void)state;
  /* Block [8,16) vs range [0,8): adjacent, no overlap. */
  g_ata_cache[0].block_lba = 8;
  g_ata_cache[0].drive     = 0;
  cache_invalidate_range(0, 0, 8);
  assert_int_equal(g_ata_cache[0].block_lba, 8);
}

static void invalidate_respects_drive_id(void **state)
{
  (void)state;
  g_ata_cache[0].block_lba = 0;
  g_ata_cache[0].drive     = 1;
  cache_invalidate_range(0, 0, 8);
  assert_int_equal(g_ata_cache[0].block_lba, 0);
}

static void invalidate_skips_free_slots(void **state)
{
  (void)state;
  cache_invalidate_range(0, 0, (u32)-1);
  for(int i = 0; i < CACHE_NUM_ENTRIES; i++)
    assert_int_equal(g_ata_cache[i].block_lba, CACHE_INVALID_LBA);
}

int main(void)
{
  const struct CMUnitTest tests[] = {
      cmocka_unit_test(trim_trailing_spaces),
      cmocka_unit_test(trim_trailing_nuls),
      cmocka_unit_test(trim_all_spaces_yields_empty),
      cmocka_unit_test(trim_no_trailing_unchanged),
      cmocka_unit_test(trim_len_zero_writes_nul_at_start),
      cmocka_unit_test_setup(init_marks_all_slots_invalid, reset),
      cmocka_unit_test_setup(init_is_idempotent, reset),
      cmocka_unit_test_setup(lookup_miss_returns_null, reset),
      cmocka_unit_test_setup(lookup_hit_returns_correct_entry, reset),
      cmocka_unit_test_setup(lookup_hit_bumps_lru_counter, reset),
      cmocka_unit_test_setup(lookup_does_not_match_wrong_drive, reset),
      cmocka_unit_test_setup(alloc_returns_free_slot_first, reset),
      cmocka_unit_test_setup(alloc_evicts_lru_when_all_full, reset),
      cmocka_unit_test_setup(invalidate_overlapping_block, reset),
      cmocka_unit_test_setup(invalidate_keeps_disjoint_block, reset),
      cmocka_unit_test_setup(invalidate_keeps_adjacent_block, reset),
      cmocka_unit_test_setup(invalidate_respects_drive_id, reset),
      cmocka_unit_test_setup(invalidate_skips_free_slots, reset),
  };
  return cmocka_run_group_tests(tests, NULL, NULL);
}
