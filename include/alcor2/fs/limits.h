/**
 * @file include/alcor2/fs/limits.h
 * @brief VFS size limits shared between the filesystem layer and proc.h.
 *
 * Isolated so proc.h can use VFS_PATH_MAX and VFS_MAX_FD without pulling
 * in the full VFS API.
 */

#ifndef ALCOR2_FS_LIMITS_H
#define ALCOR2_FS_LIMITS_H

/** @brief Maximum filename component length, not including the NUL byte. */
#define VFS_NAME_MAX 64
/** @brief Maximum absolute path length, including the NUL byte. */
#define VFS_PATH_MAX 256
/** @brief Maximum open file descriptors per process. */
#define VFS_MAX_FD 256

#endif /* ALCOR2_FS_LIMITS_H */
