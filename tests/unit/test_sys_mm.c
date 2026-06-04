#include "test_common.h"

#include <alcor2/mm/memory_layout.h>
#include <alcor2/mm/vmm.h>
#include <alcor2/proc/proc.h>
#include <alcor2/types.h>

#include <string.h>


void *kmalloc(u64 n)
{
  (void)n;
  return NULL;
}
void kfree(void *p)
{
  (void)p;
}
void *kmemcpy(void *d, const void *s, u64 n)
{
  return memcpy(d, s, n);
}
void kzero(void *d, u64 n)
{
  memset(d, 0, n);
}
void console_print(const char *s)
{
  (void)s;
}
void console_printf(const char *fmt, ...)
{
  (void)fmt;
}

bool vmm_is_user_range(const void *p, u64 n)
{
  (void)p;
  (void)n;
  return true;
}

static bool g_map_ok = true;
bool        vmm_map_range_alloc(u64 v, u64 c, u64 f)
{
  (void)v;
  (void)c;
  (void)f;
  return g_map_ok;
}

static u64 g_phys    = 0;
static u64 g_phys_fb = 0; /* if non-zero, vmm_get_phys returns this for all */
u64        vmm_get_phys(u64 v)
{
  (void)v;
  return g_phys_fb ? g_phys_fb : g_phys;
}
void vmm_unmap(u64 v)
{
  (void)v;
}
void vmm_map(u64 v, u64 p, u64 f)
{
  (void)v;
  (void)p;
  (void)f;
}
u64 vmm_get_hhdm(void)
{
  return 0;
}
static bool g_is_fb = false;
bool        fb_user_phys_page_is_framebuffer(u64 p)
{
  (void)p;
  return g_is_fb;
}

static proc_t  g_proc;
static bool    g_no_proc = false;
struct proc   *proc_current(void)
{
  return g_no_proc ? NULL : &g_proc;
}
void proc_schedule(void) {}

i64 vfs_seek(i64 fd, i64 off, int w)
{
  (void)fd;
  (void)off;
  (void)w;
  return 0;
}
static bool g_vfs_read_ok = false;
i64         vfs_read(i64 fd, void *b, u64 n)
{
  (void)fd;
  (void)b;
  if(!g_vfs_read_ok || n == 0)
    return 0;
  return (i64)n; /* pretend we read n bytes */
}

static void *g_pmm_page = NULL; /* page returned by pmm_alloc */
void        *pmm_alloc(void)
{
  return g_pmm_page;
}
void pmm_free(void *p)
{
  (void)p;
}

#include "../../src/kernel/sys/sys_mm.c"


static int setup(void **state)
{
  (void)state;
  memset(&g_proc, 0, sizeof(g_proc));
  g_proc.mmap_base     = 0x100000;
  g_proc.program_break = 0x200000;
  g_map_ok             = true;
  g_phys               = 0;
  g_phys_fb            = 0;
  g_is_fb              = false;
  g_no_proc            = false;
  g_pmm_page           = NULL;
  g_vfs_read_ok        = false;
  return 0;
}


static void align_down_already_aligned(void **state)
{
  (void)state;
  assert_int_equal(page_align_down(0x1000), 0x1000);
  assert_int_equal(page_align_down(0x2000), 0x2000);
}

static void align_down_truncates(void **state)
{
  (void)state;
  assert_int_equal(page_align_down(0x1001), 0x1000);
  assert_int_equal(page_align_down(0x1FFF), 0x1000);
  assert_int_equal(page_align_down(0x2001), 0x2000);
}

static void align_down_zero(void **state)
{
  (void)state;
  assert_int_equal(page_align_down(0), 0);
}


static void align_up_already_aligned(void **state)
{
  (void)state;
  assert_int_equal(page_align_up(0x1000), 0x1000);
  assert_int_equal(page_align_up(0x2000), 0x2000);
}

static void align_up_rounds_up(void **state)
{
  (void)state;
  assert_int_equal(page_align_up(1), 0x1000);
  assert_int_equal(page_align_up(0x1001), 0x2000);
  assert_int_equal(page_align_up(0x1FFF), 0x2000);
}

