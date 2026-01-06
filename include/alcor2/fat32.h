/**
 * @file include/alcor2/fat32.h
 * @brief FAT32 filesystem driver.
 *
 * Read/write support for FAT32 volumes on ATA devices.
 */

#ifndef ALCOR2_FAT32_H
#define ALCOR2_FAT32_H

#include <alcor2/types.h>

/** @brief Sector size for FAT32. */
#define FAT32_SECTOR_SIZE 512

/** @brief Size of a directory entry on disk. */
#define FAT32_DIR_ENTRY_SIZE 32

/** @brief Maximum filename length (long filenames). */
#define FAT32_NAME_MAX 255

/** @name FAT directory entry attributes
 * @{ */
#define FAT_ATTR_READ_ONLY 0x01
#define FAT_ATTR_HIDDEN    0x02
#define FAT_ATTR_SYSTEM    0x04
#define FAT_ATTR_VOLUME_ID 0x08
#define FAT_ATTR_DIRECTORY 0x10
#define FAT_ATTR_ARCHIVE   0x20
#define FAT_ATTR_LFN       0x0F
/** @} */

/** @name Special cluster values
 * @{ */
#define FAT32_CLUSTER_FREE 0x00000000
#define FAT32_CLUSTER_BAD  0x0FFFFFF7
#define FAT32_CLUSTER_END  0x0FFFFFF8
/** @} */

/**
 * @brief BIOS Parameter Block at offset 0 in boot sector.
 */
typedef struct
{
  u8  jmp[3];
  u8  oem[8];
  u16 bytes_per_sector;
  u8  sectors_per_cluster;
  u16 reserved_sectors;
  u8  fat_count;
  u16 root_entries;
  u16 total_sectors_16;
  u8  media_type;
  u16 fat_size_16;
  u16 sectors_per_track;
  u16 heads;
  u32 hidden_sectors;
  u32 total_sectors_32;
  u32 fat_size_32;
  u16 ext_flags;
  u16 fs_version;
  u32 root_cluster;
  u16 fs_info_sector;
  u16 backup_boot;
  u8  reserved[12];
  u8  drive_number;
  u8  reserved1;
  u8  boot_signature;
  u32 volume_id;
  u8  volume_label[11];
  u8  fs_type[8];
} PACKED fat32_bpb_t;

/**
 * @brief Standard 8.3 directory entry (32 bytes).
 */
typedef struct
{
  u8  name[11];
  u8  attr;
  u8  nt_reserved;
  u8  create_time_tenth;
  u16 create_time;
  u16 create_date;
  u16 access_date;
  u16 cluster_high;
  u16 modify_time;
  u16 modify_date;
  u16 cluster_low;
  u32 file_size;
} PACKED fat32_dirent_t;

/**
 * @brief Long filename entry (32 bytes).
 */
typedef struct
{
  u8  order;
  u16 name1[5];
  u8  attr;
  u8  type;
  u8  checksum;
  u16 name2[6];
  u16 reserved;
  u16 name3[2];
} PACKED fat32_lfn_t;

/**
 * @brief FAT32 volume descriptor.
 */
typedef struct
{
  u8   drive;
  u32  partition_lba;
  u32  fat_start;
  u32  data_start;
  u32  root_cluster;
  u32  sectors_per_cluster;
  u32  bytes_per_cluster;
  u32  fat_size;
  u32  total_clusters;
  bool mounted;
} fat32_volume_t;

/**
 * @brief FAT32 file handle.
 */
typedef struct
{
  fat32_volume_t *vol;
  u32             start_cluster;
  u32             current_cluster;
  u32             cluster_offset;
  u32             file_size;
  u32             position;
  u8              attr;
  bool            is_dir;
  bool            in_use;
  bool            dirty;           /**< File has been modified */
  u32             parent_cluster;  /**< Parent directory cluster */
  u32             dirent_offset;   /**< Offset of dirent in parent cluster */
} fat32_file_t;

/**
 * @brief FAT32 directory entry (for readdir).
 */
typedef struct
{
  char name[FAT32_NAME_MAX + 1];
  u8   attr;
  u32  size;
  u32  cluster;
} fat32_entry_t;

/**
 * @brief Initialize FAT32 driver.
 */
void fat32_init(void);

/**
 * @brief Mount a FAT32 volume.
 * @param drive ATA drive index.
 * @param partition_lba Start of partition (0 for whole disk).
 * @return Volume pointer, or NULL on error.
 */
fat32_volume_t *fat32_mount(u8 drive, u32 partition_lba);

/**
 * @brief Unmount a FAT32 volume.
 * @param vol Volume to unmount.
 */
void fat32_unmount(fat32_volume_t *vol);

/**
 * @brief Open a file or directory.
 * @param vol Volume to search.
 * @param path Path relative to root.
 * @return File handle, or NULL on error.
 */
fat32_file_t *fat32_open(fat32_volume_t *vol, const char *path);

/**
 * @brief Close a file handle.
 * @param file File handle to close.
 */
void fat32_close(fat32_file_t *file);

/**
 * @brief Read from file.
 * @param file File handle.
 * @param buf Destination buffer.
 * @param count Bytes to read.
 * @return Bytes read, or negative on error.
 */
i64 fat32_read(fat32_file_t *file, void *buf, u64 count);

/**
 * @brief Read next directory entry.
 * @param dir Directory handle.
 * @param entry Output entry.
 * @return 1 if entry read, 0 if end, negative on error.
 */
i64 fat32_readdir(fat32_file_t *dir, fat32_entry_t *entry);

/**
 * @brief Get file/directory info.
 * @param vol Volume to search.
 * @param path Path to file/directory.
 * @param entry Output entry.
 * @return 0 on success, negative on error.
 */
i64 fat32_stat(fat32_volume_t *vol, const char *path, fat32_entry_t *entry);

/**
 * @brief Write data to a file.
 * @param file File handle (must be opened for writing).
 * @param buf Source buffer.
 * @param count Number of bytes to write.
 * @return Bytes written, or negative on error.
 */
i64 fat32_write(fat32_file_t *file, const void *buf, u64 count);

/**
 * @brief Seek in a file.
 * @param file File handle.
 * @param offset Seek offset.
 * @param whence SEEK_SET, SEEK_CUR, or SEEK_END.
 * @return New position, or negative on error.
 */
i64 fat32_seek(fat32_file_t *file, i64 offset, i32 whence);

/**
 * @brief Create a new file.
 * @param vol Volume to create file on.
 * @param path Path to the new file.
 * @return File handle, or NULL on error.
 */
fat32_file_t *fat32_create(fat32_volume_t *vol, const char *path);

/**
 * @brief Truncate a file to zero length.
 * @param file File handle.
 * @return 0 on success, negative on error.
 */
i64 fat32_truncate(fat32_file_t *file);

/**
 * @brief Flush file changes to disk.
 * @param file File handle.
 * @return 0 on success, negative on error.
 */
i64 fat32_flush(fat32_file_t *file);

#endif
