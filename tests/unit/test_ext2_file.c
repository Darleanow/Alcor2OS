#include "test_common.h"

#include <alcor2/errno.h>
#include <alcor2/fs/ext2.h>
#include <alcor2/fs/vfs.h>
#include <alcor2/kstdlib.h>
#include <alcor2/mm/heap.h>
#include <alcor2/types.h>
#include <fs/ext2/internal.h>

#include <stdlib.h>
#include <string.h>

void *kmalloc(u64 n) { return malloc(n); }
void kfree(void *p) { if(p) free(p); }

u8 *cache_get_block(u32 s) { return malloc(s); }
void cache_put_block(u8 *p) { if(p) free(p); }

/* Mocks */
i64 resolve_path(const ext2_volume_t *vol, const char *path, u32 *out_ino, ext2_inode_t *out_inode) {
    (void)vol; (void)path;
    int ret = (int)mock();
    if (ret == 0) {
        if (out_ino) *out_ino = (u32)mock();
        if (out_inode) {
            ext2_inode_t *mock_inode = (ext2_inode_t*)mock();
            if (mock_inode) *out_inode = *mock_inode;
        }
    }
    return ret;
}

i64 free_inode_blocks(ext2_volume_t *vol, ext2_inode_t *inode) {
  (void)vol; (void)inode;
  return (i64)mock();
}

i64 free_inode(ext2_volume_t *vol, u32 inode_num, bool is_dir) {
  (void)vol; (void)inode_num; (void)is_dir;
  return (i64)mock();
}

i64 flush_metadata(ext2_volume_t *vol) {
    (void)vol; return mock();
}

i64 write_inode(const ext2_volume_t *vol, u32 inode_num, const ext2_inode_t *inode) {
    (void)vol; (void)inode_num; (void)inode; return mock();
}

u32 get_block_num(const ext2_volume_t *vol, const ext2_inode_t *inode, u32 file_block) {
    (void)vol; (void)inode; (void)file_block; return (u32)mock();
}

u32 alloc_file_block(ext2_volume_t *vol, ext2_inode_t *inode, u32 file_block, u32 preferred_grp) {
    (void)vol; (void)inode; (void)file_block; (void)preferred_grp; return (u32)mock();
}

i64 vol_read_block(const ext2_volume_t *vol, u32 block_num, void *buf) {
    (void)vol; (void)block_num; (void)buf; return mock();
}

i64 vol_write_block(const ext2_volume_t *vol, u32 block_num, const void *buf) {
    (void)vol; (void)block_num; (void)buf; return mock();
}

u32 alloc_inode(ext2_volume_t *vol, u32 preferred_grp, bool is_dir) {
    (void)vol; (void)preferred_grp; (void)is_dir; return (u32)mock();
}

i64 dir_add_entry(ext2_volume_t *vol, u32 dir_ino, ext2_inode_t *dir_inode, const char *name, u32 target_ino, u8 file_type) {
    (void)vol; (void)dir_ino; (void)dir_inode; (void)name; (void)target_ino; (void)file_type; return mock();
}

void path_split(const char *path, char *out_parent, char *out_name) {
    (void)path;
    strcpy(out_parent, (const char*)mock());
    strcpy(out_name, (const char*)mock());
}

#include "../../src/fs/ext2/file.c"

static int setup_test(void **state) {
    (void)state;
    for(int i = 0; i < EXT2_MAX_FILES; i++) {
        g_files[i].in_use = false;
        g_files[i].dirty = false;
        g_files[i].vol = NULL;
    }
    return 0;
}

static void test_close_dirty(void **state) {
    (void)state;
    ext2_volume_t vol = {0};
    g_files[0].in_use = true;
    g_files[0].vol = &vol;
    g_files[0].inode_num = 10;
    g_files[0].inode.i_links_count = 1;
    g_files[0].dirty = true;

    will_return(write_inode, 0);
    will_return(flush_metadata, 0);

    ext2_close(&g_files[0]);
    assert_false(g_files[0].in_use);
}

static void test_close_unlinked_shared(void **state) {
    (void)state;
    ext2_volume_t vol = {0};
    g_files[0].in_use = true;
    g_files[0].vol = &vol;
    g_files[0].inode_num = 12;
    g_files[0].inode.i_links_count = 0;
    g_files[0].dirty = false;

    g_files[1].in_use = true;
    g_files[1].vol = &vol;
    g_files[1].inode_num = 12;

    ext2_close(&g_files[0]);
    assert_false(g_files[0].in_use);
    assert_true(g_files[1].in_use);
}