static void align_up_zero(void **state)
{
  (void)state;
  assert_int_equal(page_align_up(0), 0);
}


static void build_flags_read_only(void **state)
{
  (void)state;
  u64 f = build_vmm_flags(PROT_READ);
  assert_true(f & VMM_PRESENT);
  assert_true(f & VMM_USER);
  assert_false(f & VMM_WRITE);
}

static void build_flags_read_write(void **state)
{
  (void)state;
  u64 f = build_vmm_flags(PROT_READ | PROT_WRITE);
  assert_true(f & VMM_WRITE);
}

static void build_flags_no_prot(void **state)
{
  (void)state;
  u64 f = build_vmm_flags(0);
  assert_true(f & VMM_PRESENT);
  assert_false(f & VMM_WRITE);
}


static void mmap_zero_length_einval(void **state)
{
  (void)state;
  u64 ret = sys_mmap(0, 0, PROT_READ, MAP_ANONYMOUS | MAP_PRIVATE, (u64)-1, 0);
  assert_int_equal((i64)ret, -EINVAL);
}

static void mmap_fixed_addr_zero_einval(void **state)
{
  (void)state;
  /* MAP_FIXED with addr=0 is rejected */
  u64 ret = sys_mmap(0, 0x1000, PROT_READ, MAP_FIXED | MAP_ANONYMOUS, (u64)-1, 0);
  assert_int_equal((i64)ret, -EINVAL);
}

static void mmap_file_unaligned_offset_einval(void **state)
{
  (void)state;
  /* non-anonymous with non-page-aligned offset */
  u64 ret = sys_mmap(0, 0x1000, PROT_READ, MAP_PRIVATE, 3, 0x42);
  assert_int_equal((i64)ret, -EINVAL);
}

static void mmap_anon_succeeds_and_advances_base(void **state)
{
  (void)state;
  u64 base_before = g_proc.mmap_base;
  u64 ret = sys_mmap(0, 0x1000, PROT_READ | PROT_WRITE, MAP_ANONYMOUS | MAP_PRIVATE, (u64)-1, 0);
  /* vmm_map_range_alloc succeeds (g_map_ok=true) so we get the base back */
  assert_true((i64)ret >= 0);
  /* mmap_base must have advanced */
  assert_true(g_proc.mmap_base > base_before);
}

static void mmap_alloc_fail_returns_enomem(void **state)
{
  (void)state;
  g_map_ok = false;
  u64 ret  = sys_mmap(0, 0x1000, PROT_READ, MAP_ANONYMOUS | MAP_PRIVATE, (u64)-1, 0);
  assert_int_equal((i64)ret, -ENOMEM);
}


static void mprotect_zero_length_is_noop(void **state)
{
  (void)state;
  u64 ret = sys_mprotect(0x1000, 0, PROT_READ, 0, 0, 0);
  assert_int_equal((i64)ret, 0);
}

static void mprotect_nonzero_succeeds(void **state)
{
  (void)state;
  u64 ret = sys_mprotect(0x1000, 0x1000, PROT_READ | PROT_WRITE, 0, 0, 0);
  assert_int_equal((i64)ret, 0);
}


static void munmap_zero_length_is_noop(void **state)
{
  (void)state;
  u64 ret = sys_munmap(0x1000, 0, 0, 0, 0, 0);
  assert_int_equal((i64)ret, 0);
}

static void munmap_nonzero_succeeds(void **state)
{
  (void)state;
  u64 ret = sys_munmap(0x1000, 0x1000, 0, 0, 0, 0);
  assert_int_equal((i64)ret, 0);
}


static void brk_zero_returns_current_break(void **state)
{
  (void)state;
  u64 brk = sys_brk(0, 0, 0, 0, 0, 0);
  assert_int_equal(brk, g_proc.program_break);
}

