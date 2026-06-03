/* Unit tests for Virtual Memory Manager (vmm.c) */

#include "test_common.h"

#include <alcor2/mm/memory_layout.h>
#include <alcor2/mm/vmm.h>
#include <alcor2/types.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>


static void *mock_pmm_pages[8192];
static int   mock_pmm_count     = 0;
static bool  mock_pmm_fail      = false;
static int   mock_pmm_fail_on   = -1; /* -1=never; N=fail on Nth call */
static int   mock_pmm_call_count = 0;
static int   mock_pmm_free_count = 0;

void *pmm_alloc(void)
{
  mock_pmm_call_count++;
  if(mock_pmm_fail)
    return NULL;
  if(mock_pmm_fail_on > 0 && mock_pmm_call_count == mock_pmm_fail_on)
    return NULL;
  void *ptr = aligned_alloc(4096, 4096);
  if(!ptr)
    return NULL;
  memset(ptr, 0, 4096);
  mock_pmm_pages[mock_pmm_count++] = ptr;
  return ptr;
}

void pmm_free(void *addr)
{
  mock_pmm_free_count++;
  for(int i = 0; i < mock_pmm_count; i++) {
    if(mock_pmm_pages[i] == addr) {
      mock_pmm_pages[i] = NULL;
      break;
    }
  }
  free(addr);
}

u64 fake_cr3 = 0;
u64 cpu_read_cr3(void) { return fake_cr3; }
void cpu_write_cr3(u64 pml4_phys) { fake_cr3 = pml4_phys; }
void cpu_invlpg(u64 virt) { (void)virt; }

/* Include source for testing statics */
#include "../../src/mm/vmm.c"


static int setup(void **state)
{
  (void)state;
  mock_pmm_count       = 0;
  mock_pmm_fail        = false;
  mock_pmm_fail_on     = -1;
  mock_pmm_call_count  = 0;
  mock_pmm_free_count  = 0;
  fake_cr3            = 0;
  kernel_pml4         = NULL;
  hhdm                = 0;
  return 0;
}


static void test_vmm_clear_user_mappings(void **state)
{
  (void)state;
  void *dummy_kernel = pmm_alloc();
  kernel_pml4        = (u64 *)dummy_kernel;

  u64   pml4      = vmm_create_address_space();
  void *user_page = pmm_alloc();
  vmm_map_in(pml4, 0x1000, (u64)user_page, VMM_PRESENT | VMM_USER);

  vmm_clear_user_mappings(pml4);

  u64 *pml4_ptr = (u64 *)pml4;
  assert_int_equal(pml4_ptr[0], 0);
}

static void test_vmm_clone_address_space(void **state)
{
  (void)state;
  void *dummy_kernel = pmm_alloc();
  kernel_pml4        = (u64 *)dummy_kernel;

  u64   src_pml4 = vmm_create_address_space();
  void *phys_page = pmm_alloc();
  strcpy((char *)phys_page, "hello");
  vmm_map_in(src_pml4, 0x2000, (u64)phys_page, VMM_PRESENT | VMM_USER);

  u64 dst_pml4 = vmm_clone_address_space(src_pml4);
  assert_true(dst_pml4 != 0);
  assert_true(dst_pml4 != src_pml4);

  fake_cr3          = dst_pml4;
  u64 cloned_phys   = vmm_get_phys(0x2000);
  assert_true(cloned_phys != 0);
  assert_true(cloned_phys != (u64)phys_page); /* new physical page */
  assert_string_equal((char *)cloned_phys, "hello"); /* content copied */
}

static void test_vmm_create_address_space(void **state)
{
  (void)state;
  void *dummy_kernel = pmm_alloc();
  kernel_pml4        = (u64 *)dummy_kernel;
  kernel_pml4[256]   = 0x1234 | VMM_PRESENT;

  u64  new_pml4 = vmm_create_address_space();
  assert_true(new_pml4 != 0);

  u64 *pml4_ptr = (u64 *)new_pml4;
  assert_int_equal(pml4_ptr[0], 0);                       /* user empty */
  assert_int_equal(pml4_ptr[256], 0x1234 | VMM_PRESENT);  /* kernel linked */
}