static void test_close_unlinked_single(void **state) {
    (void)state;
    ext2_volume_t vol = {0};
    g_files[0].in_use = true;
    g_files[0].vol = &vol;
    g_files[0].inode_num = 12;
    g_files[0].inode.i_links_count = 0;

    will_return(free_inode_blocks, 0);
    will_return(free_inode, 0);
    will_return(flush_metadata, 0);

    ext2_close(&g_files[0]);
    assert_false(g_files[0].in_use);
}

static void test_create_already_exists(void **state) {
    (void)state;
    ext2_volume_t vol = {.mounted = true};
    ext2_inode_t exist_inode = {0};
    exist_inode.i_mode = EXT2_S_IFREG;

    will_return(resolve_path, 0);
    will_return(resolve_path, 42);
    will_return(resolve_path, &exist_inode);

    will_return(resolve_path, 0);
    will_return(resolve_path, 42);
    will_return(resolve_path, &exist_inode);

    ext2_file_t *f = ext2_create(&vol, "/file");
    assert_non_null(f);
    assert_int_equal(f->inode_num, 42);
}

static void test_create_invalid_parent(void **state) {
    (void)state;
    ext2_volume_t vol = {.mounted = true};

    will_return(resolve_path, -1);
    will_return(path_split, "/dir");
    will_return(path_split, "file");

    ext2_inode_t parent_inode = {0};
    parent_inode.i_mode = EXT2_S_IFREG;

    will_return(resolve_path, 0);
    will_return(resolve_path, 10);
    will_return(resolve_path, &parent_inode);

    assert_null(ext2_create(&vol, "/dir/file"));
}

static void test_create_success(void **state) {
    (void)state;
    ext2_volume_t vol = {.mounted = true, .inodes_per_group = 100};

    will_return(resolve_path, -1);
    will_return(path_split, "/dir");
    will_return(path_split, "new");

    ext2_inode_t parent_inode = {0};
    parent_inode.i_mode = EXT2_S_IFDIR;

    will_return(resolve_path, 0);
    will_return(resolve_path, 10);
    will_return(resolve_path, &parent_inode);

    will_return(alloc_inode, 50);
    will_return(write_inode, 0);
    will_return(dir_add_entry, 0);
    will_return(flush_metadata, 0);

    ext2_file_t *f = ext2_create(&vol, "/dir/new");
    assert_non_null(f);
    assert_int_equal(f->inode_num, 50);
}

static void test_flush_clean(void **state) {
    (void)state;
    ext2_volume_t vol = {0};
    g_files[0].in_use = true;
    g_files[0].vol = &vol;
    g_files[0].dirty = false;
    assert_int_equal(ext2_flush(&g_files[0]), 0);
}

static void test_flush_dirty(void **state) {
    (void)state;
    ext2_volume_t vol = {0};
    g_files[0].in_use = true;
    g_files[0].vol = &vol;
    g_files[0].dirty = true;
    
    will_return(write_inode, 0);
    will_return(flush_metadata, 0);
    
    assert_int_equal(ext2_flush(&g_files[0]), 0);
    assert_false(g_files[0].dirty);
}

static void test_open_missing(void **state) {
    (void)state;
    ext2_volume_t vol = {.mounted = true};
    will_return(resolve_path, -1);
    assert_null(ext2_open(&vol, "/missing"));
}

static void test_open_success(void **state) {
    (void)state;
    ext2_volume_t vol = {.mounted = true};
    ext2_inode_t inode = {0};
    inode.i_mode = EXT2_S_IFREG;
    
    will_return(resolve_path, 0);
    will_return(resolve_path, 42);
    will_return(resolve_path, &inode);
    
    ext2_file_t *f = ext2_open(&vol, "/file");
    assert_non_null(f);
    assert_int_equal(f->inode_num, 42);
}

static void test_open_too_many(void **state) {
    (void)state;
    ext2_volume_t vol = {.mounted = true};
    for (int i = 0; i < EXT2_MAX_FILES; i++) {
        g_files[i].in_use = true;
    }
    assert_null(ext2_open(&vol, "/file"));
}

static void test_read_clamped_size(void **state) {
    (void)state;
    ext2_volume_t vol = {.mounted = true, .block_size = 1024};
    g_files[0].in_use = true;
    g_files[0].is_dir = false;
    g_files[0].vol = &vol;
    g_files[0].inode.i_size = 5;
    
    will_return(get_block_num, 0);
    
    char buf[10];
    assert_int_equal(ext2_read(&g_files[0], buf, 10, 0), 5);
}

