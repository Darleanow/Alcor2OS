/**
 * @file include/alcor2/fs/vfs.h
 * @brief Virtual File System orchestration layer.
 *
 * The VFS presents a uniform file-descriptor interface to processes while
 * dispatching I/O to interchangeable filesystem drivers.  Three distinct
 * layers are stacked on top of each other:
 *
 * @par Architecture
 * - **Filesystem drivers** expose a ::fs_ops_t table.  Each driver registers
 *   a ::fs_type_t with ::vfs_register_fs and is selected by name at mount
 *   time.
 * - **Open File Table (OFT)** holds the kernel-side state for every open file
 *   description (a kernel-side open file description).  An OFT entry lives
 *   as long as at least one file descriptor references it; the reference count
 *   is managed by ::vfs_oft_retain and ::vfs_oft_release.
 * - **Per-process fd table** maps small integers (file descriptors) to OFT
 *   indices.  File descriptors 0–2 are reserved for stdio and handled by
 *   fallback paths in the I/O syscall layer.
 *
 * @par Pipe integration
 * Pipes bypass the filesystem driver: their ::vfs_oft_entry_t carries a
 * non-NULL @c pipe pointer and a ::VFS_KIND_PIPE_RD or ::VFS_KIND_PIPE_WR
 * kind.  The @c ops and @c handle fields are @c NULL for pipe entries.
 *
 * @par Mount resolution
 * Path lookup performs longest-prefix matching over the mount table.  All
 * paths are normalised before any driver call — consecutive slashes collapsed,
 * @c . and @c .. resolved — so drivers always receive a clean relative path.
 *
 * @par Working directory
 * The current working directory is stored per-process in @c proc_t::cwd.
 * Relative paths supplied to any VFS function are anchored there.
 */

#ifndef ALCOR2_VFS_H
#define ALCOR2_VFS_H

#include <alcor2/types.h>

/** @brief Maximum filename component length, not including the NUL byte. */
#define VFS_NAME_MAX 64
/** @brief Maximum absolute path length, including the NUL byte. */
#define VFS_PATH_MAX 256
/** @brief Maximum open file descriptors per process. */
#define VFS_MAX_FD 256

/** @name Node types
 *
 * Stored in ::vfs_stat_t::type.  Drivers set this field in every stat result;
 * the VFS translates it to POSIX @c st_mode bits before copying to userspace.
 * @{
 */
#define VFS_FILE      1 /**< Regular file. */
#define VFS_DIRECTORY 2 /**< Directory. */
#define VFS_FIFO      3 /**< Pipe / FIFO — returned by ::vfs_fstat on pipe fds. */
/** @} */

/**
 * @brief Opaque handle returned by a filesystem driver's @c open callback.
 *
 * The VFS stores this in ::vfs_oft_entry_t::handle and passes it verbatim to
 * every subsequent driver call.  The driver owns the pointed-to memory.
 */
typedef void *fs_handle_t;

/** @brief Magic @c st_dev value reported for all ramfs inodes. */
#define VFS_RAMFS_ST_DEV 0x726D667300000001ULL

/**
 * @brief Metadata returned by stat operations.
 *
 * This is the VFS-internal stat buffer.  Syscall code translates it into the
 * POSIX @c stat layout before copying to userspace.
 */
typedef struct
{
  u64 size;     /**< File size in bytes; 0 for directories. */
  u8  type;     /**< Node type: ::VFS_FILE, ::VFS_DIRECTORY, or ::VFS_FIFO. */
  u64 created;  /**< Creation timestamp (driver-defined units). */
  u64 modified; /**< Last-modified timestamp (driver-defined units). */
  u64 dev;      /**< Device ID — mount index or a per-driver magic value. */
  u64 ino;      /**< Inode number; unique within a mounted volume. */
} vfs_stat_t;

