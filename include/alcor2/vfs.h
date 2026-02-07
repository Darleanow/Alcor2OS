/**
 * @file include/alcor2/vfs.h
 * @brief Virtual File System layer.
 *
 * Simple VFS with ramfs backend and FAT32 mounting support.
 */

#ifndef ALCOR2_VFS_H
#define ALCOR2_VFS_H

#include <alcor2/types.h>

/** @brief Maximum filename length. */
#define VFS_NAME_MAX 64

/** @brief Maximum path length. */
#define VFS_PATH_MAX 256

/** @brief Maximum number of files in ramfs. */
#define VFS_MAX_FILES 128

/** @brief Maximum open file descriptors per process. */
#define VFS_MAX_FD 32

/** @name File types
 * @{ */
#define VFS_FILE      1
#define VFS_DIRECTORY 2
/** @} */

/** @name Open flags
 * @{ */
#define O_RDONLY    0x0000
#define O_WRONLY    0x0001
#define O_RDWR      0x0002
#define O_CREAT     0x0040
#define O_TRUNC     0x0200
#define O_APPEND    0x0400
#define O_DIRECTORY 0x10000
#define O_CLOEXEC   0x80000
/** @} */

/** @name Seek modes
 * @{ */
#define SEEK_SET 0
#define SEEK_CUR 1
#define SEEK_END 2
/** @} */

/**
 * @brief Directory entry (for readdir).
 */
typedef struct
{
  char name[VFS_NAME_MAX];
  u8   type;
  u64  size;
} vfs_dirent_t;

/**
 * @brief File stat structure.
 */
typedef struct
{
  u64 size;
  u8  type;
  u64 created;
  u64 modified;
} vfs_stat_t;

/**
 * @brief VFS node (inode equivalent).
 */
typedef struct vfs_node
{
  char             name[VFS_NAME_MAX];
  u8               type;
  u64              size;
  u8              *data;
  u64              capacity;
  struct vfs_node *parent;
  struct vfs_node *children;
  struct vfs_node *next;
} vfs_node_t;

/**
 * @brief File descriptor.
 */
typedef struct
{
  vfs_node_t *node;
  u64         offset;
  u32         flags;
  bool        in_use;
  u64         owner_pid;
} vfs_fd_t;

/**
 * @brief Directory handle.
 */
typedef struct
{
  vfs_node_t *node;     /**< VFS node (ramfs) or FAT32 file handle */
  vfs_node_t *current;  /**< Current child for ramfs iteration */
  u64         index;    /**< Entry index for iteration */
  bool        in_use;   /**< Handle is active */
  bool        is_fat32; /**< true if FAT32 directory, false if ramfs */
} vfs_dir_t;

/**
 * @brief Initialize VFS and mount ramfs at root.
 */
void vfs_init(void);

/**
 * @brief Open a file.
 * @param path File path.
 * @param flags Open flags.
 * @return File descriptor, or negative on error.
 */
i64 vfs_open(const char *path, u32 flags);

/**
 * @brief Close a file descriptor.
 * @param fd File descriptor.
 * @return 0 on success, negative on error.
 */
i64 vfs_close(i64 fd);

/**
 * @brief Read from file.
 * @param fd File descriptor.
 * @param buf Destination buffer.
 * @param count Bytes to read.
 * @return Bytes read, or negative on error.
 */
i64 vfs_read(i64 fd, void *buf, u64 count);

/**
 * @brief Write to file.
 * @param fd File descriptor.
 * @param buf Source buffer.
 * @param count Bytes to write.
 * @return Bytes written, or negative on error.
 */
i64 vfs_write(i64 fd, const void *buf, u64 count);

/**
 * @brief Seek in file.
 * @param fd File descriptor.
 * @param offset Seek offset.
 * @param whence SEEK_SET, SEEK_CUR, or SEEK_END.
 * @return New position, or negative on error.
 */
i64 vfs_seek(i64 fd, i64 offset, i32 whence);

/**
 * @brief Get file stats.
 * @param path File path.
 * @param stat Output stat structure.
 * @return 0 on success, negative on error.
 */
i64 vfs_stat(const char *path, vfs_stat_t *stat);

/**
 * @brief Create a directory.
 * @param path Directory path.
 * @return 0 on success, negative on error.
 */
i64 vfs_mkdir(const char *path);

/**
 * @brief Open a directory for reading.
 * @param path Directory path.
 * @return Directory handle, or negative on error.
 */
i64 vfs_opendir(const char *path);

/**
 * @brief Read next directory entry.
 * @param dirfd Directory handle.
 * @param entry Output entry.
 * @return 1 if entry read, 0 if end, negative on error.
 */
i64 vfs_readdir(i64 dirfd, vfs_dirent_t *entry);

/**
 * @brief Close directory handle.
 * @param dirfd Directory handle.
 * @return 0 on success, negative on error.
 */
i64 vfs_closedir(i64 dirfd);

/**
 * @brief Create an empty file.
 * @param path File path.
 * @return 0 on success, negative on error.
 */
i64 vfs_touch(const char *path);

/**
 * @brief Remove a file (not directory).
 * @param path File path.
 * @return 0 on success, negative on error.
 */
i64 vfs_unlink(const char *path);

/**
 * @brief Get current working directory.
 * @return CWD path string.
 */
const char *vfs_getcwd(void);

/**
 * @brief Change current working directory.
 * @param path New working directory.
 * @return 0 on success, negative on error.
 */
i64 vfs_chdir(const char *path);

/**
 * @brief Get directory entries in Linux format.
 * @param fd File descriptor opened with O_DIRECTORY.
 * @param buf Buffer to fill with dirent structures.
 * @param count Size of buffer.
 * @return Bytes written, 0 at end, negative on error.
 */
i64 vfs_getdents(i64 fd, void *buf, u64 count);

/**
 * @brief Mount a filesystem.
 * @param source Device path or NULL for ramfs.
 * @param target Mount point.
 * @param fstype Filesystem type ("ramfs", "fat32").
 * @return 0 on success, negative on error.
 */
i64 vfs_mount(const char *source, const char *target, const char *fstype);

/**
 * @brief Unmount a filesystem.
 * @param target Mount point.
 * @return 0 on success, negative on error.
 */
i64 vfs_umount(const char *target);

/**
 * @brief Close all FDs owned by a specific PID.
 * @param pid Process ID.
 */
void vfs_close_for_pid(u64 pid);

#endif