static void test_read_past_eof(void **state) {
    (void)state;
    ext2_volume_t vol = {.mounted = true};
    g_files[0].in_use = true;
    g_files[0].is_dir = false;
    g_files[0].vol = &vol;
    g_files[0].inode.i_size = 100;
    
    char buf[10];
    assert_int_equal(ext2_read(&g_files[0], buf, 10, 100), 0);
    assert_int_equal(ext2_read(&g_files[0], buf, 10, 150), 0);
}

static void test_read_sparse(void **state) {
    (void)state;
    ext2_volume_t vol = {.mounted = true, .block_size = 1024};
    g_files[0].in_use = true;
    g_files[0].is_dir = false;
    g_files[0].vol = &vol;
    g_files[0].inode.i_size = 1024;
    
    will_return(get_block_num, 0);
    
    char buf[10];
    memset(buf, 1, sizeof(buf));
    assert_int_equal(ext2_read(&g_files[0], buf, 10, 0), 10);
    for(int i=0; i<10; i++) assert_int_equal(buf[i], 0);
}

static void test_truncate_shrink(void **state) {
    (void)state;
    ext2_volume_t vol = {0};
    g_files[0].in_use = true;
    g_files[0].is_dir = false;
    g_files[0].vol = &vol;
    g_files[0].inode.i_size = 100;
    
    will_return(free_inode_blocks, 0);
    will_return(write_inode, 0);
    will_return(flush_metadata, 0);
    
    assert_int_equal(ext2_truncate(&g_files[0], 0), 0);
    assert_int_equal(g_files[0].inode.i_size, 0);
}

static void test_write_allocates_block(void **state) {
    (void)state;
    ext2_volume_t vol = {.block_size = 1024, .inodes_per_group = 100};
    g_files[0].in_use = true;
    g_files[0].is_dir = false;
    g_files[0].vol = &vol;
    g_files[0].inode_num = 1;
    g_files[0].inode.i_size = 0;
    
    will_return(get_block_num, 0); 
    will_return(alloc_file_block, 42);
    will_return(vol_read_block, 0); 
    will_return(vol_write_block, 0);
    will_return(write_inode, 0); 
    
    char buf[10] = "hello";
    assert_int_equal(ext2_write(&g_files[0], buf, 5, 0), 5);
}

/* ext2_read: null/is_dir/not-in-use guards */
static void test_read_null_file(void **state) {
    (void)state;
    char buf[4];
    assert_int_equal((i64)ext2_read(NULL, buf, 4, 0), -EINVAL);
}

static void test_read_dir_file(void **state) {
    (void)state;
    g_files[0].in_use = true;
    g_files[0].is_dir = true;
    char buf[4];
    assert_int_equal((i64)ext2_read(&g_files[0], buf, 4, 0), -EINVAL);
}

static void test_read_not_in_use(void **state) {
    (void)state;
    g_files[0].in_use = false;
    char buf[4];
    assert_int_equal((i64)ext2_read(&g_files[0], buf, 4, 0), -EINVAL);
}

/* ext2_read: block is present (vol_read_block path) */
static void test_read_from_block(void **state) {
    (void)state;
    ext2_volume_t vol = {.mounted = true, .block_size = 1024};
    g_files[0].in_use = true;
    g_files[0].is_dir = false;
    g_files[0].vol    = &vol;
    g_files[0].inode.i_size = 5;

    will_return(get_block_num, 7); /* block 7 exists */
    will_return(vol_read_block, 0);

    char buf[5];
    assert_int_equal(ext2_read(&g_files[0], buf, 5, 0), 5);
}

/* ext2_write: null/is_dir guards */
static void test_write_null_file(void **state) {
    (void)state;
    assert_int_equal((i64)ext2_write(NULL, "x", 1, 0), -EINVAL);
}

static void test_write_dir_file(void **state) {
    (void)state;
    g_files[0].in_use = true;
    g_files[0].is_dir = true;
    assert_int_equal((i64)ext2_write(&g_files[0], "x", 1, 0), -EINVAL);
}

static void test_write_zero_count(void **state) {
    (void)state;
    ext2_volume_t vol = {.block_size = 1024, .inodes_per_group = 100};
    g_files[0].in_use = true;
    g_files[0].is_dir = false;
    g_files[0].vol    = &vol;
    assert_int_equal(ext2_write(&g_files[0], "x", 0, 0), 0);
}