/**
 * @brief Filesystem driver operations table.
 *
 * A filesystem driver fills this struct and registers a ::fs_type_t that
 * references it.  Callbacks that are not supported may be left @c NULL ;
 * the VFS checks before invoking them.
 */
typedef struct
{
  /**
   * @brief Open or create a file at @p path relative to the mount root.
   *
   * @param fs_data  Volume-private pointer returned by the @c mount callback.
   * @param path     Mount-relative path; always starts with @c / .
   * @param flags    Combination of @c O_RDONLY, @c O_WRONLY, @c O_CREAT, etc.
   * @return Opaque handle on success, @c NULL on failure.
   */
  fs_handle_t (*open)(void *fs_data, const char *path, u32 flags);

  /**
   * @brief Release resources held by @p fh.  Must not block.
   * @param fh  Handle returned by @c open; never @c NULL.
   */
  void (*close)(fs_handle_t fh);

  /**
   * @brief Read up to @p count bytes from @p fh starting at @p offset.
   * @return Bytes read (0 = EOF), or negative @c -errno.
   */
  i64 (*read)(fs_handle_t fh, void *buf, u64 count, u64 offset);

  /**
   * @brief Write up to @p count bytes to @p fh starting at @p offset.
   * @return Bytes written, or negative @c -errno.
   */
  i64 (*write)(fs_handle_t fh, const void *buf, u64 count, u64 offset);

  /**
   * @brief Create a directory at @p path.
   * @return 0 on success, negative @c -errno on failure.
   */
  i64 (*mkdir)(void *fs_data, const char *path);

  /**
   * @brief Delete the file at @p path (must not be a directory).
   * @return 0 on success, negative @c -errno on failure.
   */
  i64 (*unlink)(void *fs_data, const char *path);

  /**
   * @brief Remove the empty directory at @p path.
   * @return 0 on success, @c -ENOTEMPTY if not empty, negative @c -errno.
   */
  i64 (*rmdir)(void *fs_data, const char *path);

  /**
   * @brief Stat the node at @p path.
   * @return 0 and populates @p st on success, negative @c -errno on failure.
   */
  i64 (*stat)(void *fs_data, const char *path, vfs_stat_t *st);

  /**
   * @brief Stat an already-open handle.
   * @return 0 and populates @p st on success, negative @c -errno on failure.
   */
  i64 (*fstat)(fs_handle_t fh, vfs_stat_t *st);

  /**
   * @brief Read the directory entry at zero-based position @p index.
   *
   * @param fh    Open directory handle.
   * @param index Zero-based entry index; the VFS tracks this in the OFT offset.
   * @param name  Caller buffer of at least ::VFS_NAME_MAX + 1 bytes.
   * @param st    Filled with entry metadata; may be @c NULL.
   * @return 1 if an entry was produced, 0 at end-of-directory, negative @c
   * -errno.
   */
  i64 (*readdir)(fs_handle_t fh, u64 index, char *name, vfs_stat_t *st);

  /**
   * @brief Truncate @p fh to exactly @p length bytes.
   * @return 0 on success, negative @c -errno on failure.
   */
  i64 (*truncate)(fs_handle_t fh, u64 length);

  /**
   * @brief Read the target of the symbolic link at @p path.
   *
   * The result is NOT NUL-terminated; the caller uses the returned length.
   *
   * @param buf  Destination buffer.
   * @param cap  Capacity of @p buf in bytes.
   * @return Target length on success, negative @c -errno on failure.
   */
  i64 (*readlink)(void *fs_data, const char *path, char *buf, u64 cap);
} fs_ops_t;

/**
 * @brief Filesystem type descriptor — registered once, instantiated per mount.
 */