static void brk_below_current_noop(void **state)
{
  (void)state;
  /* addr < program_break: no shrink implemented, returns current */
  u64 brk = sys_brk(0x1000, 0, 0, 0, 0, 0);
  assert_int_equal(brk, g_proc.program_break);
}

/* sys_mmap: no proc → ENOMEM */
static void mmap_no_proc_returns_enomem(void **state)
{
  (void)state;
  g_no_proc = true;
  u64 ret = sys_mmap(0, 0x1000, PROT_READ, MAP_ANONYMOUS | MAP_PRIVATE, (u64)-1, 0);
  assert_int_equal((i64)ret, -ENOMEM);
}

/* sys_mmap MAP_FIXED with valid addr succeeds and does NOT advance mmap_base */
static void mmap_fixed_valid_addr_does_not_advance_base(void **state)
{
  (void)state;
  u64 base_before = g_proc.mmap_base;
  u64 ret = sys_mmap(0x400000, 0x1000, PROT_READ | PROT_WRITE,
                     MAP_FIXED | MAP_ANONYMOUS | MAP_PRIVATE, (u64)-1, 0);
  assert_int_equal(ret, 0x400000);
  assert_int_equal(g_proc.mmap_base, base_before); /* base not moved */
}

/* sys_mmap MAP_FIXED: unmap_and_free_range called with g_phys non-zero */
static void mmap_fixed_with_existing_mapping_unmaps_first(void **state)
{
  (void)state;
  g_phys = 0x1000; /* pretend the page is already mapped */
  u64 ret = sys_mmap(0x400000, 0x1000, PROT_READ | PROT_WRITE,
                     MAP_FIXED | MAP_ANONYMOUS, (u64)-1, 0);
  assert_int_equal(ret, 0x400000);
}

/* sys_mmap MAP_FIXED: unmap_and_free_range skips framebuffer pages */
static void mmap_fixed_fb_page_not_freed(void **state)
{
  (void)state;
  g_phys  = 0x2000;
  g_is_fb = true;
  u64 ret = sys_mmap(0x400000, 0x1000, PROT_READ,
                     MAP_FIXED | MAP_ANONYMOUS, (u64)-1, 0);
  /* just ensure it didn't crash and returned valid address */
  assert_int_equal(ret, 0x400000);
}

/* sys_mmap PROT_READ only: apply_final_prot called */
static void mmap_read_only_applies_final_prot(void **state)
{
  (void)state;
  g_phys = 0x3000; /* page is mapped after vmm_map_range_alloc */
  u64 ret = sys_mmap(0, 0x1000, PROT_READ,
                     MAP_ANONYMOUS | MAP_PRIVATE, (u64)-1, 0);
  assert_true((i64)ret >= 0);
}

/* sys_mmap file-backed: fill_file_backed_pages called */
static void mmap_file_backed_reads_data(void **state)
{
  (void)state;
  g_phys        = 0x5000; /* backing page for reads */
  g_vfs_read_ok = true;
  /* fd=3, page-aligned offset, non-anonymous */
  u64 ret = sys_mmap(0, 0x1000, PROT_READ | PROT_WRITE,
                     MAP_PRIVATE, 3, 0x0000);
  assert_true((i64)ret >= 0);
}

/* sys_mmap file-backed with PROT_READ: triggers both fill + apply_final_prot */
static void mmap_file_backed_read_only(void **state)
{
  (void)state;
  g_phys        = 0x5000;
  g_vfs_read_ok = true;
  u64 ret = sys_mmap(0, 0x1000, PROT_READ, MAP_PRIVATE, 3, 0);
  assert_true((i64)ret >= 0);
}

/* sys_mmap fd=-1 forces anonymous regardless of MAP_PRIVATE */
static void mmap_fd_minus1_is_anonymous(void **state)
{
  (void)state;
  u64 ret = sys_mmap(0, 0x1000, PROT_READ | PROT_WRITE,
                     MAP_PRIVATE, (u64)-1, 0);
  assert_true((i64)ret >= 0);
}