/* ext2_write: overwrite existing block */
static void test_write_existing_block(void **state) {
    (void)state;
    ext2_volume_t vol = {.block_size = 1024, .inodes_per_group = 100};
    g_files[0].in_use    = true;
    g_files[0].is_dir    = false;
    g_files[0].vol       = &vol;
    g_files[0].inode_num = 1;
    g_files[0].inode.i_size = 1024;

    will_return(get_block_num, 5); /* block exists */
    will_return(vol_read_block, 0);
    will_return(vol_write_block, 0);
    /* write_inode not called: dirty stays false for existing block */

    char buf[5] = "hello";
    assert_int_equal(ext2_write(&g_files[0], buf, 5, 0), 5);
}

/* ext2_truncate: null guard */
static void test_truncate_null(void **state) {
    (void)state;
    assert_int_equal((i64)ext2_truncate(NULL, 0), -EINVAL);
}

/* ext2_truncate: is_dir guard */
static void test_truncate_dir(void **state) {
    (void)state;
    g_files[0].in_use = true;
    g_files[0].is_dir = true;
    assert_int_equal((i64)ext2_truncate(&g_files[0], 0), -EINVAL);
}

/* ext2_truncate: non-zero length (currently -ENOSYS in this impl) */
static void test_truncate_nonzero(void **state) {
    (void)state;
    ext2_volume_t vol = {0};
    g_files[0].in_use = true;
    g_files[0].is_dir = false;
    g_files[0].vol    = &vol;
    g_files[0].inode.i_size = 100;

    will_return(write_inode, 0);
    will_return(flush_metadata, 0);

    /* truncate to length=50 — skips free_inode_blocks */
    assert_int_equal(ext2_truncate(&g_files[0], 50), 0);
    assert_int_equal(g_files[0].inode.i_size, 50);
}

/* ext2_open: directory inode opens as is_dir=true */
static void test_open_directory_inode(void **state) {
    (void)state;
    ext2_volume_t vol = {.mounted = true};
    ext2_inode_t inode = {0};
    inode.i_mode = EXT2_S_IFDIR | 0755;

    will_return(resolve_path, 0);
    will_return(resolve_path, 10);
    will_return(resolve_path, &inode);

    ext2_file_t *f = ext2_open(&vol, "/dir");
    assert_non_null(f);
    assert_true(f->is_dir);
}

int main(void) {
    const struct CMUnitTest tests[] = {
        cmocka_unit_test_setup(test_close_dirty, setup_test),
        cmocka_unit_test_setup(test_close_unlinked_shared, setup_test),
        cmocka_unit_test_setup(test_close_unlinked_single, setup_test),
        cmocka_unit_test_setup(test_create_already_exists, setup_test),
        cmocka_unit_test_setup(test_create_invalid_parent, setup_test),
        cmocka_unit_test_setup(test_create_success, setup_test),
        cmocka_unit_test_setup(test_flush_clean, setup_test),
        cmocka_unit_test_setup(test_flush_dirty, setup_test),
        cmocka_unit_test_setup(test_open_missing, setup_test),
        cmocka_unit_test_setup(test_open_success, setup_test),
        cmocka_unit_test_setup(test_open_too_many, setup_test),
        cmocka_unit_test_setup(test_read_clamped_size, setup_test),
        cmocka_unit_test_setup(test_read_past_eof, setup_test),
        cmocka_unit_test_setup(test_read_sparse, setup_test),
        cmocka_unit_test_setup(test_truncate_shrink, setup_test),
        cmocka_unit_test_setup(test_write_allocates_block, setup_test),
        cmocka_unit_test_setup(test_read_null_file, setup_test),
        cmocka_unit_test_setup(test_read_dir_file, setup_test),
        cmocka_unit_test_setup(test_read_not_in_use, setup_test),
        cmocka_unit_test_setup(test_read_from_block, setup_test),
        cmocka_unit_test_setup(test_write_null_file, setup_test),
        cmocka_unit_test_setup(test_write_dir_file, setup_test),
        cmocka_unit_test_setup(test_write_zero_count, setup_test),
        cmocka_unit_test_setup(test_write_existing_block, setup_test),
        cmocka_unit_test_setup(test_truncate_null, setup_test),
        cmocka_unit_test_setup(test_truncate_dir, setup_test),
        cmocka_unit_test_setup(test_truncate_nonzero, setup_test),
        cmocka_unit_test_setup(test_open_directory_inode, setup_test),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