typedef struct
{
  const char     *name; /**< Type name used by ::vfs_mount, e.g. @c "ext2". */
  const fs_ops_t *ops;  /**< Static operations table shared by all instances. */

  /**
   * @brief Mount a volume and return its private state.
   *
   * Called by ::vfs_mount.  The returned pointer becomes @c fs_data and is
   * forwarded to every ::fs_ops_t callback for this mount.
   *
   * @param source  Device path or special string (driver-defined).
   * @param flags   Mount flags (currently always 0).
   * @return Volume-private data pointer on success, @c NULL on failure.
   */
  void *(*mount)(const char *source, u32 flags);

  /**
   * @brief Unmount the volume and release all driver resources.
   * @param fs_data  Pointer returned by @c mount.
   */
  void (*unmount)(void *fs_data);
} fs_type_t;

/** @name Open flags (POSIX-compatible subset)
 * @{ */
#define O_RDONLY 0x0000 /**< Open for reading only. */
#define O_WRONLY 0x0001 /**< Open for writing only. */
#define O_RDWR   0x0002 /**< Open for reading and writing. */
#define O_CREAT  0x0040 /**< Create the file if it does not exist. */
#define O_TRUNC  0x0200 /**< Truncate to zero length on open. */
#define O_APPEND 0x0400 /**< All writes advance to end-of-file first. */
#define O_DIRECTORY                                                            \
  0x10000                 /**< Fail if path does not resolve to a directory. */
#define O_CLOEXEC 0x80000 /**< Close this fd automatically on @c execve. */
/** @} */

/** @name Seek origins (POSIX)
 * @{ */
#define SEEK_SET 0 /**< Offset is relative to the beginning of the file. */
#define SEEK_CUR 1 /**< Offset is relative to the current file position. */
#define SEEK_END 2 /**< Offset is relative to the end of the file. */
/** @} */

/** @name Directory entry type codes (@c d_type field)
 * @{ */
#define DT_UNKNOWN 0 /**< File type could not be determined. */
#define DT_DIR     4 /**< Directory. */
#define DT_REG     8 /**< Regular file. */
/** @} */

/**
 * @brief POSIX directory entry returned by @c getdents(2).
 *
 * Variable-length; @c d_name immediately follows the fixed fields.
 * @c d_reclen is rounded up to an 8-byte boundary.
 */
typedef struct
{
  u64 d_ino;    /**< Inode number. */
  i64 d_off;    /**< Opaque offset hint to the next entry. */
  u16 d_reclen; /**< Total record size including @c d_name and alignment pad. */
  u8  d_type;   /**< Entry type: ::DT_DIR, ::DT_REG, or ::DT_UNKNOWN. */
  char d_name[]; /**< NUL-terminated filename. */
} PACKED dirent_t;

/**
 * @brief VFS-level Open File Description.
 *
 * One entry exists for the lifetime of an open "file description".  It is
 * shared across descriptors created with @c dup and across @c fork ; the
 * reference count tracks how many per-process fd slots point here.
 *
 * @note For pipe entries, @c ops and @c handle are @c NULL.  Use @c pipe and
 *       @c kind to distinguish direction.
 */
typedef struct
{
  fs_handle_t     handle; /**< Driver handle; @c NULL for pipes. */
  const fs_ops_t *ops;    /**< Driver operations; @c NULL for pipes. */
  void           *pipe;   /**< Opaque pipe object; @c NULL for regular files. */
  u64             offset; /**< Current byte offset (or entry index for dirs). */
  u32             flags;  /**< Open flags: @c O_RDONLY, @c O_APPEND, etc. */
  i32             kind;   /**< ::VFS_KIND_FILE, ::VFS_KIND_PIPE_RD, or
                               ::VFS_KIND_PIPE_WR. */
  i32  refcount;          /**< Number of fd slots sharing this description. */
  u64  st_dev;            /**< Cached device ID (used by @c fstat). */
  bool in_use;            /**< @c true when this slot is allocated. */
} vfs_oft_entry_t;

/** @name OFT kind — also used as pipe-direction argument to @c pipe_oft_release
 * @{ */
#define VFS_KIND_FILE    0 /**< Regular file. */
#define VFS_KIND_PIPE_RD 1 /**< Pipe, read end. */
#define VFS_KIND_PIPE_WR 2 /**< Pipe, write end. */
/** @} */

