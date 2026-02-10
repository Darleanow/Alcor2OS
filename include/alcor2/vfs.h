/**
 * @file include/alcor2/vfs.h
 * @brief Virtual File System layer.
 *
 * Simple VFS with ramfs backend and ext2 mounting support.
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

/**
 * @brief Opaque file handle for filesystem operations.
 *
 * This is cast to/from filesystem-specific types (ext2_file_t, vfs_node_t).
 */
typedef void *fs_file_t;

/**
 * @brief Filesystem operations table.
 *
 * Provides an abstraction layer so VFS can operate on any filesystem
 * without knowing its internals. Each filesystem implements these ops.
 */
typedef struct fs_ops
{
  /**
   * @brief Open a file by path.
   * @param fs_data Filesystem data (e.g., ext2_volume_t*).
   * @param path Path relative to mount point.
   * @param flags Open flags.
   * @param[out] is_dir Set to true if opened file is a directory.
   * @return Opaque file handle, or NULL on error.
   */
  fs_file_t (*open)(void *fs_data, const char *path, u32 flags, bool *is_dir);

  /**
   * @brief Create and open a new file.
   * @param fs_data Filesystem data.
   * @param path Path relative to mount point.
   * @return Opaque file handle, or NULL on error.
   */
  fs_file_t (*create)(void *fs_data, const char *path);

  /**
   * @brief Close a file handle.
   * @param fh File handle from open/create.
   */
  void (*close)(fs_file_t fh);

  /**
   * @brief Read from file.
   * @param fh File handle.
   * @param buf Destination buffer.
   * @param count Max bytes to read.
   * @return Bytes read, 0 at EOF, negative on error.
   */
  i64 (*read)(fs_file_t fh, void *buf, u64 count);

  /**
   * @brief Write to file.
   * @param fh File handle.
   * @param buf Source buffer.
   * @param count Bytes to write.
   * @return Bytes written, negative on error.
   */
  i64 (*write)(fs_file_t fh, const void *buf, u64 count);

  /**
   * @brief Seek in file.
   * @param fh File handle.
   * @param offset Seek offset.
   * @param whence SEEK_SET/CUR/END.
   * @return New position, negative on error.
   */
  i64 (*seek)(fs_file_t fh, i64 offset, i32 whence);

  /**
   * @brief Truncate file to zero length.
   * @param fh File handle.
   * @return 0 on success, negative on error.
   */
  i64 (*truncate)(fs_file_t fh);

  /**
   * @brief Create a directory.
   * @param fs_data Filesystem data.
   * @param path Path relative to mount.
   * @return 0 on success, negative errno on error.
   */
  i64 (*mkdir)(void *fs_data, const char *path);

  /**
   * @brief Remove a file.
   * @param fs_data Filesystem data.
   * @param path Path relative to mount.
   * @return 0 on success, negative errno on error.
   */
  i64 (*unlink)(void *fs_data, const char *path);

  /**
   * @brief Remove an empty directory.
   * @param fs_data Filesystem data.
   * @param path Path relative to mount.
   * @return 0 on success, negative errno on error.
   */
  i64 (*rmdir)(void *fs_data, const char *path);

  /**
   * @brief Get file/directory info (stat).
   * @param fs_data Filesystem data.
   * @param path Path relative to mount.
   * @param[out] size File size.
   * @param[out] type VFS_FILE or VFS_DIRECTORY.
   * @return 0 on success, negative errno on error.
   */
  i64 (*stat)(void *fs_data, const char *path, u64 *size, u8 *type);

  /**
   * @brief Check if file handle is a directory.
   * @param fh File handle.
   * @return true if directory, false otherwise.
   */
  bool (*is_dir)(fs_file_t fh);

  /**
   * @brief Get current file position.
   * @param fh File handle.
   * @return Current position in file.
   */
  u64 (*get_position)(fs_file_t fh);

  /**
   * @brief Flush file changes to disk.
   * @param fh File handle.
   * @return 0 on success, negative on error.
   */
  i64 (*flush)(fs_file_t fh);

  /**
   * @brief Read next directory entry.
   * @param fh Directory file handle.
   * @param[out] name Output buffer for entry name (VFS_NAME_MAX bytes).
   * @param[out] type Output file type (VFS_FILE or VFS_DIRECTORY).
   * @param[out] size Output file size.
   * @param[out] inode Output inode number.
   * @return 1 if entry read, 0 at end, negative on error.
   */
  i64 (*readdir)(fs_file_t fh, char *name, u8 *type, u64 *size, u64 *inode);
} fs_ops_t;

/**
 * @brief Filesystem type descriptor for registration.
 *
 * Each filesystem registers one of these to be mountable via vfs_mount().
 */
typedef struct
{
  const char     *name; /**< Filesystem type name (e.g., "ext2") */
  const fs_ops_t *ops;  /**< Filesystem operations */

  /**
   * @brief Mount callback.
   * @param drive Drive number (parsed from source).
   * @param partition Partition number.
   * @return Filesystem-specific data pointer, or NULL on failure.
   */
  void *(*mount)(u8 drive, u8 partition);

  /**
   * @brief Unmount callback.
   * @param fs_data Filesystem data from mount().
   */
  void (*unmount)(void *fs_data);
} fs_type_t;

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

/** @name Directory entry types (for linux_dirent)
 * @{ */
#define DT_UNKNOWN 0
#define DT_DIR     4
#define DT_REG     8
/** @} */

/**
 * @brief Linux dirent structure for getdents syscall.
 *
 * Packed structure matching kernel ABI for the getdents syscall.
 */
typedef struct
{
  u64  d_ino;    /**< Inode number */
  i64  d_off;    /**< Offset to next structure */
  u16  d_reclen; /**< Length of this record */
  u8   d_type;   /**< File type (DT_*) */
  char d_name[]; /**< Filename (flexible array) */
} PACKED linux_dirent_t;

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
  vfs_node_t     *node;      /**< VFS node (ramfs) or opaque FS file handle */
  const fs_ops_t *ops;       /**< FS operations (NULL for ramfs) */
  u64             offset;    /**< Current file offset */
  u32             flags;     /**< Open flags */
  bool            in_use;    /**< Descriptor is active */
  u64             owner_pid; /**< Owner process ID */
} vfs_fd_t;

/**
 * @brief Directory handle.
 */
typedef struct
{
  vfs_node_t     *node;    /**< VFS node (ramfs) or opaque FS file handle */
  vfs_node_t     *current; /**< Current child for ramfs iteration */
  const fs_ops_t *ops;     /**< FS operations (NULL for ramfs) */
  u64             index;   /**< Entry index for iteration */
  bool            in_use;  /**< Handle is active */
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
 * @param fstype Filesystem type ("ramfs", "ext2").
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

/**
 * @brief Register a filesystem type.
 *
 * Called by filesystem drivers during init to make themselves mountable.
 *
 * @param fs Filesystem type descriptor.
 * @return 0 on success, negative on error.
 */
i64 vfs_register_fs(const fs_type_t *fs);

#endif
