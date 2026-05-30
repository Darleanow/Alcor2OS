/* Unit tests for Virtual Memory Manager (vmm.c) */

#define TEST_ENV

#include "test_common.h"
#include <alcor2/mm/memory_layout.h>
#include <alcor2/types.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

#include <alcor2/mm/vmm.h>

/* Mock PMM */
static void *mock_pmm_pages[8192];
static int mock_pmm_count = 0;
static bool mock_pmm_fail = false;
static int mock_pmm_free_count = 0;

void *pmm_alloc(void) {
    if (mock_pmm_fail) return NULL;
    void *ptr = aligned_alloc(4096, 4096);
    if (!ptr) return NULL;
    memset(ptr, 0, 4096);
    mock_pmm_pages[mock_pmm_count++] = ptr;
    return ptr;
}

void pmm_free(void *addr) {
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

/* Include source for testing statics */
#include "../../src/mm/vmm.c"

static void test_vmm_clear_user_mappings(void **state) {
    (void)state;
    // Set up dummy kernel pml4 to avoid segfaults
    void *dummy_kernel = pmm_alloc();
    kernel_pml4 = (u64 *)dummy_kernel;

    u64 pml4 = vmm_create_address_space();
    void *user_page = pmm_alloc();
    vmm_map_in(pml4, 0x1000, (u64)user_page, VMM_PRESENT | VMM_USER); // A user mapping
    
    // Test clearing
    vmm_clear_user_mappings(pml4);
    
    // Assert user mappings are gone
    u64 *pml4_ptr = (u64 *)pml4;
    assert_int_equal(pml4_ptr[0], 0); // 0-255 should be clear
}

static void test_vmm_clone_address_space(void **state) {
    (void)state;
    void *dummy_kernel = pmm_alloc();
    kernel_pml4 = (u64 *)dummy_kernel;
    
    u64 src_pml4 = vmm_create_address_space();
    
    // Map a user page
    void *phys_page = pmm_alloc();
    strcpy((char*)phys_page, "hello");
    vmm_map_in(src_pml4, 0x2000, (u64)phys_page, VMM_PRESENT | VMM_USER);
    
    u64 dst_pml4 = vmm_clone_address_space(src_pml4);
    assert_true(dst_pml4 != 0);
    assert_true(dst_pml4 != src_pml4);
    
    // Verify cloned mapping
    fake_cr3 = dst_pml4;
    u64 cloned_phys = vmm_get_phys(0x2000);
    assert_true(cloned_phys != 0);
    assert_true(cloned_phys != (u64)phys_page); // Should be a new physical page
    assert_string_equal((char*)cloned_phys, "hello"); // Content copied
}

static void test_vmm_create_address_space(void **state) {
    (void)state;
    // Set up dummy kernel pml4
    void *dummy_kernel = pmm_alloc();
    kernel_pml4 = (u64 *)dummy_kernel;
    kernel_pml4[256] = 0x1234 | VMM_PRESENT; // Mock kernel mapping
    
    u64 new_pml4 = vmm_create_address_space();
    assert_true(new_pml4 != 0);
    
    u64 *pml4_ptr = (u64 *)new_pml4;
    assert_int_equal(pml4_ptr[0], 0); // User empty
    assert_int_equal(pml4_ptr[256], 0x1234 | VMM_PRESENT); // Kernel linked
}

static void test_vmm_destroy_user_mappings(void **state) {
    (void)state;
    void *dummy_kernel = pmm_alloc();
    kernel_pml4 = (u64 *)dummy_kernel;

    u64 pml4 = vmm_create_address_space();
    void *user_page = pmm_alloc();
    vmm_map_in(pml4, 0x1000, (u64)user_page, VMM_PRESENT | VMM_USER); // A user mapping
    
    int initial_free = mock_pmm_free_count;
    vmm_destroy_user_mappings(pml4);
    
    assert_true(mock_pmm_free_count > initial_free);
    // Destroy frees the PML4 itself as well
}

static void test_vmm_get_phys(void **state) {
    (void)state;
    void *dummy_pml4 = pmm_alloc();
    fake_cr3 = (u64)dummy_pml4;
    
    void *phys_page = pmm_alloc();
    vmm_map(0x4000, (u64)phys_page, VMM_PRESENT | VMM_WRITE);
    
    u64 phys = vmm_get_phys(0x4000);
    assert_int_equal(phys, (u64)phys_page);
    
    // Test unmapped
    u64 unmapped = vmm_get_phys(0x8000);
    assert_int_equal(unmapped, 0);
}

static void test_vmm_is_user_ptr(void **state) {
    (void)state;
    assert_true(vmm_is_user_ptr((void *)0x1000));
    assert_true(vmm_is_user_ptr((void *)0x1005)); // unaligned user pointer
    assert_false(vmm_is_user_ptr((void *)USER_SPACE_END));
    assert_false(vmm_is_user_ptr((void *)0xFFFFFF8000000000)); // kernel pointer
    assert_true(vmm_is_user_ptr(NULL)); // NULL is in lower half
}

static void test_vmm_is_user_range(void **state) {
    (void)state;
    assert_true(vmm_is_user_range((void *)0x1000, 0x1000));
    assert_false(vmm_is_user_range((void *)(USER_SPACE_END - 0x1000), 0x2000)); // Crosses boundary
    assert_false(vmm_is_user_range((void *)0xFFFFFF8000000000, 0x1000));
    
    // Overflow case
    assert_false(vmm_is_user_range((void *)0x1000, 0xFFFFFFFFFFFFFFFF));
}

static void test_vmm_map(void **state) {
    (void)state;
    void *dummy_pml4 = pmm_alloc();
    fake_cr3 = (u64)dummy_pml4;
    vmm_init(0); 
    fake_cr3 = (u64)kernel_pml4;
    
    void *phys_page = pmm_alloc();
    vmm_map(0x1000, (u64)phys_page, VMM_PRESENT | VMM_WRITE);
    
    u64 resolved_phys = vmm_get_phys(0x1000);
    assert_int_equal(resolved_phys, (u64)phys_page);
}

static void test_vmm_map_in_fail(void **state) {
    (void)state;
    u64 pml4_phys = (u64)pmm_alloc();
    
    mock_pmm_fail = true;
    bool success = vmm_map_in(pml4_phys, 0x2000, 0x1234000, VMM_PRESENT);
    assert_false(success);
    mock_pmm_fail = false;
}

static void test_vmm_map_range_alloc(void **state) {
    (void)state;
    void *dummy_pml4 = pmm_alloc();
    fake_cr3 = (u64)dummy_pml4;
    
    bool success = vmm_map_range_alloc(0x5000, 3, VMM_PRESENT | VMM_WRITE);
    assert_true(success);
    
    assert_true(vmm_get_phys(0x5000) != 0);
    assert_true(vmm_get_phys(0x6000) != 0);
    assert_true(vmm_get_phys(0x7000) != 0);
    assert_true(vmm_get_phys(0x8000) == 0); // Not mapped
    
    // Test allocation failure
    mock_pmm_fail = true;
    success = vmm_map_range_alloc(0x9000, 1, VMM_PRESENT);
    assert_false(success);
    mock_pmm_fail = false;
}

static void test_vmm_unmap(void **state) {
    (void)state;
    void *dummy_pml4 = pmm_alloc();
    fake_cr3 = (u64)dummy_pml4;
    
    void *phys_page = pmm_alloc();
    vmm_map(0x3000, (u64)phys_page, VMM_PRESENT | VMM_WRITE);
    
    assert_true(vmm_get_phys(0x3000) == (u64)phys_page);
    
    vmm_unmap(0x3000);
    assert_true(vmm_get_phys(0x3000) == 0);
}

int main(void) {
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_vmm_clear_user_mappings),
        cmocka_unit_test(test_vmm_clone_address_space),
        cmocka_unit_test(test_vmm_create_address_space),
        cmocka_unit_test(test_vmm_destroy_user_mappings),
        cmocka_unit_test(test_vmm_get_phys),
        cmocka_unit_test(test_vmm_is_user_ptr),
        cmocka_unit_test(test_vmm_is_user_range),
        cmocka_unit_test(test_vmm_map),
        cmocka_unit_test(test_vmm_map_in_fail),
        cmocka_unit_test(test_vmm_map_range_alloc),
        cmocka_unit_test(test_vmm_unmap),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