static void test_vmm_destroy_user_mappings(void **state)
{
  (void)state;
  void *dummy_kernel = pmm_alloc();
  kernel_pml4        = (u64 *)dummy_kernel;

  u64   pml4      = vmm_create_address_space();
  void *user_page = pmm_alloc();
  vmm_map_in(pml4, 0x1000, (u64)user_page, VMM_PRESENT | VMM_USER);

  int initial_free = mock_pmm_free_count;
  vmm_destroy_user_mappings(pml4);
  assert_true(mock_pmm_free_count > initial_free);
}

static void test_vmm_get_phys(void **state)
{
  (void)state;
  void *dummy_pml4 = pmm_alloc();
  fake_cr3         = (u64)dummy_pml4;

  void *phys_page = pmm_alloc();
  vmm_map(0x4000, (u64)phys_page, VMM_PRESENT | VMM_WRITE);

  /* Exact base */
  u64 phys = vmm_get_phys(0x4000);
  assert_int_equal(phys, (u64)phys_page);

  /* Unmapped address returns 0 */
  u64 unmapped = vmm_get_phys(0x8000);
  assert_int_equal(unmapped, 0);

  /* Offset within the page is preserved */
  u64 phys_off = vmm_get_phys(0x4042);
  assert_int_equal(phys_off, (u64)phys_page + 0x042);
}

static void test_vmm_is_user_ptr(void **state)
{
  (void)state;
  assert_true(vmm_is_user_ptr((void *)0x1000));
  assert_true(vmm_is_user_ptr((void *)0x1005));              /* unaligned */
  assert_false(vmm_is_user_ptr((void *)USER_SPACE_END));
  assert_false(vmm_is_user_ptr((void *)0xFFFFFF8000000000)); /* kernel */
  assert_true(vmm_is_user_ptr(NULL));                        /* NULL < USER_SPACE_END */
}

static void test_vmm_is_user_range(void **state)
{
  (void)state;
  assert_true(vmm_is_user_range((void *)0x1000, 0x1000));
  assert_false(vmm_is_user_range(
      (void *)(USER_SPACE_END - 0x1000), 0x2000
  ));                                                     /* crosses boundary */
  assert_false(vmm_is_user_range((void *)0xFFFFFF8000000000, 0x1000));
  assert_false(vmm_is_user_range((void *)0x1000, 0xFFFFFFFFFFFFFFFF)); /* overflow */
  assert_true(vmm_is_user_range((void *)0x0, 0));          /* zero-length always in */
}

static void test_vmm_map(void **state)
{
  (void)state;
  void *dummy_pml4 = pmm_alloc();
  fake_cr3         = (u64)dummy_pml4;
  vmm_init(0);
  fake_cr3 = (u64)kernel_pml4;

  void *phys_page   = pmm_alloc();
  vmm_map(0x1000, (u64)phys_page, VMM_PRESENT | VMM_WRITE);

  u64 resolved_phys = vmm_get_phys(0x1000);
  assert_int_equal(resolved_phys, (u64)phys_page);
}

static void test_vmm_map_in_fail(void **state)
{
  (void)state;
  u64 pml4_phys = (u64)pmm_alloc();

  mock_pmm_fail = true;
  bool success  = vmm_map_in(pml4_phys, 0x2000, 0x1234000, VMM_PRESENT);
  assert_false(success);
  mock_pmm_fail = false;
}

static void test_vmm_map_range_alloc(void **state)
{
  (void)state;
  void *dummy_pml4 = pmm_alloc();
  fake_cr3         = (u64)dummy_pml4;

  /* count=0 is a valid no-op */
  assert_true(vmm_map_range_alloc(0x5000, 0, VMM_PRESENT | VMM_WRITE));

  /* Map 3 pages */
  bool success = vmm_map_range_alloc(0x5000, 3, VMM_PRESENT | VMM_WRITE);
  assert_true(success);
  assert_true(vmm_get_phys(0x5000) != 0);
  assert_true(vmm_get_phys(0x6000) != 0);
  assert_true(vmm_get_phys(0x7000) != 0);
  assert_true(vmm_get_phys(0x8000) == 0); /* not mapped */

  /* Allocation failure stops early */
  mock_pmm_fail = true;
  success       = vmm_map_range_alloc(0x9000, 1, VMM_PRESENT);
  assert_false(success);
  mock_pmm_fail = false;
}