/**
 * @brief Initialise the VFS layer.
 *
 * Zeroes the mount table and the Open File Table.  Must be called exactly
 * once during kernel boot, before any other VFS function.
 */
void vfs_init(void);

/**
 * @brief Register a filesystem driver.
 *
 * @p fstype must remain valid for the lifetime of the kernel (use a static).
 *
 * @param fstype  Filesystem type descriptor to register.
 * @return 0 on success, @c -ENOMEM if the driver registry is full.
 */
i64 vfs_register_fs(const fs_type_t *fstype);

/**
 * @brief Mount @p source at @p target using the named filesystem driver.
 *
 * @param source  Device identifier forwarded to the driver's @c mount callback.
 * @param target  Absolute mount-point path (normalised internally).
 * @param fstype  Name of a previously registered filesystem type.
 * @return 0 on success.
 * @retval -ENODEV  No driver is registered under @p fstype.
 * @retval -ENOMEM  The mount table is full.
 * @retval -EINVAL  The driver's @c mount callback returned @c NULL.
 */
i64 vfs_mount(const char *source, const char *target, const char *fstype);

/**
 * @brief Unmount the filesystem mounted at @p target.
 *
 * Calls the driver's @c unmount callback if provided.
 *
 * @param target  Absolute mount-point path.
 * @return 0 on success, @c -ENOENT if no mount is found at @p target.
 */
i64 vfs_umount(const char *target);

/**
 * @brief Open or create a file and install a file descriptor.
 *
 * Resolves @p path (relative paths are anchored to the calling process's CWD),
 * finds the responsible mount via longest-prefix matching, and calls the
 * driver's @c open.  The resulting fd is the lowest available slot ≥ 3.
 *
 * @param path   Absolute or CWD-relative path.
 * @param flags  Open flags: @c O_RDONLY, @c O_WRONLY, @c O_CREAT, etc.
 * @return New file descriptor (≥ 3) on success, or negative @c -errno.
 */
i64 vfs_open(const char *path, u32 flags);

/**
 * @brief Release a file descriptor.
 *
 * Decrements the OFT refcount.  When the count reaches zero the entry is torn
 * down: the driver's @c close is called for file entries; ::pipe_oft_release
 * is called for pipe entries.
 *
 * @param fd  File descriptor to close.
 * @return 0 on success, @c -EBADF if @p fd is not open.
 */
i64 vfs_close(i64 fd);

/**
 * @brief Read up to @p count bytes from @p fd into @p buf.
 *
 * For pipe read-ends, blocks until data is available or the write end closes.
 * The file offset is advanced by the number of bytes read.
 *
 * @return Bytes read (0 = EOF or write-end closed), or negative @c -errno.
 */
i64 vfs_read(i64 fd, void *buf, u64 count);

/**
 * @brief Write @p count bytes from @p buf to @p fd.
 *
 * If @c O_APPEND is set, the write position is moved to end-of-file before
 * each call.  For pipe write-ends, blocks when the ring buffer is full.
 *
 * @return Bytes written, or negative @c -errno.  Writing to a pipe whose read
 *         end is closed returns @c -EPIPE.
 */
i64 vfs_write(i64 fd, const void *buf, u64 count);

/**
 * @brief Reposition the file offset of @p fd.
 *
 * @param fd      Open file descriptor.
 * @param offset  Signed byte displacement.
 * @param whence  ::SEEK_SET, ::SEEK_CUR, or ::SEEK_END.
 * @return New absolute byte offset on success.
 * @retval -ESPIPE  @p fd refers to a pipe.
 * @retval -EINVAL  Invalid @p whence, or the resulting offset would be
 * negative.
 */
i64 vfs_seek(i64 fd, i64 offset, i32 whence);

/**
 * @brief Stat the node at @p path.
 * @return 0 on success, @c -ENOENT if the path cannot be resolved.
 */
