/**
 * @file include/alcor2/ext2.h
 * @brief ext2 filesystem driver.
 *
 * Read/write support for ext2 volumes on ATA devices.
 */

#ifndef ALCOR2_EXT2_H
#define ALCOR2_EXT2_H

#include <alcor2/types.h>

/** @brief ext2 magic number in superblock. */
#define EXT2_MAGIC 0xEF53

/** @brief Default block size (1KB). */
#define EXT2_MIN_BLOCK_SIZE 1024

/** @brief Sector size for disk I/O. */
#define EXT2_SECTOR_SIZE 512

/** @brief Maximum filename length. */
#define EXT2_NAME_MAX 255

/** @brief Root inode number (always 2 in ext2). */
#define EXT2_ROOT_INODE 2

/** @brief Number of direct block pointers in inode. */
#define EXT2_NDIR_BLOCKS 12

/** @brief Indirect block index. */
#define EXT2_IND_BLOCK 12

/** @brief Double indirect block index. */
#define EXT2_DIND_BLOCK 13

/** @brief Triple indirect block index. */
#define EXT2_TIND_BLOCK 14

/** @brief Total block pointers in inode. */
#define EXT2_N_BLOCKS 15

/** @name Inode file types (stored in i_mode)
 * @{ */
#define EXT2_S_IFSOCK 0xC000 /**< Socket */
#define EXT2_S_IFLNK  0xA000 /**< Symbolic link */
#define EXT2_S_IFREG  0x8000 /**< Regular file */
#define EXT2_S_IFBLK  0x6000 /**< Block device */
#define EXT2_S_IFDIR  0x4000 /**< Directory */
#define EXT2_S_IFCHR  0x2000 /**< Character device */
#define EXT2_S_IFIFO  0x1000 /**< FIFO */
#define EXT2_S_IFMT   0xF000 /**< Format mask */
/** @} */

/** @name Inode permission bits
 * @{ */
#define EXT2_S_ISUID 0x0800 /**< Set user ID */
#define EXT2_S_ISGID 0x0400 /**< Set group ID */
#define EXT2_S_ISVTX 0x0200 /**< Sticky bit */
#define EXT2_S_IRWXU 0x01C0 /**< User rwx */
#define EXT2_S_IRUSR 0x0100 /**< User read */
#define EXT2_S_IWUSR 0x0080 /**< User write */
#define EXT2_S_IXUSR 0x0040 /**< User execute */
#define EXT2_S_IRWXG 0x0038 /**< Group rwx */
#define EXT2_S_IRGRP 0x0020 /**< Group read */
#define EXT2_S_IWGRP 0x0010 /**< Group write */
#define EXT2_S_IXGRP 0x0008 /**< Group execute */
#define EXT2_S_IRWXO 0x0007 /**< Others rwx */
#define EXT2_S_IROTH 0x0004 /**< Others read */
#define EXT2_S_IWOTH 0x0002 /**< Others write */
#define EXT2_S_IXOTH 0x0001 /**< Others execute */
/** @} */

/** @name Directory entry file types
 * @{ */
#define EXT2_FT_UNKNOWN  0
#define EXT2_FT_REG_FILE 1
#define EXT2_FT_DIR      2
#define EXT2_FT_CHRDEV   3
#define EXT2_FT_BLKDEV   4
#define EXT2_FT_FIFO     5
#define EXT2_FT_SOCK     6
#define EXT2_FT_SYMLINK  7
/** @} */

/**
 * @brief ext2 superblock structure.
 *
 * Located at byte 1024 from the start of the partition.
 */
