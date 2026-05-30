#include "test_common.h"

#include <alcor2/mm/memory_layout.h>
#include <alcor2/mm/vmm.h>
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
bool vmm_map_range_alloc(u64 v, u64 c, u64 f)
{
  (void)v;
  (void)c;
  (void)f;
  return true;
}
u64 vmm_get_phys(u64 v)
{
  (void)v;
  return 0;
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

struct proc;
struct proc *proc_current(void)
{
  return NULL;
}
void proc_schedule(void) {}

i64  vfs_seek(i64 fd, i64 off, int w)
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

int main(void)
{
  const struct CMUnitTest tests[] = {
      cmocka_unit_test(align_down_already_aligned),
      cmocka_unit_test(align_down_truncates),
      cmocka_unit_test(align_down_zero),
      cmocka_unit_test(align_up_already_aligned),
      cmocka_unit_test(align_up_rounds_up),
      cmocka_unit_test(align_up_zero),
      cmocka_unit_test(build_flags_read_only),
      cmocka_unit_test(build_flags_read_write),
      cmocka_unit_test(build_flags_no_prot),
  };
  return cmocka_run_group_tests(tests, NULL, NULL);
}