static void test_vmm_unmap(void **state)
{
  (void)state;
  void *dummy_pml4 = pmm_alloc();
  fake_cr3         = (u64)dummy_pml4;

  void *phys_page = pmm_alloc();
  vmm_map(0x3000, (u64)phys_page, VMM_PRESENT | VMM_WRITE);
  assert_true(vmm_get_phys(0x3000) == (u64)phys_page);

  vmm_unmap(0x3000);
  assert_true(vmm_get_phys(0x3000) == 0);

  /* Double-unmap must not crash */
  vmm_unmap(0x3000);
}

static void test_vmm_map_pd_fail(void **state)
{
  (void)state;
  void *pml4_raw = pmm_alloc();
  fake_cr3       = (u64)pml4_raw;
  vmm_init(0);
  fake_cr3       = (u64)kernel_pml4;

  /* Fail on 2nd pmm_alloc (pd allocation) → vmm_map hits line 108 */
  mock_pmm_fail_on = 2;
  mock_pmm_call_count = 0;
  vmm_map(0x8000, 0x1234000, VMM_PRESENT);
  mock_pmm_fail_on = -1;

  assert_int_equal(vmm_get_phys(0x8000), 0);
}

static void test_vmm_map_pt_fail(void **state)
{
  (void)state;
  void *pml4_raw = pmm_alloc();
  fake_cr3       = (u64)pml4_raw;
  vmm_init(0);
  fake_cr3       = (u64)kernel_pml4;

  /* Fail on 3rd pmm_alloc (pt allocation) → vmm_map hits line 112 */
  mock_pmm_fail_on = 3;
  mock_pmm_call_count = 0;
  vmm_map(0x9000, 0x1234000, VMM_PRESENT);
  mock_pmm_fail_on = -1;

  assert_int_equal(vmm_get_phys(0x9000), 0);
}

static void test_vmm_unmap_partial_walk_pdpt_missing(void **state)
{
  (void)state;
  void *pml4_raw = pmm_alloc();
  fake_cr3       = (u64)pml4_raw;
  vmm_init(0);
  fake_cr3       = (u64)kernel_pml4;

  /* Unmap an address that was never mapped — walk hits missing pdpt → return (line 209) */
  vmm_unmap(0xABCD000);
}

static void test_vmm_get_phys_pd_missing(void **state)
{
  (void)state;
  void *pml4_raw = pmm_alloc();
  fake_cr3       = (u64)pml4_raw;
  vmm_init(0);

  /* Map only one page so pdpt exists but pd is missing for another address */
  void *phys = pmm_alloc();
  vmm_map(0x1000, (u64)phys, VMM_PRESENT);
  fake_cr3 = (u64)kernel_pml4;

  /* A different pd_idx → get_next_level(pdpt, pdpt_idx, false) = NULL → return 0 (line 250) */
  u64 r = vmm_get_phys(0x40000000); /* different pdpt entry */
  assert_int_equal(r, 0);
}

static void test_vmm_create_address_space_oom(void **state)
{
  (void)state;
  mock_pmm_fail = true;
  u64 r = vmm_create_address_space();
  assert_int_equal(r, 0);
  mock_pmm_fail = false;
}

static void test_vmm_map_in_pd_fail(void **state)
{
  (void)state;
  u64 pml4_phys = (u64)pmm_alloc();

  /* pdpt succeeds (call 1), pd fails (call 2) → vmm_map_in returns false (line 162) */
  mock_pmm_fail_on = 2;
  mock_pmm_call_count = 0;
  bool ok = vmm_map_in(pml4_phys, 0xB000, 0x5678000, VMM_PRESENT);
  assert_false(ok);
  mock_pmm_fail_on = -1;
}

static void test_vmm_map_in_pt_fail(void **state)
{
  (void)state;
  u64 pml4_phys = (u64)pmm_alloc();

  /* pdpt ok (1), pd ok (2), pt fails (3) → returns false (line 169) */
  mock_pmm_fail_on = 3;
  mock_pmm_call_count = 0;
  bool ok = vmm_map_in(pml4_phys, 0xC000, 0x9ABC000, VMM_PRESENT);
  assert_false(ok);
  mock_pmm_fail_on = -1;
}