typedef struct
{
  u32 s_inodes_count;      /**< Total number of inodes */
  u32 s_blocks_count;      /**< Total number of blocks */
  u32 s_r_blocks_count;    /**< Reserved blocks for superuser */
  u32 s_free_blocks_count; /**< Free blocks count */
  u32 s_free_inodes_count; /**< Free inodes count */
  u32 s_first_data_block;  /**< First data block (0 or 1) */
  u32 s_log_block_size;    /**< Block size = 1024 << s_log_block_size */
  u32 s_log_frag_size;     /**< Fragment size (obsolete) */
  u32 s_blocks_per_group;  /**< Blocks per group */
  u32 s_frags_per_group;   /**< Fragments per group (obsolete) */
  u32 s_inodes_per_group;  /**< Inodes per group */
  u32 s_mtime;             /**< Last mount time */
  u32 s_wtime;             /**< Last write time */
  u16 s_mnt_count;         /**< Mount count since last fsck */
  u16 s_max_mnt_count;     /**< Max mounts before fsck */
  u16 s_magic;             /**< Magic number (0xEF53) */
  u16 s_state;             /**< Filesystem state */
  u16 s_errors;            /**< Behavior on error */
  u16 s_minor_rev_level;   /**< Minor revision level */
  u32 s_lastcheck;         /**< Last fsck time */
  u32 s_checkinterval;     /**< Max time between fsck */
  u32 s_creator_os;        /**< Creator OS */
  u32 s_rev_level;         /**< Revision level */
  u16 s_def_resuid;        /**< Default UID for reserved blocks */
  u16 s_def_resgid;        /**< Default GID for reserved blocks */
  /* Extended superblock fields (rev 1) */
  u32 s_first_ino;         /**< First non-reserved inode */
  u16 s_inode_size;        /**< Inode size */
  u16 s_block_group_nr;    /**< Block group of this superblock */
  u32 s_feature_compat;    /**< Compatible feature set */
  u32 s_feature_incompat;  /**< Incompatible feature set */
  u32 s_feature_ro_compat; /**< Read-only compatible feature set */
  u8  s_uuid[16];          /**< Volume UUID */
  u8  s_volume_name[16];   /**< Volume name */
  u8  s_last_mounted[64];  /**< Last mounted path */
  u32 s_algo_bitmap;       /**< Compression algorithm bitmap */
  /* Performance hints */
  u8  s_prealloc_blocks;     /**< Blocks to preallocate for files */
  u8  s_prealloc_dir_blocks; /**< Blocks to preallocate for dirs */
  u16 s_padding1;
  /* Journaling (ext3/4) - not used in ext2 */
  u8  s_journal_uuid[16];
  u32 s_journal_inum;
  u32 s_journal_dev;
  u32 s_last_orphan;
  /* Directory indexing (not used) */
  u32 s_hash_seed[4];
  u8  s_def_hash_version;
  u8  s_reserved_char_pad;
  u16 s_reserved_word_pad;
  u32 s_default_mount_opts;
  u32 s_first_meta_bg;
  u8  s_reserved[760]; /**< Padding to 1024 bytes */
} PACKED ext2_superblock_t;

/**
 * @brief Block group descriptor.
 *
 * Describes one block group in the filesystem.
 */
typedef struct
{
  u32 bg_block_bitmap;      /**< Block bitmap block number */
  u32 bg_inode_bitmap;      /**< Inode bitmap block number */
  u32 bg_inode_table;       /**< First inode table block */
  u16 bg_free_blocks_count; /**< Free blocks in group */
  u16 bg_free_inodes_count; /**< Free inodes in group */
  u16 bg_used_dirs_count;   /**< Directories in group */
  u16 bg_pad;
  u8  bg_reserved[12];
} PACKED ext2_group_desc_t;

/**
 * @brief ext2 inode structure.
 *
 * Contains file metadata and block pointers.
 */
typedef struct
{
  u16 i_mode;                 /**< File mode (type and permissions) */
  u16 i_uid;                  /**< Owner UID */
  u32 i_size;                 /**< File size in bytes (lower 32 bits) */
  u32 i_atime;                /**< Access time */
  u32 i_ctime;                /**< Creation time */
  u32 i_mtime;                /**< Modification time */
  u32 i_dtime;                /**< Deletion time */
  u16 i_gid;                  /**< Group ID */
  u16 i_links_count;          /**< Hard link count */
  u32 i_blocks;               /**< 512-byte blocks allocated */
  u32 i_flags;                /**< File flags */
  u32 i_osd1;                 /**< OS dependent value 1 */
  u32 i_block[EXT2_N_BLOCKS]; /**< Block pointers */
  u32 i_generation;           /**< File version (NFS) */
  u32 i_file_acl;             /**< File ACL block */
  u32 i_dir_acl;              /**< Directory ACL / size_high */
  u32 i_faddr;                /**< Fragment address (obsolete) */
  u8  i_osd2[12];             /**< OS dependent value 2 */
} PACKED ext2_inode_t;

/**
 * @brief ext2 directory entry structure.
 *
 * Variable-length entry in directory blocks.
 */
typedef struct
{
  u32  inode;     /**< Inode number (0 = unused) */
  u16  rec_len;   /**< Entry length (to next entry) */
  u8   name_len;  /**< Name length */
  u8   file_type; /**< File type (EXT2_FT_*) */
  char name[];    /**< Filename (not null-terminated) */
} PACKED ext2_dirent_t;

/**
 * @brief ext2 volume descriptor.
 */
typedef struct
{
  u8                 drive;            /**< ATA drive index */
  u32                partition_lba;    /**< Partition start sector */
  u32                block_size;       /**< Block size in bytes */
  u32                blocks_per_group; /**< Blocks per group */
  u32                inodes_per_group; /**< Inodes per group */
  u32                inode_size;       /**< Inode structure size */
  u32                groups_count;     /**< Number of block groups */
  u32                inodes_count;     /**< Total inodes */
  u32                blocks_count;     /**< Total blocks */
  u32                first_data_block; /**< First data block number */
  bool               mounted;          /**< Volume is mounted */
  ext2_superblock_t  sb;               /**< Cached superblock */
  ext2_group_desc_t *groups;           /**< Group descriptor table */
} ext2_volume_t;

/**
 * @brief ext2 file handle.
 */