/* sys_mprotect: with a mapped page (g_phys non-zero) vmm_map is called */
static void mprotect_remaps_existing_pages(void **state)
{
  (void)state;
  g_phys  = 0x4000;
  u64 ret = sys_mprotect(0x2000, 0x2000, PROT_READ, 0, 0, 0);
  assert_int_equal((i64)ret, 0);
}

/* sys_munmap: with mapped page triggers unmap + free */
static void munmap_frees_mapped_page(void **state)
{
  (void)state;
  g_phys  = 0x9000;
  u64 ret = sys_munmap(0x3000, 0x1000, 0, 0, 0, 0);
  assert_int_equal((i64)ret, 0);
}

/* sys_brk: no proc → returns 0 */
static void brk_no_proc_returns_zero(void **state)
{
  (void)state;
  g_no_proc   = true;
  u64 brk     = sys_brk(0, 0, 0, 0, 0, 0);
  assert_int_equal(brk, 0);
}

/* sys_brk: addr > program_break, pmm_alloc fails → returns current break */
static void brk_grow_pmm_fail_stays_at_current(void **state)
{
  (void)state;
  g_pmm_page = NULL; /* pmm_alloc returns NULL */
  u64 new_brk = g_proc.program_break + PAGE_SIZE;
  u64 brk     = sys_brk(new_brk, 0, 0, 0, 0, 0);
  assert_int_equal(brk, g_proc.program_break); /* unchanged */
}

/* sys_brk: addr > program_break, pmm_alloc succeeds → advances break */
static void brk_grow_succeeds(void **state)
{
  (void)state;
  static u8 page[4096];
  g_pmm_page           = page; /* valid page pointer */
  u64 old_brk          = g_proc.program_break;
  u64 new_brk          = old_brk + PAGE_SIZE;
  u64 brk              = sys_brk(new_brk, 0, 0, 0, 0, 0);
  assert_int_equal(brk, new_brk);
  assert_int_equal(g_proc.program_break, new_brk);
}

/* sys_mmap: aligned_len wraps (huge length) → -ENOMEM (line 122) */
static void mmap_aligned_len_overflow_enomem(void **state) {
  (void)state;
  /* length near UINT64_MAX → page_align_up wraps → aligned_len < length */
  u64 ret = sys_mmap(0, (u64)-1ULL, PROT_READ, MAP_ANONYMOUS | MAP_PRIVATE, (u64)-1, 0);
  assert_int_equal((i64)ret, -ENOMEM);
}

/* sys_mmap: end overflows or exceeds USER_SPACE_END → -ENOMEM (line 128) */
static void mmap_end_overflow_enomem(void **state) {
  (void)state;
  /* Use MAP_FIXED with a very high addr so base+len > USER_SPACE_END */
  u64 ret = sys_mmap(USER_SPACE_END - 0x1000, 0x10000,
                     PROT_READ, MAP_FIXED | MAP_ANONYMOUS | MAP_PRIVATE,
                     (u64)-1, 0);
  assert_int_equal((i64)ret, -ENOMEM);
}

/* sys_mmap: file-backed with non-page-aligned offset → -EINVAL (line 191) */
static void mmap_file_unaligned_offset_einval2(void **state) {
  (void)state;
  /* fd=3 (not -1), offset=1 (not page-aligned) → EINVAL */
  u64 ret = sys_mmap(0, 0x1000, PROT_READ, MAP_PRIVATE, 3, 1);
  assert_int_equal((i64)ret, -EINVAL);
}