i64 vfs_stat(const char *path, vfs_stat_t *st);

/**
 * @brief Stat an open file descriptor.
 *
 * For pipe fds, returns a synthetic ::vfs_stat_t with @c type = ::VFS_FIFO
 * and all other fields zeroed.
 *
 * @return 0 on success, @c -EBADF if @p fd is invalid.
 */
i64 vfs_fstat(i64 fd, vfs_stat_t *st);

/**
 * @brief Create a directory at @p path.
 * @return 0 on success, or negative @c -errno.
 */
i64 vfs_mkdir(const char *path);

/**
 * @brief Delete the file at @p path (must not be a directory).
 * @return 0 on success, or negative @c -errno.
 */
i64 vfs_unlink(const char *path);

/**
 * @brief Remove the empty directory at @p path.
 * @return 0 on success, @c -ENOTEMPTY if not empty, or negative @c -errno.
 */
i64 vfs_rmdir(const char *path);

/**
 * @brief Fill @p buf with @c dirent64 entries from an open directory.
 *
 * Reads as many complete entries as fit within @p count bytes, advancing the
 * internal directory position on each call.  Returns 0 when exhausted.
 *
 * @param fd     File descriptor for an open directory.
 * @param buf    Output buffer aligned to 8 bytes.
 * @param count  Buffer size in bytes (must be at least 32).
 * @return Total bytes written, 0 at end-of-directory, or negative @c -errno.
 */
i64 vfs_getdents(i64 fd, void *buf, u64 count);

/**
 * @brief Truncate @p fd to exactly @p length bytes.
 * @return 0 on success, @c -EINVAL if @p fd is a pipe, or negative @c -errno.
 */
i64 vfs_ftruncate(i64 fd, u64 length);

/**
 * @brief Read the target of the symbolic link at @p path.
 *
 * The result placed in @p buf is NOT NUL-terminated; use the returned length.
 *
 * @param path  Path to the symlink itself.
 * @param buf   Output buffer.
 * @param cap   Capacity of @p buf in bytes.
 * @return Target length on success, or negative @c -errno.
 */
i64 vfs_readlink(const char *path, char *buf, u64 cap);

/**
 * @brief Rename (move) a file, copying across mount points if necessary.
 *
 * Implemented as a copy-and-unlink when @p oldpath and @p newpath reside on
 * different mounts.  The operation is NOT atomic.  Directories are not
 * supported.  Files larger than 16 MiB return @c -ENOSYS.
 *
 * @param oldpath  Source path.
 * @param newpath  Destination path (created or overwritten).
 * @return 0 on success, or negative @c -errno.
 */
i64 vfs_rename(const char *oldpath, const char *newpath);

/**
 * @brief Return the open flags stored in the OFT for @p fd.
 * @return Flags on success, @c -EBADF if @p fd is not open.
 */
i64 vfs_get_flags(i64 fd);

/**
 * @brief Overwrite the open flags for @p fd (used by @c fcntl @c F_SETFL).
 * @return 0 on success, @c -EBADF if @p fd is not open.
 */
i64 vfs_set_flags(i64 fd, u32 flags);

/**
 * @brief Return a pointer to the calling process's current working directory.
 *
 * The pointer addresses storage inside the current @c proc_t and remains
 * valid until the next ::vfs_chdir call.  Callers must not modify the string.
 * Returns @c "/" when no process is running.
 */
const char *vfs_getcwd(void);

/**
 * @brief Change the calling process's current working directory to @p path.
 * @return 0 on success, @c -ENOTDIR if @p path does not name a directory.
 */
i64 vfs_chdir(const char *path);

/**
 * @brief Initialise a per-process fd array to the "all closed" state.
 * @param fds  Array of ::VFS_MAX_FD @c i32 values to initialise.
 */
void vfs_proc_init_fds(i32 *fds);

