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

static u64 g_phys = 0;
u64        vmm_get_phys(u64 v)
{
  (void)v;
  return g_phys;
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
bool fb_user_phys_page_is_framebuffer(u64 p)
{
  (void)p;
  return false;
}

static proc_t g_proc;
struct proc  *proc_current(void)
{
  return &g_proc;
}
void proc_schedule(void) {}

i64 vfs_seek(i64 fd, i64 off, int w)
{
  (void)fd;
  (void)off;
  (void)w;
  return 0;
}
i64 vfs_read(i64 fd, void *b, u64 n)
{
  (void)fd;
  (void)b;
  (void)n;
  return 0;
}

void *pmm_alloc(void)
{
  return NULL;
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
      /* sys_brk */
      cmocka_unit_test_setup(brk_zero_returns_current_break, setup),
      cmocka_unit_test_setup(brk_below_current_noop, setup),
  };
  return cmocka_run_group_tests(tests, NULL, NULL);
}
