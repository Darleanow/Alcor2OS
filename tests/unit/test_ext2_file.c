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

static int g_kmalloc_fail_on = -1;
static int g_kmalloc_call    = 0;
void *kmalloc(u64 n) {
    g_kmalloc_call++;
    if(g_kmalloc_fail_on >= 0 && g_kmalloc_call == g_kmalloc_fail_on) return NULL;
    return malloc(n);
}
void kfree(void *p) { if(p) free(p); }

static bool g_cache_fail = false;
u8 *cache_get_block(u32 s) { return g_cache_fail ? NULL : malloc(s); }
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
    g_cache_fail     = false;
    g_kmalloc_fail_on = -1;
    g_kmalloc_call    = 0;
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

/* claim_free_file_slot: all slots in use → ext2_open returns NULL (no resolve called) */
static void test_open_all_slots_used_null(void **state) {
    (void)state;
    ext2_volume_t vol = {.mounted = true};
    /* Mark all slots in use */
    for(int i = 0; i < EXT2_MAX_FILES; i++)
        g_files[i].in_use = true;
    /* claim_free_file_slot returns NULL before resolve_path is called */
    assert_null(ext2_open(&vol, "/f"));
}

/* ext2_read: run-length detection triggers read_run_chunk */
static i64 mock_dev_read(void *ctx, u64 lba, u32 count, void *buf) {
    (void)ctx; (void)lba; (void)count;
    memset(buf, 0, count * 512);
    return (i64)(count * 512);
}
static void test_read_run_chunk_success(void **state) {
    (void)state;
    static blockdev_t dev = {.read = mock_dev_read, .ctx = NULL};
    ext2_volume_t vol = {.mounted = true, .block_size = 1024, .dev = &dev};
    g_files[0].in_use = true;
    g_files[0].is_dir = false;
    g_files[0].vol    = &vol;
    g_files[0].inode.i_size = 3072; /* 3 blocks */

    /* Read 2048 bytes: max_run = ceil(2048/1024) = 2 → detect runs up to 2 */
    will_return(get_block_num, 10); /* file block 0 → disk block 10 */
    will_return(get_block_num, 11); /* run[1]: 11=10+1 → contiguous, run=2, stop (max_run=2) */

    char buf[2048];
    i64 ret = ext2_read(&g_files[0], buf, 2048, 0);
    assert_true(ret >= 0);
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

/* ext2_read: cache_get_block fails → -ENOMEM */
static void test_read_no_cache_enomem(void **state) {
    (void)state;
    ext2_volume_t vol = {.mounted = true, .block_size = 1024};
    g_files[0].in_use = true;
    g_files[0].is_dir = false;
    g_files[0].vol    = &vol;
    g_files[0].inode.i_size = 100;
    g_cache_fail = true; /* cache_get_block returns NULL */
    char buf[10];
    assert_int_equal((i64)ext2_read(&g_files[0], buf, 10, 0), -ENOMEM);
}

/* ext2_read: vol_read_block fails → -EIO */
static void test_read_io_error(void **state) {
    (void)state;
    ext2_volume_t vol = {.mounted = true, .block_size = 1024};
    g_files[0].in_use = true;
    g_files[0].is_dir = false;
    g_files[0].vol    = &vol;
    g_files[0].inode.i_size = 1024;

    will_return(get_block_num, 5); /* block exists */
    will_return(vol_read_block, -1); /* read fails */

    char buf[512];
    assert_int_equal((i64)ext2_read(&g_files[0], buf, 512, 0), -EIO);
}

/* ext2_write: cache_get_block fails → -ENOMEM */
static void test_write_no_cache_enomem(void **state) {
    (void)state;
    ext2_volume_t vol = {.block_size = 1024, .inodes_per_group = 100};
    g_files[0].in_use = true;
    g_files[0].is_dir = false;
    g_files[0].vol    = &vol;
    g_files[0].inode_num = 1;
    g_files[0].inode.i_size = 0;
    g_cache_fail = true;
    char buf[4] = "hi";
    assert_int_equal((i64)ext2_write(&g_files[0], buf, 2, 0), -ENOMEM);
}

/* ext2_create: dir_add_entry fails → rollback inode */
static void test_create_link_fails_rollback(void **state) {
    (void)state;
    ext2_volume_t vol = {.mounted = true, .inodes_per_group = 100};

    /* 1. resolve_path(path): file doesn't exist yet */
    will_return(resolve_path, -ENOENT);

    /* 2. path_split */
    will_return(path_split, "/");
    will_return(path_split, "newfile");

    /* 3. validate_parent_for_create → resolve_path(parent) succeeds */
    static ext2_inode_t parent = {.i_mode = EXT2_S_IFDIR | 0755};
    will_return(resolve_path, 0);
    will_return(resolve_path, (u32)2);
    will_return(resolve_path, &parent);

    /* 4. create_file_inode_and_link */
    will_return(alloc_inode, 10);
    will_return(write_inode, 0);
    /* dir_add_entry fails → rollback */
    will_return(dir_add_entry, -EIO);
    will_return(free_inode, 0);

    ext2_file_t *f = ext2_create(&vol, "/newfile");
    assert_null(f);
}

/* ext2_open: null vol → NULL */
static void test_open_null_vol(void **state) {
    (void)state;
    assert_null(ext2_open(NULL, "/f"));
}

/* ext2_open: unmounted vol → NULL */
static void test_open_unmounted(void **state) {
    (void)state;
    ext2_volume_t vol = {.mounted = false};
    assert_null(ext2_open(&vol, "/f"));
}

static void test_open_null_path(void **state) {
    (void)state;
    ext2_volume_t vol = {.mounted = true};
    assert_null(ext2_open(&vol, NULL));
}

/* ext2_close: null → no-op, no crash */
static void test_close_null(void **state) {
    (void)state;
    ext2_close(NULL); /* should not crash */
}

/* ext2_close: !in_use → no-op */
static void test_close_not_in_use(void **state) {
    (void)state;
    g_files[0].in_use = false;
    ext2_close(&g_files[0]); /* should not crash */
}

/* ext2_write: alloc_file_block fails → -ENOSPC */
static void test_write_alloc_fails_enospc(void **state) {
    (void)state;
    ext2_volume_t vol = {.block_size = 1024, .inodes_per_group = 100};
    g_files[0].in_use    = true;
    g_files[0].is_dir    = false;
    g_files[0].vol       = &vol;
    g_files[0].inode_num = 1;
    g_files[0].inode.i_size = 0;

    will_return(get_block_num, 0);    /* no existing block */
    will_return(alloc_file_block, 0); /* alloc fails */

    char buf[4] = "test";
    i64 ret = ext2_write(&g_files[0], buf, 4, 0);
    assert_int_equal(ret, -ENOSPC);
}

/* ext2_write: vol_read_block fails during partial r-m-w → -EIO */
static void test_write_read_rmw_fail_eio(void **state) {
    (void)state;
    ext2_volume_t vol = {.block_size = 1024, .inodes_per_group = 100};
    g_files[0].in_use    = true;
    g_files[0].is_dir    = false;
    g_files[0].vol       = &vol;
    g_files[0].inode_num = 1;
    g_files[0].inode.i_size = 1024;

    /* Write 4 bytes at offset 0 (partial → needs r-m-w) */
    will_return(get_block_num, 5);      /* block exists */
    will_return(vol_read_block, -1);    /* read fails */

    char buf[4] = "test";
    assert_int_equal((i64)ext2_write(&g_files[0], buf, 4, 0), -EIO);
}

/* ext2_write: vol_write_block fails → -EIO */
static void test_write_write_block_fail_eio(void **state) {
    (void)state;
    ext2_volume_t vol = {.block_size = 1024, .inodes_per_group = 100};
    g_files[0].in_use    = true;
    g_files[0].is_dir    = false;
    g_files[0].vol       = &vol;
    g_files[0].inode_num = 1;
    g_files[0].inode.i_size = 1024;

    will_return(get_block_num, 5);   /* block exists */
    will_return(vol_read_block, 0);  /* read ok */
    will_return(vol_write_block, -1);/* write fails */

    char buf[4] = "test";
    assert_int_equal((i64)ext2_write(&g_files[0], buf, 4, 0), -EIO);
}

/* ext2_write: partial write then error → return bytes already written */
static void test_write_partial_then_error(void **state) {
    (void)state;
    /* Write 2*block_size bytes: first block succeeds, second fails → return 1024 */
    ext2_volume_t vol = {.block_size = 1024, .inodes_per_group = 100};
    g_files[0].in_use    = true;
    g_files[0].is_dir    = false;
    g_files[0].vol       = &vol;
    g_files[0].inode_num = 1;
    g_files[0].inode.i_size = 2048;

    /* First block (offset=0, full block): exists, no r-m-w, write succeeds */
    will_return(get_block_num, 5);
    will_return(vol_write_block, 0);
    /* Second block (offset=1024, full block): exists, write fails */
    will_return(get_block_num, 6);
    will_return(vol_write_block, -1);

    char buf[2048];
    i64 ret = ext2_write(&g_files[0], buf, 2048, 0);
    assert_int_equal(ret, 1024); /* partial: first block succeeded */
}

/* create_file_inode_and_link: alloc_inode returns 0 → ext2_create returns NULL */
static void test_create_alloc_inode_fails(void **state) {
    (void)state;
    ext2_volume_t vol = {.mounted = true, .inodes_per_group = 100};

    will_return(resolve_path, -ENOENT); /* file doesn't exist */

    will_return(path_split, "/");
    will_return(path_split, "newfile");

    static ext2_inode_t parent = {.i_mode = EXT2_S_IFDIR | 0755};
    will_return(resolve_path, 0);
    will_return(resolve_path, (u32)2);
    will_return(resolve_path, &parent);

    will_return(alloc_inode, 0); /* fails */

    assert_null(ext2_create(&vol, "/newfile"));
}

/* ext2_create: null vol → NULL */
static void test_create_null_vol(void **state) {
    (void)state;
    assert_null(ext2_create(NULL, "/f"));
}

static void test_create_unmounted(void **state) {
    (void)state;
    ext2_volume_t vol = {.mounted = false};
    assert_null(ext2_create(&vol, "/f"));
}

static void test_create_null_path(void **state) {
    (void)state;
    ext2_volume_t vol = {.mounted = true};
    assert_null(ext2_create(&vol, NULL));
}

/* ext2_create: all file slots in use → NULL (claim_free_file_slot fails) */
static void test_create_all_slots_used(void **state) {
    (void)state;
    ext2_volume_t vol = {.mounted = true, .inodes_per_group = 100};

    will_return(resolve_path, -ENOENT); /* file doesn't exist */

    for(int i = 0; i < EXT2_MAX_FILES; i++)
        g_files[i].in_use = true; /* all slots taken → claim returns NULL */

    assert_null(ext2_create(&vol, "/newfile"));
}

/* ext2_create: empty filename → NULL */
static void test_create_empty_filename(void **state) {
    (void)state;
    ext2_volume_t vol = {.mounted = true, .inodes_per_group = 100};

    will_return(resolve_path, -ENOENT);

    will_return(path_split, "/");
    will_return(path_split, ""); /* empty name */

    assert_null(ext2_create(&vol, "/"));
}

/* ext2_truncate: write_inode fails → -EIO */
static void test_truncate_write_inode_fail(void **state) {
    (void)state;
    ext2_volume_t vol = {0};
    g_files[0].in_use = true;
    g_files[0].is_dir = false;
    g_files[0].vol    = &vol;
    g_files[0].inode.i_size = 100;

    will_return(write_inode, -1); /* fails */

    assert_int_equal((i64)ext2_truncate(&g_files[0], 50), -EIO);
}

/* ext2_flush: !in_use → -EINVAL */
static void test_flush_not_in_use(void **state) {
    (void)state;
    g_files[0].in_use = false;
    assert_int_equal((i64)ext2_flush(&g_files[0]), -EINVAL);
}

/* ext2_flush: write_inode fails → -EIO */
static void test_flush_write_inode_fail(void **state) {
    (void)state;
    ext2_volume_t vol = {0};
    g_files[0].in_use = true;
    g_files[0].dirty  = true;
    g_files[0].vol    = &vol;

    will_return(write_inode, -1);

    assert_int_equal((i64)ext2_flush(&g_files[0]), -EIO);
}

/* ext2_flush: flush_metadata fails → -EIO */
static void test_flush_metadata_fail(void **state) {
    (void)state;
    ext2_volume_t vol = {0};
    g_files[0].in_use = true;
    g_files[0].dirty  = true;
    g_files[0].vol    = &vol;

    will_return(write_inode, 0);
    will_return(flush_metadata, -1);

    assert_int_equal((i64)ext2_flush(&g_files[0]), -EIO);
}

/* detect_run_length: non-contiguous blocks → break (line 171).
 * Uses blockdev (mock_dev_read) so cache_get_block returns a run_buf. */
static void test_detect_run_non_contiguous_break(void **state) {
    (void)state;
    static blockdev_t dev = {.read = mock_dev_read, .ctx = NULL};
    ext2_volume_t vol = {.mounted = true, .block_size = 1024, .dev = &dev};
    g_files[0].in_use = true;
    g_files[0].is_dir = false;
    g_files[0].vol    = &vol;
    g_files[0].inode.i_size = 2048;

    /* Read 2048 bytes with non-contiguous blocks.
     * With blockdev, cache_get_block provides a run_buf.
     * detect_run: block[0]=10, block[1]=15 (≠11) → break, run=1 → single read.
     * Then block[1]=15, block[2]=20 (≠16) → break, run=1 → single read. */
    will_return(get_block_num, 10); /* file block 0 → disk block 10 */
    will_return(get_block_num, 15); /* detect_run: 15≠11 → break, run=1 */
    /* run=1: still takes read_run_chunk since run_buf non-null? No:
     * if(run > 1) → only goes to run_chunk if run > 1.
     * run=1 → falls to read_single_block_chunk → vol_read_block */
    will_return(vol_read_block, 0);
    /* Next iteration: block 1 (max_run=1 → detect_run loop skipped) */
    will_return(get_block_num, 15); /* file block 1 */
    /* max_run=1 → while(1<1) false → no detect_run get_block_num call */
    will_return(vol_read_block, 0);

    char buf[2048];
    i64 ret = ext2_read(&g_files[0], buf, 2048, 0);
    assert_true(ret >= 0);
}

/* read_run_chunk: vol_read_sectors fails → -EIO (line 233).
 * Covered indirectly via a dev that returns error. Since mock_dev_read
 * always succeeds, we verify the function doesn't crash when run > 1. */
static i64 failing_dev_read(void *ctx, u64 lba, u32 cnt, void *buf) {
    (void)ctx; (void)lba; (void)cnt; (void)buf; return -1;
}
static void test_read_run_chunk_vol_read_sectors_fail(void **state) {
    (void)state;
    static blockdev_t fail_dev = {.read = failing_dev_read, .ctx = NULL};
    ext2_volume_t vol = {.mounted = true, .block_size = 1024, .dev = &fail_dev};
    g_files[0].in_use = true;
    g_files[0].is_dir = false;
    g_files[0].vol    = &vol;
    g_files[0].inode.i_size = 2048;

    /* Return 2 contiguous blocks → run=2 → read_run_chunk → vol_read_sectors fails → -EIO */
    will_return(get_block_num, 10);
    will_return(get_block_num, 11); /* contiguous */
    /* read_run_chunk called → failing_dev_read returns -1 → -EIO */

    char buf[2048];
    i64 ret = ext2_read(&g_files[0], buf, 2048, 0);
    assert_int_equal(ret, -EIO);
}

/* read_one_chunk: max_run > EXT2_READ_RUN_MAX → capped (line 272) */
static void test_read_max_run_capped(void **state) {
    (void)state;
    static blockdev_t dev = {.read = mock_dev_read, .ctx = NULL};
    ext2_volume_t vol = {.mounted = true, .block_size = 1024, .dev = &dev};
    g_files[0].in_use = true;
    g_files[0].is_dir = false;
    g_files[0].vol    = &vol;
    g_files[0].inode.i_size = 1024 * 20;

    /* Read 17 blocks: max_run = 17 > EXT2_READ_RUN_MAX(16) → capped to 16 */
    /* 1st iter: 1 call for block 0, 15 calls in detect_run (max_run=16, loop 1..15) */
    will_return(get_block_num, 5);
    for(int i = 1; i <= 15; i++)
        will_return(get_block_num, 5 + i);
    /* 2nd iter: 1 call for block 16 */
    will_return(get_block_num, 22);
    /* run=1 → single block path → vol_read_block */
    will_return(vol_read_block, 0);

    char buf[17 * 1024];
    i64 ret = ext2_read(&g_files[0], buf, sizeof(buf), 0);
    assert_true(ret > 0);
}

/* ext2_write: is_dir=true → -EINVAL (covers !file->in_use||file->is_dir branch) */
static void test_write_is_dir(void **state) {
    (void)state;
    g_files[0].in_use = true;
    g_files[0].is_dir = true;
    char buf[4] = "abcd";
    assert_int_equal((i64)ext2_write(&g_files[0], buf, 4, 0), -EINVAL);
}

/* ext2_write: count == 0 → 0 */
static void test_write_count_zero(void **state) {
    (void)state;
    g_files[0].in_use = true;
    g_files[0].is_dir = false;
    assert_int_equal(ext2_write(&g_files[0], "x", 0, 0), 0);
}

/* ext2_truncate: is_dir → -EINVAL (covers middle term of ||) */
static void test_truncate_is_dir(void **state) {
    (void)state;
    g_files[0].in_use = true;
    g_files[0].is_dir = true;
    assert_int_equal((i64)ext2_truncate(&g_files[0], 0), -EINVAL);
}

/* ext2_flush: !in_use → -EINVAL */
static void test_flush_null_file(void **state) {
    (void)state;
    assert_int_equal((i64)ext2_flush(NULL), -EINVAL);
}

/* ext2_read: run_buf kmalloc fails → run_buf=NULL → single-block path only */
static void test_read_run_buf_null(void **state) {
    (void)state;
    static blockdev_t dev = {.read = mock_dev_read, .ctx = NULL};
    ext2_volume_t vol = {.mounted = true, .block_size = 1024, .dev = &dev};
    g_files[0].in_use = true;
    g_files[0].is_dir = false;
    g_files[0].vol    = &vol;
    g_files[0].inode.i_size = 1024;
    /* Fail kmalloc on call 1 → run_buf=NULL → if(run_buf) false branch */
    g_kmalloc_fail_on = 1;
    g_kmalloc_call    = 0;
    will_return(get_block_num, 5);
    will_return(vol_read_block, 0);
    char buf[1024];
    i64 ret = ext2_read(&g_files[0], buf, sizeof(buf), 0);
    assert_true(ret >= 0);
    g_kmalloc_fail_on = -1;
}

/* ext2_close: g_files[0] in_use with same vol but different inode_num → branch False */
static void test_close_open_count_different_inode(void **state) {
    (void)state;
    ext2_volume_t vol = {.mounted = true};
    g_files[0].in_use    = true;
    g_files[0].vol       = &vol;
    g_files[0].inode_num = 99;
    g_files[1].in_use          = true;
    g_files[1].vol             = &vol;
    g_files[1].inode_num       = 5;
    g_files[1].dirty           = false;
    g_files[1].inode.i_links_count = 1;
    ext2_close(&g_files[1]);
}

/* validate_parent_for_create: resolve_path fails → returns false (line 553) */
static void test_validate_parent_resolve_fail(void **state) {
    (void)state;
    ext2_volume_t vol = {.mounted = true, .inodes_per_group = 100};

    /* File doesn't exist */
    will_return(resolve_path, -ENOENT);

    will_return(path_split, "/nonexistent");
    will_return(path_split, "file");

    /* Parent resolve fails → validate_parent_for_create returns false */
    will_return(resolve_path, -ENOENT);

    ext2_file_t *f = ext2_create(&vol, "/nonexistent/file");
    assert_null(f);
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
        cmocka_unit_test_setup(test_open_all_slots_used_null, setup_test),
        cmocka_unit_test_setup(test_read_run_chunk_success, setup_test),
        cmocka_unit_test_setup(test_read_no_cache_enomem, setup_test),
        cmocka_unit_test_setup(test_read_io_error, setup_test),
        cmocka_unit_test_setup(test_write_no_cache_enomem, setup_test),
        cmocka_unit_test_setup(test_create_link_fails_rollback, setup_test),
        /* new coverage */
        cmocka_unit_test_setup(test_open_null_vol, setup_test),
        cmocka_unit_test_setup(test_open_unmounted, setup_test),
        cmocka_unit_test_setup(test_open_null_path, setup_test),
        cmocka_unit_test_setup(test_close_null, setup_test),
        cmocka_unit_test_setup(test_close_not_in_use, setup_test),
        cmocka_unit_test_setup(test_write_alloc_fails_enospc, setup_test),
        cmocka_unit_test_setup(test_write_read_rmw_fail_eio, setup_test),
        cmocka_unit_test_setup(test_write_write_block_fail_eio, setup_test),
        cmocka_unit_test_setup(test_write_partial_then_error, setup_test),
        cmocka_unit_test_setup(test_create_alloc_inode_fails, setup_test),
        cmocka_unit_test_setup(test_create_null_vol, setup_test),
        cmocka_unit_test_setup(test_create_unmounted, setup_test),
        cmocka_unit_test_setup(test_create_null_path, setup_test),
        cmocka_unit_test_setup(test_create_all_slots_used, setup_test),
        cmocka_unit_test_setup(test_create_empty_filename, setup_test),
        cmocka_unit_test_setup(test_truncate_write_inode_fail, setup_test),
        cmocka_unit_test_setup(test_flush_not_in_use, setup_test),
        cmocka_unit_test_setup(test_flush_write_inode_fail, setup_test),
        cmocka_unit_test_setup(test_flush_metadata_fail, setup_test),
        cmocka_unit_test_setup(test_detect_run_non_contiguous_break, setup_test),
        cmocka_unit_test_setup(test_read_run_chunk_vol_read_sectors_fail, setup_test),
        cmocka_unit_test_setup(test_read_max_run_capped, setup_test),
        cmocka_unit_test_setup(test_validate_parent_resolve_fail, setup_test),
        cmocka_unit_test_setup(test_read_run_buf_null, setup_test),
        cmocka_unit_test_setup(test_close_open_count_different_inode, setup_test),
        cmocka_unit_test_setup(test_write_is_dir, setup_test),
        cmocka_unit_test_setup(test_write_count_zero, setup_test),
        cmocka_unit_test_setup(test_truncate_is_dir, setup_test),
        cmocka_unit_test_setup(test_flush_null_file, setup_test),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
