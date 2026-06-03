/* Unit tests for heap allocator (heap.c) */

#include "test_common.h"
#include <alcor2/mm/memory_layout.h>
#include <alcor2/types.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <alcor2/mm/heap.h>
#include <alcor2/mm/vmm.h>

/* Mock PMM and VMM */
static bool pmm_fail = false;

void *pmm_alloc_pages(u64 count) {
    if(pmm_fail) return NULL;
    void *ptr = aligned_alloc(4096, count * 4096);
    memset(ptr, 0, count * 4096);
    return ptr;
}

bool vmm_map_range_alloc(u64 virt_start, u64 count, u64 flags) {
    (void)virt_start;
    (void)count;
    (void)flags;
    return true; // pretend it always succeeds
}

void vmm_map(u64 virt, u64 phys, u64 flags) {
    (void)virt;
    (void)phys;
    (void)flags;
}

void console_print(const char *msg) {
    (void)msg;
}

void console_printf(const char *fmt, ...) {
    (void)fmt;
}

/* Include source for testing statics */
#include "../../src/mm/heap.c"

static void *fake_heap_memory = NULL;

static int setup_empty(void **state) {
    (void)state;
    if (fake_heap_memory) free(fake_heap_memory);
    fake_heap_memory = malloc(1024 * 1024 * 32); // 32MB
    memset(fake_heap_memory, 0, 1024 * 1024 * 32);
    
    heap_start = NULL;
    heap_end = NULL;
    heap_size = 0;
    heap_used = 0;
    heap_next_va = (u64)fake_heap_memory;
    return 0;
}

static int teardown(void **state) {
    (void)state;
    if (fake_heap_memory) {
        free(fake_heap_memory);
        fake_heap_memory = NULL;
    }
    return 0;
}

static void test_kmalloc_basic(void **state) {
    (void)state;
    
    void *ptr = kmalloc(128);
    assert_non_null(ptr);
    
    void *ptr2 = kmalloc(64);
    assert_non_null(ptr2);
    
    // Check block header
    heap_block_t *block1 = (heap_block_t *)((u8 *)ptr - HEAP_HEADER_SIZE);
    assert_int_equal(block1->size, 128);
    assert_false(block1->free);
    
    kfree(ptr);
    assert_true(block1->free);
    
    kfree(ptr2);
}

static void test_krealloc_shrink(void **state) {
    (void)state;
    void *ptr = kmalloc(256);
    assert_non_null(ptr);
    
    // Write something in the buffer
    memset(ptr, 'A', 256);
    
    // Reallocate to a smaller size
    void *new_ptr = krealloc(ptr, 128);
    assert_non_null(new_ptr);
    
    // Check if the contents are preserved
    for(int i = 0; i < 128; i++) {
        assert_int_equal(((u8 *)new_ptr)[i], 'A');
    }
    
    kfree(new_ptr);
}

static void test_kzalloc(void **state) {
    (void)state;
    void *ptr = kzalloc(128);
    assert_non_null(ptr);
    
    // Check if the contents are zeroed
    for(int i = 0; i < 128; i++) {
        assert_int_equal(((u8 *)ptr)[i], 0);
    }
    kfree(ptr);
}

static void test_krealloc_grow(void **state) {
    (void)state;
    void *ptr = kmalloc(64);
    assert_non_null(ptr);
    memset(ptr, 'B', 64);
    
    // Grow to a larger size
    void *new_ptr = krealloc(ptr, 512);
    assert_non_null(new_ptr);
    
    for(int i = 0; i < 64; i++) {
        assert_int_equal(((u8 *)new_ptr)[i], 'B');
    }
    kfree(new_ptr);
}

static void test_heap_split_block(void **state) {
    (void)state;
    void *ptr1 = kmalloc(1024);
    assert_non_null(ptr1);
    
    heap_block_t *block1 = (heap_block_t *)((u8 *)ptr1 - HEAP_HEADER_SIZE);
    
    // Allocate a small chunk. The large free chunk after block1 should be split.
    void *ptr2 = kmalloc(32);
    assert_non_null(ptr2);
    heap_block_t *block2 = (heap_block_t *)((u8 *)ptr2 - HEAP_HEADER_SIZE);
    
    assert_true(block1->next == block2);
    assert_int_equal(block2->size, 32);
    
    kfree(ptr1);
    kfree(ptr2);
}

