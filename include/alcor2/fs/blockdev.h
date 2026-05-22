/**
 * @file include/alcor2/fs/blockdev.h
 * @brief Generic block device interface.
 *
 * Filesystems use this instead of calling hardware drivers directly,
 * keeping the FS layer independent of the underlying storage backend.
 */

#ifndef ALCOR2_FS_BLOCKDEV_H
#define ALCOR2_FS_BLOCKDEV_H

#include <alcor2/types.h>

/**
 * @brief Block device operations and context.
 *
 * @c ctx is passed as-is to each operation; drivers use it to hold the
 * drive index or a pointer to their own state.
 */
typedef struct
{
  i64   (*read)(void *ctx, u64 lba, u32 count, void *buf);
  i64   (*write)(void *ctx, u64 lba, u32 count, const void *buf);
  void *ctx;
} blockdev_t;

#endif /* ALCOR2_FS_BLOCKDEV_H */