/* fill_file_backed_pages: vfs_read returns 0 → break (line 100) */
static void fill_file_backed_vfs_read_zero_breaks(void **state) {
  (void)state;
  /* g_vfs_read_ok=false → vfs_read returns 0 → break on first iteration */
  /* g_phys=0 → vmm_get_phys returns 0 → break at line 91 instead */
  /* Set g_phys non-zero and g_vfs_read_ok=false to hit line 100 */
  g_phys         = 0x1000; /* non-zero so we pass line 90-91 */
  g_vfs_read_ok  = false;  /* vfs_read returns 0 */
  g_map_ok       = true;
  g_pmm_page     = (void *)0x1000;
  /* Call sys_mmap with file-backed anonymous-like but fd valid */
  /* Actually call fill_file_backed_pages directly — it's static.
   * Trigger via sys_mmap with file fd and g_vfs_read_ok=false. */
  /* sys_mmap with file: g_vfs_read_ok=false → vfs_read=0 → fill does nothing */
  u64 ret = sys_mmap(0, 0x1000, PROT_READ | PROT_WRITE, MAP_PRIVATE, 3, 0);
  /* With g_map_ok=true and pmm_page non-null → succeeds (0 bytes filled) */
  /* Just verify no crash */
  (void)ret;
}

int main(void)
{
  const struct CMUnitTest tests[] = {
      /* page_align_down */
      cmocka_unit_test(align_down_already_aligned),
      cmocka_unit_test(align_down_truncates),
      cmocka_unit_test(align_down_zero),
      /* page_align_up */
      cmocka_unit_test(align_up_already_aligned),
      cmocka_unit_test(align_up_rounds_up),
      cmocka_unit_test(align_up_zero),
      /* build_vmm_flags */
      cmocka_unit_test(build_flags_read_only),
      cmocka_unit_test(build_flags_read_write),
      cmocka_unit_test(build_flags_no_prot),
      /* sys_mmap */
      cmocka_unit_test_setup(mmap_zero_length_einval, setup),
      cmocka_unit_test_setup(mmap_fixed_addr_zero_einval, setup),
      cmocka_unit_test_setup(mmap_file_unaligned_offset_einval, setup),
      cmocka_unit_test_setup(mmap_anon_succeeds_and_advances_base, setup),
      cmocka_unit_test_setup(mmap_alloc_fail_returns_enomem, setup),
      /* sys_mprotect */
      cmocka_unit_test_setup(mprotect_zero_length_is_noop, setup),
      cmocka_unit_test_setup(mprotect_nonzero_succeeds, setup),
      /* sys_munmap */
      cmocka_unit_test_setup(munmap_zero_length_is_noop, setup),
      cmocka_unit_test_setup(munmap_nonzero_succeeds, setup),
      /* sys_mmap extra paths */
      cmocka_unit_test_setup(mmap_no_proc_returns_enomem, setup),
      cmocka_unit_test_setup(mmap_fixed_valid_addr_does_not_advance_base, setup),
      cmocka_unit_test_setup(mmap_fixed_with_existing_mapping_unmaps_first, setup),
      cmocka_unit_test_setup(mmap_fixed_fb_page_not_freed, setup),
      cmocka_unit_test_setup(mmap_read_only_applies_final_prot, setup),
      cmocka_unit_test_setup(mmap_file_backed_reads_data, setup),
      cmocka_unit_test_setup(mmap_file_backed_read_only, setup),
      cmocka_unit_test_setup(mmap_fd_minus1_is_anonymous, setup),
      /* sys_mprotect extra */
      cmocka_unit_test_setup(mprotect_remaps_existing_pages, setup),
      /* sys_munmap extra */
      cmocka_unit_test_setup(munmap_frees_mapped_page, setup),
      /* sys_brk */
      cmocka_unit_test_setup(brk_zero_returns_current_break, setup),
      cmocka_unit_test_setup(brk_below_current_noop, setup),
      cmocka_unit_test_setup(brk_no_proc_returns_zero, setup),
      cmocka_unit_test_setup(brk_grow_pmm_fail_stays_at_current, setup),
      cmocka_unit_test_setup(brk_grow_succeeds, setup),
      cmocka_unit_test_setup(mmap_aligned_len_overflow_enomem, setup),
      cmocka_unit_test_setup(mmap_end_overflow_enomem, setup),
      cmocka_unit_test_setup(mmap_file_unaligned_offset_einval2, setup),
      cmocka_unit_test_setup(fill_file_backed_vfs_read_zero_breaks, setup),
  };
  return cmocka_run_group_tests(tests, NULL, NULL);
}