/**
 * @brief Inherit parent's fd table into child's after @c fork.
 *
 * Copies every open fd slot and calls ::vfs_oft_retain for each, so parent
 * and child hold independent references to the same OFT entries.
 *
 * @param child_fds   Destination fd array (written).
 * @param child_clox  Destination cloexec bitmap (written).
 * @param parent_fds  Source fd array (read-only).
 * @param parent_clox Source cloexec bitmap (read-only).
 */
void vfs_proc_inherit_fds(
    i32 *child_fds, u8 *child_clox, const i32 *parent_fds, const u8 *parent_clox
);

/**
 * @brief Release all file descriptors held by an exiting process.
 *
 * Calls ::vfs_oft_release for every open slot.  Entries whose refcount reaches
 * zero are torn down.  All slots in @p fds are set to -1 on return.
 *
 * @param fds  Per-process fd array to drain.
 */
void vfs_proc_release_fds(i32 *fds);

/**
 * @brief Close every fd in the calling process that has @c FD_CLOEXEC set.
 *
 * Called by the @c execve path immediately after the new image is loaded.
 */
void vfs_proc_close_cloexec_fds(void);

/**
 * @brief Duplicate @p oldfd, placing the clone at the lowest free fd ≥ 3.
 * @return New file descriptor on success, @c -EBADF if @p oldfd is invalid.
 */
i64 vfs_dup(i64 oldfd);

/**
 * @brief Duplicate @p oldfd into the specific slot @p newfd.
 *
 * If @p newfd is already open it is silently closed first.  If @p oldfd
 * equals @p newfd the call is a no-op.
 *
 * @return @p newfd on success, @c -EBADF on error.
 */
i64 vfs_dup2(i64 oldfd, i64 newfd);

/**
 * @brief Check whether @p fd has data available without blocking.
 * @return Positive if ready, 0 if not ready, negative @c -errno on error.
 */
i32 vfs_select_read_ready(i64 fd);

/**
 * @brief Check whether @p fd can accept a write without blocking.
 * @return Positive if ready, 0 if not ready, negative @c -errno on error.
 */
i32 vfs_select_write_ready(i64 fd);

/** @return @c true if @p fd refers to either end of a pipe. */
bool vfs_fd_is_pipe(u64 fd);

/** @return @c true if @p fd is currently open in the calling process. */
bool vfs_fd_is_valid(i64 fd);

/**
 * @brief Install a new fd pointing to an existing OFT slot.
 *
 * Searches for the lowest free fd ≥ 3 in the calling process.  Does NOT
 * increment the OFT refcount — the caller is responsible.
 *
 * @param oft_idx  Index of the target OFT slot.
 * @return New fd on success, @c -EMFILE if the process table is full.
 */
i64 vfs_install_fd(i32 oft_idx);

/**
 * @brief Allocate an OFT entry for one end of a pipe.
 *
 * @param kind  ::VFS_KIND_PIPE_RD or ::VFS_KIND_PIPE_WR.
 * @param pipe  Opaque pipe object returned by @c pipe_alloc_obj.
 * @return OFT index on success, negative @c -errno on failure.
 */
i32 vfs_oft_alloc_pipe(i32 kind, void *pipe);

/**
 * @brief Increment the refcount of OFT slot @p idx.
 *
 * Call when a new fd is made to share an existing file description — after
 * @c fork (via ::vfs_proc_inherit_fds) or after @c dup.
 *
 * @param idx  OFT slot index; silently ignored if out of range or not in use.
 */
void vfs_oft_retain(i32 idx);

/**
 * @brief Decrement the refcount of OFT slot @p idx.
 *
 * When the count reaches zero the entry is torn down: the driver's @c close
 * callback is invoked for file entries; ::pipe_oft_release is called for pipe
 * entries with the stored kind.
 *
 * @param idx  OFT slot index; silently ignored if out of range or not in use.
 */
void vfs_oft_release(i32 idx);

#endif /* ALCOR2_VFS_H */
