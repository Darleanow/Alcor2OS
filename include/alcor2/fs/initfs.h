/**
 * @file include/alcor2/fs/initfs.h
 * @brief Read-only, buffer-backed filesystem populated from Limine modules.
 *
 * initfs ships the small userland utilities (@c /bin) inside the boot ISO
 * so they survive a wipe-free reboot — the persistent ext2 disk only needs
 * to hold user data (@c /home, @c /tmp, @c /etc) and the heavier @c apps/
 * binaries that live under @c /usr/bin.
 *
 * The filesystem is flat (no subdirectories) and read-only. Each file's
 * payload is a pointer into a Limine module buffer; nothing is copied.
 */

#ifndef ALCOR2_FS_INITFS_H
#define ALCOR2_FS_INITFS_H

#include <alcor2/types.h>

/** @brief Register the initfs driver with the VFS. Idempotent. */
void initfs_init(void);

/**
 * @brief Add a virtual file to initfs.
 *
 * @p data must remain valid for the lifetime of the kernel — typically a
 * Limine-loaded module address, which the bootloader keeps mapped.
 *
 * @param name  Filename without any path component (e.g. @c "ls").
 * @param data  Pointer to file contents; not copied.
 * @param size  Length in bytes.
 * @return 0 on success, @c -EEXIST if a file with that name is already
 *         registered, negative @c -errno on other failures.
 */
i64 initfs_register(const char *name, const void *data, u64 size);

#endif
