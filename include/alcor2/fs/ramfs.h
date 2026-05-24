/**
 * @file include/alcor2/fs/ramfs.h
 * @brief Public ramfs API: init + character-device registration.
 *
 * Ramfs backs both the root fallback (when there is no disk) and the
 * @c /dev mount. Drivers register character-device callbacks against a
 * ramfs path; reads/writes/ioctls on that node are routed to the callback
 * table instead of the in-memory byte buffer.
 */

#ifndef ALCOR2_FS_RAMFS_H
#define ALCOR2_FS_RAMFS_H

#include <alcor2/types.h>

/** @brief Driver-supplied callback table for a character device. */
typedef struct ramfs_chardev_ops
{
  i64 (*read)(void *ctx, void *buf, u64 count, u64 offset);
  i64 (*write)(void *ctx, const void *buf, u64 count, u64 offset);
  i64 (*ioctl)(void *ctx, u64 request, u64 arg);
} ramfs_chardev_ops_t;

/** @brief Initialise ramfs (idempotent). */
void ramfs_init(void);

/**
 * @brief Register a character device under @p path inside the ramfs tree.
 *
 * @p path is rooted at the ramfs root (which the kernel mounts at @c /dev),
 * so @c "/mouse" surfaces as @c /dev/mouse to userspace. Parent directories
 * must exist. The registered node cannot be removed via @c unlink / @c rmdir.
 *
 * @param path Ramfs-relative path; must start with @c /.
 * @param ops  Callback table (pointer must outlive the kernel; statics OK).
 * @param ctx  Opaque pointer forwarded to each callback.
 * @return 0 on success, negative @c -errno on failure.
 */
i64 ramfs_chardev_register(
    const char *path, const ramfs_chardev_ops_t *ops, void *ctx
);

#endif
