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
static void *mock_pmm_pages[1024];
static int mock_pmm_count = 0;
static bool mock_pmm_fail = false;
static int mock_pmm_free_count = 0;

void *pmm_alloc(void) {
    if (mock_pmm_fail) return NULL;
    void *ptr = aligned_alloc(4096, 4096);
    memset(ptr, 0, 4096);
    mock_pmm_pages[mock_pmm_count++] = ptr;
    return ptr;
}

void pmm_free(void *addr) {
    mock_pmm_free_count++;
    free(addr);
}

u64 fake_cr3 = 0;

/* Include source for testing statics */
#include "../../src/mm/vmm.c"

static void test_vmm_map(void **state) {
    (void)state;
    // Set a dummy old_pml4 so vmm_init doesn't crash
    void *dummy_pml4 = pmm_alloc();
    fake_cr3 = (u64)dummy_pml4;
    vmm_init(0); // hhdm is 0 for tests
    fake_cr3 = (u64)kernel_pml4;
    
    // Test mapping a physical page
    void *phys_page = pmm_alloc();
    vmm_map(0x1000, (u64)phys_page, VMM_PRESENT | VMM_WRITE);
    
    // Check if it mapped correctly
    u64 resolved_phys = vmm_get_phys(0x1000);
    assert_int_equal(resolved_phys, (u64)phys_page);
}

static void test_vmm_map_in_fail(void **state) {
    (void)state;
    u64 pml4_phys = (u64)pmm_alloc();
    
    // Force pmm_alloc to fail so intermediate tables can't be created
    mock_pmm_fail = true;
    bool success = vmm_map_in(pml4_phys, 0x2000, 0x1234000, VMM_PRESENT);
    assert_false(success);
    mock_pmm_fail = false;
}

int main(void) {
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_vmm_map),
        cmocka_unit_test(test_vmm_map_in_fail),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
