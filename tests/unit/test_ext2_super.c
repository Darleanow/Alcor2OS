#include "test_common.h"
#include <alcor2/types.h>
#include <string.h>
#include <alcor2/errno.h>
#include <alcor2/fs/ext2.h>
#include <fs/ext2/internal.h>

// Mocks

void *kmalloc(u64 size) {
    static u8 heap[65536 * 4];
    static u64 offset = 0;
    if (offset + size > sizeof(heap)) return NULL;
    void *ptr = &heap[offset];
    offset += size;
    return ptr;
}

void kfree(void *ptr) {
    (void)ptr;
}



const fs_type_t g_ext2_fstype = { .name = "ext2" };

bool vfs_registered = false;
i64 vfs_register_fs(const fs_type_t *fstype) {
    (void)fstype;
    vfs_registered = true;
    return 0;
}

bool mock_dev_write_called = false;

u8 mock_disk[8192];
u64 fail_lba_start = -1ULL;
u64 fail_lba_end = -1ULL;

i64 mock_dev_read(void *ctx, u64 lba, u32 count, void *buf) {
    (void)ctx;
    if (lba >= fail_lba_start && lba < fail_lba_end) return -EIO;
    if (lba * 512 >= sizeof(mock_disk)) return -EIO;
    memcpy(buf, mock_disk + lba * 512, count * 512);
    return count * 512;
}

i64 mock_dev_write(void *ctx, u64 lba, u32 count, const void *buf) {
    (void)ctx;
    mock_dev_write_called = true;
    if (lba * 512 >= sizeof(mock_disk)) return -EIO;
    memcpy(mock_disk + lba * 512, buf, count * 512);
    return count * 512;
}

const blockdev_t mock_dev = {
    .ctx = NULL,
    .read = mock_dev_read,
    .write = mock_dev_write,
};

#include "../../src/fs/ext2/super.c"

void console_print(const char *s) { (void)s; }
void console_printf(const char *fmt, ...) { (void)fmt; }
ext2_file_t g_files[EXT2_MAX_FILES];

static void setup_valid_sb(void) {
    memset(mock_disk, 0, sizeof(mock_disk));
    ext2_superblock_t *sb = (ext2_superblock_t *)(mock_disk + 1024);
    sb->s_magic = EXT2_MAGIC;
    sb->s_log_block_size = 0; // 1024 bytes
    sb->s_blocks_per_group = 8192;
    sb->s_inodes_per_group = 8192;
    sb->s_rev_level = 0;
    sb->s_first_data_block = 1;
    sb->s_blocks_count = 8192;
    sb->s_inodes_count = 8192;
}

static void ext2_init_clears_state(void **state) {
    (void)state;
    g_volumes[0].mounted = true;
    vfs_registered = false;
    ext2_init(&mock_dev);
    assert_false(g_volumes[0].mounted);
    assert_true(vfs_registered);
    assert_ptr_equal(g_default_dev, &mock_dev);
}

static void ext2_mount_fails_on_eio(void **state) {
    (void)state;
    ext2_init(&mock_dev);
    ext2_volume_t *vol = ext2_mount(&mock_dev, 99999); // Out of bounds LBA
    assert_null(vol);
}

static void ext2_mount_fails_on_invalid_magic(void **state) {
    (void)state;
    ext2_init(&mock_dev);
    setup_valid_sb();
    ext2_superblock_t *sb = (ext2_superblock_t *)(mock_disk + 1024);
    sb->s_magic = 0x1234;
    ext2_volume_t *vol = ext2_mount(&mock_dev, 0);
    assert_null(vol);
}

static void ext2_mount_fails_when_slots_full(void **state) {
    (void)state;
    ext2_init(&mock_dev);
    for (int i = 0; i < EXT2_MAX_VOLUMES; i++) {
        g_volumes[i].mounted = true;
    }
    setup_valid_sb();
    ext2_volume_t *vol = ext2_mount(&mock_dev, 0);
    assert_null(vol);
}

static void ext2_mount_validates_superblock_cleanly(void **state) {
    (void)state;
    ext2_init(&mock_dev);
    setup_valid_sb();
    ext2_volume_t *vol = ext2_mount(&mock_dev, 0);
    assert_non_null(vol);
    assert_true(vol->mounted);
    assert_int_equal(vol->block_size, 1024);
}

static void ext2_mount_rev1_inode_size(void **state) {
    (void)state;
    ext2_init(&mock_dev);
    setup_valid_sb();
    ext2_superblock_t *sb = (ext2_superblock_t *)(mock_disk + 1024);
    sb->s_rev_level = 1;
    sb->s_inode_size = 256;
    ext2_volume_t *vol = ext2_mount(&mock_dev, 0);
    assert_non_null(vol);
    assert_int_equal(vol->inode_size, 256);
}

static void ext2_mount_rev0_inode_size(void **state) {
    (void)state;
    ext2_init(&mock_dev);
    setup_valid_sb();
    ext2_superblock_t *sb = (ext2_superblock_t *)(mock_disk + 1024);
    sb->s_rev_level = 0;
    sb->s_inode_size = 256; // Should be ignored
    ext2_volume_t *vol = ext2_mount(&mock_dev, 0);
    assert_non_null(vol);
    assert_int_equal(vol->inode_size, 128);
}

static void ext2_unmount_flushes_metadata_and_frees_volume(void **state) {
    (void)state;
    ext2_init(&mock_dev);
    setup_valid_sb();
    ext2_volume_t *vol = ext2_mount(&mock_dev, 0);
    assert_non_null(vol);
    mock_dev_write_called = false;
    
    ext2_unmount(vol);
    assert_true(mock_dev_write_called);
    assert_false(vol->mounted);
}

static void ext2_mount_fails_on_gdt_read_error(void **state) {
    (void)state;
    ext2_init(&mock_dev);
    setup_valid_sb();
    
    // GDT is at block 2, which corresponds to LBA 4 (since block size is 1024 and sector size is 512).
    // Let's just make the dev_read fail on LBA 4 and 5.
    fail_lba_start = 4;
    fail_lba_end = 6;
    
    ext2_volume_t *vol = ext2_mount(&mock_dev, 0);
    assert_null(vol); 
    
    fail_lba_start = -1ULL;
    fail_lba_end = -1ULL;
}

static void ext2_unmount_ignores_null_or_unmounted(void **state) {
    (void)state;
    mock_dev_write_called = false;
    ext2_unmount(NULL);
    assert_false(mock_dev_write_called);
    
    ext2_volume_t unmounted_vol = { .mounted = false };
    ext2_unmount(&unmounted_vol);
    assert_false(mock_dev_write_called);
}

int main(void) {
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(ext2_init_clears_state),
        cmocka_unit_test(ext2_mount_fails_on_eio),
        cmocka_unit_test(ext2_mount_fails_on_gdt_read_error),
        cmocka_unit_test(ext2_mount_fails_on_invalid_magic),
        cmocka_unit_test(ext2_mount_fails_when_slots_full),
        cmocka_unit_test(ext2_mount_rev0_inode_size),
        cmocka_unit_test(ext2_mount_rev1_inode_size),
        cmocka_unit_test(ext2_mount_validates_superblock_cleanly),
        cmocka_unit_test(ext2_unmount_flushes_metadata_and_frees_volume),
        cmocka_unit_test(ext2_unmount_ignores_null_or_unmounted),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