typedef struct
{
  ext2_volume_t *vol;          /**< Volume reference */
  u32            inode_num;    /**< Inode number */
  ext2_inode_t   inode;        /**< Cached inode */
  u32            position;     /**< Current read/write position */
  u32            block_offset; /**< Offset within current block */
  bool           is_dir;       /**< Is a directory */
  bool           in_use;       /**< Handle is in use */
  bool           dirty;        /**< Inode modified */
} ext2_file_t;

/**
 * @brief ext2 directory entry (for readdir).
 */
typedef struct
{
  char name[EXT2_NAME_MAX + 1]; /**< Null-terminated filename */
  u32  inode;                   /**< Inode number */
  u8   file_type;               /**< File type (EXT2_FT_*) */
  u32  size;                    /**< File size */
} ext2_entry_t;

/**
 * @brief Initialize ext2 driver.
 */
void ext2_init(void);

/**
 * @brief Mount an ext2 volume.
 * @param drive ATA drive index.
 * @param partition_lba Start of partition (0 for whole disk).
 * @return Volume pointer, or NULL on error.
 */
ext2_volume_t *ext2_mount(u8 drive, u32 partition_lba);

/**
 * @brief Unmount an ext2 volume.
 * @param vol Volume to unmount.
 */
void ext2_unmount(ext2_volume_t *vol);

/**
 * @brief Open a file or directory.
 * @param vol Volume to search.
 * @param path Path relative to root.
 * @return File handle, or NULL on error.
 */
ext2_file_t *ext2_open(ext2_volume_t *vol, const char *path);

/**
 * @brief Close a file handle.
 * @param file File handle to close.
 */
void ext2_close(ext2_file_t *file);

/**
 * @brief Read from file.
 * @param file File handle.
 * @param buf Destination buffer.
 * @param count Bytes to read.
 * @return Bytes read, or negative on error.
 */
i64 ext2_read(ext2_file_t *file, void *buf, u64 count);

/**
 * @brief Write data to a file.
 * @param file File handle.
 * @param buf Source buffer.
 * @param count Number of bytes to write.
 * @return Bytes written, or negative on error.
 */
i64 ext2_write(ext2_file_t *file, const void *buf, u64 count);

/**
 * @brief Read next directory entry.
 * @param dir Directory handle.
 * @param entry Output entry.
 * @return 1 if entry read, 0 if end, negative on error.
 */
i64 ext2_readdir(ext2_file_t *dir, ext2_entry_t *entry);

/**
 * @brief Get file/directory info.
 * @param vol Volume to search.
 * @param path Path to file/directory.
 * @param entry Output entry.
 * @return 0 on success, negative on error.
 */
i64 ext2_stat(const ext2_volume_t *vol, const char *path, ext2_entry_t *entry);

/**
 * @brief Seek in a file.
 * @param file File handle.
 * @param offset Seek offset.
 * @param whence SEEK_SET, SEEK_CUR, or SEEK_END.
 * @return New position, or negative on error.
 */
i64 ext2_seek(ext2_file_t *file, i64 offset, i32 whence);

/**
 * @brief Create a new file.
 * @param vol Volume to create file on.
 * @param path Path to the new file.
 * @return File handle, or NULL on error.
 */
ext2_file_t *ext2_create(ext2_volume_t *vol, const char *path);

/**
 * @brief Create a new directory.
 * @param vol Volume to create directory on.
 * @param path Path to the new directory.
 * @return 0 on success, negative on error.
 */
i64 ext2_mkdir(ext2_volume_t *vol, const char *path);

/**
 * @brief Truncate a file to zero length.
 * @param file File handle.
 * @return 0 on success, negative on error.
 */
i64 ext2_truncate(ext2_file_t *file);

/**
 * @brief Flush file changes to disk.
 * @param file File handle.
 * @return 0 on success, negative on error.
 */
i64 ext2_flush(ext2_file_t *file);

/**
 * @brief Delete a file from the filesystem.
 * @param vol Volume containing the file.
 * @param path Path to the file to delete.
 * @return 0 on success, negative on error.
 */
i64 ext2_unlink(ext2_volume_t *vol, const char *path);

/**
 * @brief Remove an empty directory.
 * @param vol Volume containing the directory.
 * @param path Path to the directory to remove.
 * @return 0 on success, negative on error.
 */
i64 ext2_rmdir(ext2_volume_t *vol, const char *path);

/* Forward declaration for VFS integration */
struct fs_ops;

/**
 * @brief Get ext2 VFS operations table.
 *
 * Returns a pointer to the static fs_ops_t structure that implements
 * ext2 filesystem operations for the VFS layer.
 *
 * @return Pointer to ext2 fs_ops_t structure.
 */
const struct fs_ops *ext2_get_ops(void);

/**
 * @brief Initialize and register ext2 filesystem with VFS.
 *
 * Must be called after vfs_init() but before vfs_mount() for ext2.
 */
void ext2_init(void);

#endif