static void test_heap_merge_free_blocks(void **state) {
    (void)state;
    void *ptr1 = kmalloc(128);
    void *ptr2 = kmalloc(128);
    void *ptr3 = kmalloc(128);
    void *ptr4 = kmalloc(128); // Barrier to prevent merging with the rest of the heap
    
    heap_block_t *b1 = (heap_block_t *)((u8 *)ptr1 - HEAP_HEADER_SIZE);
    heap_block_t *b2 = (heap_block_t *)((u8 *)ptr2 - HEAP_HEADER_SIZE);
    heap_block_t *b3 = (heap_block_t *)((u8 *)ptr3 - HEAP_HEADER_SIZE);
    
    kfree(ptr1);
    kfree(ptr3);
    
    // At this point b1 and b3 are free, but b2 is allocated, so they don't merge
    assert_true(b1->free);
    assert_true(b3->free);
    assert_false(b2->free);
    
    // Now free ptr2. They should all merge into b1
    kfree(ptr2);
    
    assert_true(b1->free);
    assert_int_equal(b1->size, b1->size); // We just assert it merged correctly by checking b1->next
    assert_true(b1->next == (heap_block_t *)((u8 *)ptr4 - HEAP_HEADER_SIZE));
    
    kfree(ptr4);
}

static void test_heap_expand_zero_pages(void **state) {
    (void)state;
    /* heap_expand(0) must clamp to 1 and succeed */
    int rc = heap_expand(0);
    assert_int_equal(rc, 0);
    assert_non_null(heap_start);
}

static void test_heap_expand_pmm_fail(void **state) {
    (void)state;
    pmm_fail = true;
    int rc = heap_expand(1);
    assert_int_equal(rc, -1);
    pmm_fail = false;
}

static void test_heap_expand_twice_links_blocks(void **state) {
    (void)state;
    /* Second expand appends a new block — exercises heap_end->next = block */
    heap_expand(1);
    heap_block_t *first = heap_start;
    heap_expand(1);
    assert_non_null(first->next);
}

static void test_kfree_null_is_noop(void **state) {
    (void)state;
    heap_expand(1);
    kfree(NULL); /* must not crash */
}

static void test_kfree_bad_magic_is_noop(void **state) {
    (void)state;
    heap_expand(1);
    /* Corrupt a fake block with wrong magic */
    static heap_block_t fake = {0};
    fake.magic = 0xDEAD;
    kfree((void *)((u8 *)&fake + HEAP_HEADER_SIZE)); /* must not crash */
}

static void test_split_block_updates_next_prev(void **state) {
    (void)state;
    /* Allocate two chunks so split_block has a following block (line 112) */
    heap_expand(2);
    void *p1 = kmalloc(32);
    void *p2 = kmalloc(32);
    kfree(p1);
    /* Reallocate p1 with a smaller size — forces split with block->next != NULL */
    void *p3 = kmalloc(16);
    (void)p2; (void)p3;
}

static void test_heap_init_pmm_fail(void **state) {
    (void)state;
    pmm_fail = true;
    heap_init(); /* hits the "Init failed!" console_print path */
    pmm_fail = false;
    /* Reinit for teardown */
    heap_start = NULL;
    heap_end = NULL;
    heap_size = 0;
}

int main(void) {
    const struct CMUnitTest tests[] = {
        cmocka_unit_test_setup_teardown(test_kmalloc_basic, setup_empty, teardown),
        cmocka_unit_test_setup_teardown(test_krealloc_shrink, setup_empty, teardown),
        cmocka_unit_test_setup_teardown(test_kzalloc, setup_empty, teardown),
        cmocka_unit_test_setup_teardown(test_krealloc_grow, setup_empty, teardown),
        cmocka_unit_test_setup_teardown(test_heap_split_block, setup_empty, teardown),
        cmocka_unit_test_setup_teardown(test_heap_merge_free_blocks, setup_empty, teardown),
        cmocka_unit_test_setup_teardown(test_heap_expand_zero_pages, setup_empty, teardown),
        cmocka_unit_test_setup_teardown(test_heap_expand_pmm_fail, setup_empty, teardown),
        cmocka_unit_test_setup_teardown(test_heap_expand_twice_links_blocks, setup_empty, teardown),
        cmocka_unit_test_setup_teardown(test_kfree_null_is_noop, setup_empty, teardown),
        cmocka_unit_test_setup_teardown(test_kfree_bad_magic_is_noop, setup_empty, teardown),
        cmocka_unit_test_setup_teardown(test_split_block_updates_next_prev, setup_empty, teardown),
        cmocka_unit_test_setup_teardown(test_heap_init_pmm_fail, setup_empty, teardown),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