static void test_vmm_map_range_already_present_skip(void **state)
{
  (void)state;
  void *pml4_raw = pmm_alloc();
  fake_cr3       = (u64)pml4_raw;
  vmm_init(0);
  fake_cr3       = (u64)kernel_pml4;

  /* Map a page, then map_range over the same address — it should skip (line 175) */
  void *phys = pmm_alloc();
  vmm_map(0xD000, (u64)phys, VMM_PRESENT);
  bool ok = vmm_map_range(0xD000, 1, VMM_PRESENT | VMM_WRITE);
  assert_true(ok);
}

static void test_vmm_get_next_level_no_create_missing(void **state)
{
  (void)state;
  void *pml4_raw = pmm_alloc();
  fake_cr3       = (u64)pml4_raw;
  vmm_init(0);
  fake_cr3       = (u64)kernel_pml4;

  /* Look up an address that has never been mapped with create=false →
   * get_next_level returns 0 (line 40) and vmm_get_phys returns 0 */
  u64 phys = vmm_get_phys(0xDEAD000);
  assert_int_equal(phys, 0);
}

static void test_vmm_map_promotes_to_user(void **state)
{
  (void)state;
  void *pml4_raw = pmm_alloc();
  fake_cr3       = (u64)pml4_raw;
  vmm_init(0);
  fake_cr3       = (u64)kernel_pml4;

  void *phys = pmm_alloc();

  /* First map without VMM_USER */
  vmm_map(0x5000, (u64)phys, VMM_PRESENT | VMM_WRITE);

  /* Second map on same address with VMM_USER — get_next_level hits
   * the existing entry and ORs in VMM_USER (line 34) */
  vmm_map(0x5000, (u64)phys, VMM_PRESENT | VMM_WRITE | VMM_USER);

  u64 resolved = vmm_get_phys(0x5000);
  assert_int_equal(resolved, (u64)phys);
}

static void test_vmm_map_pmm_fail_mid_walk(void **state)
{
  (void)state;
  void *pml4_raw = pmm_alloc();
  fake_cr3       = (u64)pml4_raw;
  vmm_init(0);
  fake_cr3       = (u64)kernel_pml4;

  /* Fail pmm_alloc on next call — vmm_map's get_next_level will return NULL
   * and vmm_map early-returns (lines 104/108/112) */
  mock_pmm_fail = true;
  vmm_map(0x7000, 0x1234000, VMM_PRESENT);
  mock_pmm_fail = false;

  /* Address should not be mapped */
  assert_int_equal(vmm_get_phys(0x7000), 0);
}

int main(void)
{
  const struct CMUnitTest tests[] = {
      cmocka_unit_test_setup(test_vmm_clear_user_mappings, setup),
      cmocka_unit_test_setup(test_vmm_clone_address_space, setup),
      cmocka_unit_test_setup(test_vmm_create_address_space, setup),
      cmocka_unit_test_setup(test_vmm_destroy_user_mappings, setup),
      cmocka_unit_test_setup(test_vmm_get_phys, setup),
      cmocka_unit_test_setup(test_vmm_is_user_ptr, setup),
      cmocka_unit_test_setup(test_vmm_is_user_range, setup),
      cmocka_unit_test_setup(test_vmm_map, setup),
      cmocka_unit_test_setup(test_vmm_map_in_fail, setup),
      cmocka_unit_test_setup(test_vmm_map_range_alloc, setup),
      cmocka_unit_test_setup(test_vmm_unmap, setup),
      cmocka_unit_test_setup(test_vmm_get_next_level_no_create_missing, setup),
      cmocka_unit_test_setup(test_vmm_map_promotes_to_user, setup),
      cmocka_unit_test_setup(test_vmm_map_pmm_fail_mid_walk, setup),
      cmocka_unit_test_setup(test_vmm_map_pd_fail, setup),
      cmocka_unit_test_setup(test_vmm_map_pt_fail, setup),
      cmocka_unit_test_setup(test_vmm_unmap_partial_walk_pdpt_missing, setup),
      cmocka_unit_test_setup(test_vmm_get_phys_pd_missing, setup),
      cmocka_unit_test_setup(test_vmm_create_address_space_oom, setup),
      cmocka_unit_test_setup(test_vmm_map_in_pd_fail, setup),
      cmocka_unit_test_setup(test_vmm_map_in_pt_fail, setup),
      cmocka_unit_test_setup(test_vmm_map_range_already_present_skip, setup),
  };
  return cmocka_run_group_tests(tests, NULL, NULL);
}
