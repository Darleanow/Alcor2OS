/**
 * @file src/fs/fat32.c
 * @brief FAT32 filesystem implementation.
 */

#include <alcor2/ata.h>
#include <alcor2/console.h>
#include <alcor2/errno.h>
#include <alcor2/fat32.h>
#include <alcor2/heap.h>
#include <alcor2/kstdlib.h>

#define MAX_VOLUMES       4
#define MAX_FILES         32
#define SECTOR_CACHE_SIZE 4096

static fat32_volume_t volumes[MAX_VOLUMES];
static fat32_file_t   files[MAX_FILES];
static u8             sector_cache[SECTOR_CACHE_SIZE];

/*
 * @brief Write sector to volume.
 * @param vol Volume.
 * @param sector Sector number.
 * @param buf Buffer.
 * @return 0 on success, negative on error.
 */
static i64 vol_write_sector(const fat32_volume_t *vol, u32 sector, const void *buf)
{
  return ata_write(vol->drive, vol->partition_lba + sector, 1, buf);
}

/**
 * @brief Read sector from volume.
 * @param vol Volume.
 * @param sector Sector number.
 * @param buf Buffer.
 * @return Bytes read or negative on error.
 */
static i64 vol_read_sector(const fat32_volume_t *vol, u32 sector, void *buf)
{
  return ata_read(vol->drive, vol->partition_lba + sector, 1, buf);
}

/**
 * @brief Convert cluster to first sector.
 * @param vol Volume.
 * @param cluster Cluster number.
 * @return Sector number.
 */
static u32 cluster_to_sector(const fat32_volume_t *vol, u32 cluster)
{
  return vol->data_start + (cluster - 2) * vol->sectors_per_cluster;
}

/**
 * @brief Read FAT entry for cluster.
 * @param vol Volume.
 * @param cluster Cluster number.
 * @return Next cluster or end-of-chain marker.
 */
static u32 fat_read_entry(const fat32_volume_t *vol, u32 cluster)
{
  u32 fat_offset   = cluster * 4;
  u32 fat_sector   = vol->fat_start + (fat_offset / FAT32_SECTOR_SIZE);
  u32 entry_offset = fat_offset % FAT32_SECTOR_SIZE;

  if(vol_read_sector(vol, fat_sector, sector_cache) < 0) {
    return FAT32_CLUSTER_BAD;
  }

  const u32 *entries = (const u32 *)sector_cache;
  return entries[entry_offset / 4] & 0x0FFFFFFF;
}

/**
 * @brief Check if cluster is end of chain
 * @param cluster Cluster number to check
 * @return true if end marker, false otherwise
 */
static bool cluster_is_end(u32 cluster)
{
  return cluster >= FAT32_CLUSTER_END;
}

/**
 * @brief Read a full cluster from volume
 * @param vol Volume to read from
 * @param cluster Cluster number
 * @param buf Buffer to store cluster data (must be bytes_per_cluster size)
 * @return 0 on success, negative on error
 */
static i64 vol_read_cluster(const fat32_volume_t *vol, u32 cluster, void *buf)
{
  u32 sector = cluster_to_sector(vol, cluster);

  for(u32 i = 0; i < vol->sectors_per_cluster; i++) {
    if(vol_read_sector(vol, sector + i, (u8 *)buf + (size_t)i * FAT32_SECTOR_SIZE) <
       0) {
      return -1;
    }
  }

  return 0;
}

/**
 * @brief Write a full cluster to volume
 * @param vol Volume to write to
 * @param cluster Cluster number
 * @param buf Buffer with cluster data (must be bytes_per_cluster size)
 * @return 0 on success, negative on error
 */
static i64 vol_write_cluster(const fat32_volume_t *vol, u32 cluster, const void *buf)
{
  u32 sector = cluster_to_sector(vol, cluster);

  for(u32 i = 0; i < vol->sectors_per_cluster; i++) {
    if(vol_write_sector(
           vol, sector + i, (const u8 *)buf + (size_t)i * FAT32_SECTOR_SIZE
       ) < 0) {
      return -1;
    }
  }

  return 0;
}

/**
 * @brief Write FAT entry for cluster.
 * @param vol Volume.
 * @param cluster Cluster number.
 * @param value Value to write (next cluster or end marker).
 * @return 0 on success, negative on error.
 */
static i64 fat_write_entry(const fat32_volume_t *vol, u32 cluster, u32 value)
{
  u32 fat_offset   = cluster * 4;
  u32 fat_sector   = vol->fat_start + (fat_offset / FAT32_SECTOR_SIZE);
  u32 entry_offset = fat_offset % FAT32_SECTOR_SIZE;

  /* Read the FAT sector */
  if(vol_read_sector(vol, fat_sector, sector_cache) < 0) {
    return -1;
  }

  /* Modify the entry (preserve top 4 bits) */
  u32 *entries = (u32 *)sector_cache;
  u32  idx     = entry_offset / 4;
  entries[idx] = (entries[idx] & 0xF0000000) | (value & 0x0FFFFFFF);

  /* Write back the FAT sector */
  if(vol_write_sector(vol, fat_sector, sector_cache) < 0) {
    return -1;
  }

  return 0;
}

/**
 * @brief Allocate a free cluster from FAT.
 * @param vol Volume.
 * @return Allocated cluster number, or 0 on failure.
 */
static u32 fat_alloc_cluster(const fat32_volume_t *vol)
{
  /* Search for a free cluster starting from cluster 2 */
  for(u32 cluster = 2; cluster < vol->total_clusters + 2; cluster++) {
    u32 entry = fat_read_entry(vol, cluster);
    if(entry == FAT32_CLUSTER_FREE) {
      /* Mark as end of chain */
      if(fat_write_entry(vol, cluster, FAT32_CLUSTER_END) < 0) {
        return 0;
      }
      return cluster;
    }
  }
  return 0; /* No free clusters */
}

/**
 * @brief Free a cluster chain starting from a cluster.
 * @param vol Volume.
 * @param start_cluster First cluster to free.
 * @return 0 on success, negative on error.
 */
static i64 fat_free_chain(const fat32_volume_t *vol, u32 start_cluster)
{
  u32 cluster = start_cluster;

  while(cluster >= 2 && !cluster_is_end(cluster) &&
        cluster != FAT32_CLUSTER_BAD) {
    u32 next = fat_read_entry(vol, cluster);
    if(fat_write_entry(vol, cluster, FAT32_CLUSTER_FREE) < 0) {
      return -1;
    }
    cluster = next;
  }

  return 0;
}

/**
 * @brief Convert 8.3 filename to normal format (lowercase)
 * @param fat_name FAT 8.3 name (11 bytes)
 * @param out Output buffer for normal filename
 */
static void fat_name_to_string(const u8 *fat_name, char *out)
{
  int i, j = 0;

  /* Copy name (up to 8 chars, trim spaces), convert to lowercase */
  for(i = 0; i < 8 && fat_name[i] != ' '; i++) {
    char c = (char)fat_name[i];
    if(c >= 'A' && c <= 'Z')
      c += 32;
    out[j++] = c;
  }

  /* Add dot and extension if present */
  if(fat_name[8] != ' ') {
    out[j++] = '.';
    for(i = 8; i < 11 && fat_name[i] != ' '; i++) {
      char c = (char)fat_name[i];
      if(c >= 'A' && c <= 'Z')
        c += 32;
      out[j++] = c;
    }
  }

  out[j] = '\0';
}

/**
 * @brief Convert string to 8.3 format
 * @param str Normal filename string
 * @param fat_name Output buffer for FAT 8.3 name (11 bytes)
 */
static void string_to_fat_name(const char *str, u8 *fat_name)
{
  int i, j;

  /* Initialize with spaces */
  for(i = 0; i < 11; i++)
    fat_name[i] = ' ';

  /* Find dot */
  const char *dot = NULL;
  for(i = 0; str[i]; i++) {
    if(str[i] == '.')
      dot = &str[i];
  }

  /* Copy name */
  j = 0;
  for(i = 0; str[i] && str[i] != '.' && j < 8; i++) {
    char c = str[i];
    if(c >= 'a' && c <= 'z')
      c -= 32;
    fat_name[j++] = c;
  }

  /* Copy extension */
  if(dot) {
    j = 8;
    for(i = 1; dot[i] && j < 11; i++) {
      char c = dot[i];
      if(c >= 'a' && c <= 'z')
        c -= 32;
      fat_name[j++] = c;
    }
  }
}
/**
 * @brief Find entry in directory by name
 * @param vol Volume containing directory
 * @param dir_cluster Directory cluster number
 * @param name Filename to search for
 * @param out_entry Output buffer for directory entry
 * @param out_cluster Optional: output for cluster containing the entry
 * @param out_offset Optional: output for offset of entry within cluster
 * @return 0 on success, negative if not found
 */
static i64 find_entry_in_dir(
    const fat32_volume_t *vol, u32 dir_cluster, const char *name,
    fat32_dirent_t *out_entry, u32 *out_cluster, u32 *out_offset
)
{
  u8 *cluster_buf = kmalloc(vol->bytes_per_cluster);
  if(!cluster_buf)
    return -1;

  u8 search_name[11];
  string_to_fat_name(name, search_name);

  u32 cluster = dir_cluster;

  while(!cluster_is_end(cluster)) {
    if(vol_read_cluster(vol, cluster, cluster_buf) < 0) {
      kfree(cluster_buf);
      return -1;
    }

    const fat32_dirent_t *entries = (const fat32_dirent_t *)cluster_buf;
    u32 entries_per_cluster = vol->bytes_per_cluster / FAT32_DIR_ENTRY_SIZE;

    for(u32 i = 0; i < entries_per_cluster; i++) {
      const fat32_dirent_t *e = &entries[i];

      /* End of directory */
      if(e->name[0] == 0x00) {
        kfree(cluster_buf);
        return -1;
      }

      /* Deleted entry */
      if(e->name[0] == 0xE5)
        continue;

      /* LFN entry */
      if((e->attr & FAT_ATTR_LFN) == FAT_ATTR_LFN)
        continue;

      /* Volume label */
      if(e->attr & FAT_ATTR_VOLUME_ID)
        continue;

      /* Compare name */
      bool match = true;
      for(int j = 0; j < 11; j++) {
        if(e->name[j] != search_name[j]) {
          match = false;
          break;
        }
      }

      if(match) {
        kmemcpy(out_entry, e, sizeof(fat32_dirent_t));
        if(out_cluster)
          *out_cluster = cluster;
        if(out_offset)
          *out_offset = i * FAT32_DIR_ENTRY_SIZE;
        kfree(cluster_buf);
        return 0;
      }
    }

    cluster = fat_read_entry(vol, cluster);
  }

  kfree(cluster_buf);
  return -1;
}

/**
 * @brief Resolve path to directory entry
 * @param vol Volume to search in
 * @param path Path to resolve (e.g., "/dir/file.txt")
 * @param out_entry Output buffer for entry
 * @param out_parent_cluster Optional output for parent directory cluster
 * @param out_entry_cluster Optional output for cluster containing the entry
 * @param out_entry_offset Optional output for offset of entry within cluster
 * @return 0 on success, negative if not found
 */
static i64 resolve_path(
    const fat32_volume_t *vol, const char *path, fat32_dirent_t *out_entry,
    u32 *out_parent_cluster, u32 *out_entry_cluster, u32 *out_entry_offset
)
{
  u32  current_cluster = vol->root_cluster;
  char component[FAT32_NAME_MAX];
  u64  i = 0;

  /* Skip leading slash */
  if(path[0] == '/')
    i++;

  /* Root directory */
  if(path[i] == '\0') {
    /* Fake root entry */
    kzero(out_entry, sizeof(fat32_dirent_t));
    out_entry->attr         = FAT_ATTR_DIRECTORY;
    out_entry->cluster_high = (vol->root_cluster >> 16) & 0xFFFF;
    out_entry->cluster_low  = vol->root_cluster & 0xFFFF;
    if(out_parent_cluster)
      *out_parent_cluster = vol->root_cluster;
    if(out_entry_cluster)
      *out_entry_cluster = 0;
    if(out_entry_offset)
      *out_entry_offset = 0;
    return 0;
  }

  fat32_dirent_t entry;
  u32            entry_cluster = 0;
  u32            entry_offset  = 0;

  while(path[i]) {
    /* Skip slashes */
    while(path[i] == '/')
      i++;
    if(!path[i])
      break;

    /* Extract component */
    u64 j = 0;
    while(path[i] && path[i] != '/' && j < FAT32_NAME_MAX - 1) {
      component[j++] = path[i++];
    }
    component[j] = '\0';

    /* Find in current directory */
    if(find_entry_in_dir(
           vol, current_cluster, component, &entry, &entry_cluster,
           &entry_offset
       ) < 0) {
      return -1;
    }

    if(out_parent_cluster)
      *out_parent_cluster = current_cluster;

    /* Get cluster for this entry */
    current_cluster = ((u32)entry.cluster_high << 16) | entry.cluster_low;

    /* If not a directory and more path remains, error */
    if(path[i] && !(entry.attr & FAT_ATTR_DIRECTORY)) {
      return -1;
    }
  }

  kmemcpy(out_entry, &entry, sizeof(fat32_dirent_t));
  if(out_entry_cluster)
    *out_entry_cluster = entry_cluster;
  if(out_entry_offset)
    *out_entry_offset = entry_offset;
  return 0;
}
/**
 * @brief Initialize FAT32 filesystem subsystem
 *
 * Zeros out volume and file structures. Must be called before mounting.
 */
// cppcheck-suppress unusedFunction
void fat32_init(void)
{
  kzero(volumes, sizeof(volumes));
  kzero(files, sizeof(files));
  console_print("[FAT32] Initialized\n");
}

/**
 * @brief Mount a FAT32 volume
 * @param drive ATA drive index
 * @param partition_lba LBA of partition start sector
 * @return Pointer to mounted volume, or NULL on error
 *
 * Reads boot sector, validates FAT32 signature, and initializes volume
 * structure.
 */
fat32_volume_t *fat32_mount(u8 drive, u32 partition_lba)
{
  /* Find free volume slot */
  fat32_volume_t *vol = NULL;
  for(int i = 0; i < MAX_VOLUMES; i++) {
    if(!volumes[i].mounted) {
      vol = &volumes[i];
      break;
    }
  }
  if(!vol)
    return NULL;

  /* Read boot sector */
  if(ata_read(drive, partition_lba, 1, sector_cache) < 0) {
    console_print("[FAT32] Failed to read boot sector\n");
    return NULL;
  }

  const fat32_bpb_t *bpb = (const fat32_bpb_t *)sector_cache;

  /* Verify FAT32 signature */
  if(bpb->boot_signature != 0x29) {
    console_print("[FAT32] Invalid boot signature\n");
    return NULL;
  }

  /* Check for FAT32 */
  if(bpb->fat_size_16 != 0 || bpb->fat_size_32 == 0) {
    console_print("[FAT32] Not a FAT32 volume\n");
    return NULL;
  }

  /* Fill volume info */
  vol->drive               = drive;
  vol->partition_lba       = partition_lba;
  vol->sectors_per_cluster = bpb->sectors_per_cluster;
  vol->bytes_per_cluster   = bpb->sectors_per_cluster * FAT32_SECTOR_SIZE;
  vol->fat_start           = bpb->reserved_sectors;
  vol->fat_size            = bpb->fat_size_32;
  vol->root_cluster        = bpb->root_cluster;
  vol->data_start = bpb->reserved_sectors + (bpb->fat_count * bpb->fat_size_32);
  vol->total_clusters =
      (bpb->total_sectors_32 - vol->data_start) / bpb->sectors_per_cluster;
  vol->mounted = true;

  console_printf(
      "[FAT32] Mounted: %d clusters, %d bytes/cluster\n",
      (int)vol->total_clusters, (int)vol->bytes_per_cluster
  );

  return vol;
}

/**
 * @brief Unmount a FAT32 volume
 * @param vol Volume to unmount
 */
void fat32_unmount(fat32_volume_t *vol)
{
  if(vol) {
    vol->mounted = false;
  }
}

/**
 * @brief Open a file or directory
 * @param vol Volume containing the file
 * @param path Path to file (e.g., "/dir/file.txt")
 * @return File handle, or NULL if file not found or no free handles
 */
fat32_file_t *fat32_open(fat32_volume_t *vol, const char *path)
{
  if(!vol || !vol->mounted)
    return NULL;

  /* Find free file slot */
  fat32_file_t *file = NULL;
  for(int i = 0; i < MAX_FILES; i++) {
    if(!files[i].in_use) {
      file = &files[i];
      break;
    }
  }
  if(!file)
    return NULL;

  /* Resolve path */
  fat32_dirent_t entry;
  u32            entry_cluster = 0;
  u32            entry_offset  = 0;
  if(resolve_path(vol, path, &entry, NULL, &entry_cluster, &entry_offset) < 0) {
    return NULL;
  }

  /* Fill file handle */
  file->vol             = vol;
  file->start_cluster   = ((u32)entry.cluster_high << 16) | entry.cluster_low;
  file->current_cluster = file->start_cluster;
  file->cluster_offset  = 0;
  file->file_size       = entry.file_size;
  file->position        = 0;
  file->attr            = entry.attr;
  file->is_dir          = (entry.attr & FAT_ATTR_DIRECTORY) != 0;
  file->is_root         = (file->start_cluster == vol->root_cluster);
  file->in_use          = true;
  file->dirty           = false;
  file->parent_cluster  = entry_cluster;
  file->dirent_offset   = entry_offset;

  return file;
}

/**
 * @brief Close a file handle
 * @param file File handle to close
 */
void fat32_close(fat32_file_t *file)
{
  if(file) {
    file->in_use = false;
  }
}

/**
 * @brief Read data from a file
 * @param file File handle
 * @param buf Buffer to store read data
 * @param count Number of bytes to read
 * @return Number of bytes read, or negative on error
 */
i64 fat32_read(fat32_file_t *file, void *buf, u64 count)
{
  if(!file || !file->in_use || file->is_dir)
    return -1;

  const fat32_volume_t *vol = file->vol;
  u8             *dst        = (u8 *)buf;
  u64             bytes_read = 0;

  /* Limit to file size */
  if(file->position >= file->file_size)
    return 0;
  if(file->position + count > file->file_size) {
    count = file->file_size - file->position;
  }

  u8 *cluster_buf = kmalloc(vol->bytes_per_cluster);
  if(!cluster_buf)
    return -1;

  while(bytes_read < count && !cluster_is_end(file->current_cluster)) {
    /* Read current cluster */
    if(vol_read_cluster(vol, file->current_cluster, cluster_buf) < 0) {
      kfree(cluster_buf);
      return -1;
    }

    /* Calculate how much to read from this cluster */
    u64 cluster_remaining = vol->bytes_per_cluster - file->cluster_offset;
    u64 to_read           = count - bytes_read;
    if(to_read > cluster_remaining)
      to_read = cluster_remaining;

    /* Copy data */
    kmemcpy(dst + bytes_read, cluster_buf + file->cluster_offset, to_read);
    bytes_read += to_read;
    file->position += to_read;
    file->cluster_offset += to_read;

    /* Move to next cluster if needed */
    if(file->cluster_offset >= vol->bytes_per_cluster) {
      file->current_cluster = fat_read_entry(vol, file->current_cluster);
      file->cluster_offset  = 0;
    }
  }

  kfree(cluster_buf);
  return (i64)bytes_read;
}

/**
 * @brief Read next directory entry
 * @param dir Directory handle (opened with O_DIRECTORY)
 * @param entry Output buffer for entry information
 * @return 1 if entry read, 0 if end of directory, -1 on error
 */
i64 fat32_readdir(fat32_file_t *dir, fat32_entry_t *entry)
{
  if(!dir || !dir->in_use || !dir->is_dir) {
    return -1;
  }

  const fat32_volume_t *vol = dir->vol;

  u8             *cluster_buf = kmalloc(vol->bytes_per_cluster);
  if(!cluster_buf)
    return -1;

  while(!cluster_is_end(dir->current_cluster)) {
    if(vol_read_cluster(vol, dir->current_cluster, cluster_buf) < 0) {
      kfree(cluster_buf);
      return -1;
    }

    fat32_dirent_t *entries = (fat32_dirent_t *)cluster_buf;
    u32 entries_per_cluster = vol->bytes_per_cluster / FAT32_DIR_ENTRY_SIZE;
    u32 start_idx           = dir->cluster_offset / FAT32_DIR_ENTRY_SIZE;

    for(u32 i = start_idx; i < entries_per_cluster; i++) {
      fat32_dirent_t *e = &entries[i];

      /* End of directory */
      if(e->name[0] == 0x00) {
        kfree(cluster_buf);
        return 0;
      }

      dir->cluster_offset = (i + 1) * FAT32_DIR_ENTRY_SIZE;

      /* Skip deleted, LFN, volume label */
      if(e->name[0] == 0xE5)
        continue;
      if((e->attr & FAT_ATTR_LFN) == FAT_ATTR_LFN)
        continue;
      if(e->attr & FAT_ATTR_VOLUME_ID)
        continue;

      /* Convert name */
      fat_name_to_string(e->name, entry->name);
      entry->attr    = e->attr;
      entry->size    = e->file_size;
      entry->cluster = ((u32)e->cluster_high << 16) | e->cluster_low;

      kfree(cluster_buf);
      return 1;
    }

    /* Move to next cluster */
    dir->current_cluster = fat_read_entry(vol, dir->current_cluster);
    dir->cluster_offset  = 0;
  }

  kfree(cluster_buf);
  return 0;
}

/**
 * @brief Get file/directory status
 * @param vol Volume containing the file
 * @param path Path to file or directory
 * @param entry Output buffer for file information
 * @return 0 on success, -1 if not found
 */
i64 fat32_stat(const fat32_volume_t *vol, const char *path, fat32_entry_t *entry)
{
  if(!vol || !vol->mounted)
    return -1;

  fat32_dirent_t dirent;
  if(resolve_path(vol, path, &dirent, NULL, NULL, NULL) < 0) {
    return -1;
  }

  fat_name_to_string(dirent.name, entry->name);
  entry->attr    = dirent.attr;
  entry->size    = dirent.file_size;
  entry->cluster = ((u32)dirent.cluster_high << 16) | dirent.cluster_low;

  return 0;
}

/**
 * @brief Update directory entry on disk
 * @param file File handle with parent_cluster and dirent_offset set
 * @return 0 on success, negative on error
 */
static i64 update_dirent(const fat32_file_t *file)
{
  const fat32_volume_t *vol = file->vol;

  /* Cannot update root directory entry (root has no parent) */
  if(file->parent_cluster < 2)
    return 0;

  u8 *cluster_buf = kmalloc(vol->bytes_per_cluster);
  if(!cluster_buf)
    return -1;

  /* The directory entry location */
  u32 cluster      = file->parent_cluster;
  u32 offset       = file->dirent_offset;
  u32 cluster_size = vol->bytes_per_cluster;

  /* Sanity check: offset should be within a single cluster */
  if(offset >= cluster_size) {
    kfree(cluster_buf);
    return -1;
  }

  /* Read the cluster */
  if(vol_read_cluster(vol, cluster, cluster_buf) < 0) {
    kfree(cluster_buf);
    return -1;
  }

  /* Update the directory entry */
  fat32_dirent_t *dirent = (fat32_dirent_t *)(cluster_buf + offset);
  dirent->file_size      = file->file_size;
  dirent->cluster_high   = (file->start_cluster >> 16) & 0xFFFF;
  dirent->cluster_low    = file->start_cluster & 0xFFFF;

  /* Write back the cluster */
  if(vol_write_cluster(vol, cluster, cluster_buf) < 0) {
    kfree(cluster_buf);
    return -1;
  }

  kfree(cluster_buf);
  return 0;
}

/**
 * @brief Seek to a position in a file
 * @param file File handle
 * @param offset Seek offset
 * @param whence SEEK_SET (0), SEEK_CUR (1), or SEEK_END (2)
 * @return New position, or negative on error
 */
i64 fat32_seek(fat32_file_t *file, i64 offset, i32 whence)
{
  if(!file || !file->in_use)
    return -1;

  i64 new_pos;
  switch(whence) {
  case 0: /* SEEK_SET */
    new_pos = offset;
    break;
  case 1: /* SEEK_CUR */
    new_pos = (i64)file->position + offset;
    break;
  case 2: /* SEEK_END */
    new_pos = (i64)file->file_size + offset;
    break;
  default:
    return -1;
  }

  if(new_pos < 0)
    return -1;

  file->position = (u32)new_pos;

  /* Recalculate current_cluster and cluster_offset */
  const fat32_volume_t *vol         = file->vol;
  u32             cluster_idx = file->position / vol->bytes_per_cluster;

  file->current_cluster = file->start_cluster;
  for(u32 i = 0; i < cluster_idx && !cluster_is_end(file->current_cluster);
      i++) {
    file->current_cluster = fat_read_entry(vol, file->current_cluster);
  }

  file->cluster_offset = file->position % vol->bytes_per_cluster;

  return (i64)file->position;
}

/**
 * @brief Write data to a file
 * @param file File handle
 * @param buf Source buffer
 * @param count Number of bytes to write
 * @return Bytes written, or negative on error
 */
i64 fat32_write(fat32_file_t *file, const void *buf, u64 count)
{
  if(!file || !file->in_use || file->is_dir)
    return -1;

  if(count == 0)
    return 0;

  const fat32_volume_t *vol           = file->vol;
  const u8       *src           = (const u8 *)buf;
  u64             bytes_written = 0;

  u8             *cluster_buf = kmalloc(vol->bytes_per_cluster);
  if(!cluster_buf)
    return -1;

  /* If file has no clusters yet, allocate the first one */
  if(file->start_cluster < 2) {
    u32 new_cluster = fat_alloc_cluster(vol);
    if(new_cluster == 0) {
      kfree(cluster_buf);
      return -1;
    }
    file->start_cluster   = new_cluster;
    file->current_cluster = new_cluster;
    file->cluster_offset  = 0;
    file->dirty           = true;

    /* Zero out the new cluster */
    kzero(cluster_buf, vol->bytes_per_cluster);
    if(vol_write_cluster(vol, new_cluster, cluster_buf) < 0) {
      kfree(cluster_buf);
      return -1;
    }
  }

  /* Seek to correct position if needed */
  if(file->position > 0) {
    u32 target_cluster_idx = file->position / vol->bytes_per_cluster;
    u32 current_idx        = 0;

    file->current_cluster = file->start_cluster;
    while(current_idx < target_cluster_idx) {
      u32 next = fat_read_entry(vol, file->current_cluster);
      if(cluster_is_end(next)) {
        /* Need to allocate more clusters to reach position */
        u32 new_cluster = fat_alloc_cluster(vol);
        if(new_cluster == 0) {
          kfree(cluster_buf);
          return -1;
        }
        fat_write_entry(vol, file->current_cluster, new_cluster);
        next = new_cluster;

        /* Zero out the new cluster */
        kzero(cluster_buf, vol->bytes_per_cluster);
        if(vol_write_cluster(vol, new_cluster, cluster_buf) < 0) {
          kfree(cluster_buf);
          return -1;
        }
      }
      file->current_cluster = next;
      current_idx++;
    }
    file->cluster_offset = file->position % vol->bytes_per_cluster;
  }

  while(bytes_written < count) {
    /* Read current cluster (for partial writes) */
    if(vol_read_cluster(vol, file->current_cluster, cluster_buf) < 0) {
      kfree(cluster_buf);
      return (i64)bytes_written;
    }

    /* Calculate how much to write to this cluster */
    u64 cluster_remaining = vol->bytes_per_cluster - file->cluster_offset;
    u64 to_write          = count - bytes_written;
    if(to_write > cluster_remaining)
      to_write = cluster_remaining;

    /* Copy data to cluster buffer */
    kmemcpy(cluster_buf + file->cluster_offset, src + bytes_written, to_write);

    /* Write cluster back to disk */
    if(vol_write_cluster(vol, file->current_cluster, cluster_buf) < 0) {
      kfree(cluster_buf);
      return (i64)bytes_written;
    }

    bytes_written += to_write;
    file->position += to_write;
    file->cluster_offset += to_write;

    /* Update file size if we wrote past the end */
    if(file->position > file->file_size) {
      file->file_size = file->position;
      file->dirty     = true;
    }

    /* Move to next cluster if needed */
    if(file->cluster_offset >= vol->bytes_per_cluster) {
      u32 next = fat_read_entry(vol, file->current_cluster);

      if(cluster_is_end(next) && bytes_written < count) {
        /* Need to allocate a new cluster */
        u32 new_cluster = fat_alloc_cluster(vol);
        if(new_cluster == 0) {
          kfree(cluster_buf);
          /* Update directory entry with what we have */
          if(file->dirty) {
            update_dirent(file);
          }
          return (i64)bytes_written;
        }

        /* Link to new cluster */
        fat_write_entry(vol, file->current_cluster, new_cluster);
        next = new_cluster;

        /* Zero out the new cluster */
        kzero(cluster_buf, vol->bytes_per_cluster);
        if(vol_write_cluster(vol, new_cluster, cluster_buf) < 0) {
          kfree(cluster_buf);
          return (i64)bytes_written;
        }
      }

      file->current_cluster = next;
      file->cluster_offset  = 0;
    }
  }

  kfree(cluster_buf);

  /* Update directory entry if file was modified */
  if(file->dirty) {
    update_dirent(file);
  }

  return (i64)bytes_written;
}

/**
 * @brief Truncate a file to zero length
 * @param file File handle
 * @return 0 on success, negative on error
 */
i64 fat32_truncate(fat32_file_t *file)
{
  if(!file || !file->in_use || file->is_dir)
    return -1;

  const fat32_volume_t *vol = file->vol;

  /* Free all clusters if file has any */
  if(file->start_cluster >= 2) {
    fat_free_chain(vol, file->start_cluster);
  }

  /* Reset file state */
  file->start_cluster   = 0;
  file->current_cluster = 0;
  file->cluster_offset  = 0;
  file->file_size       = 0;
  file->position        = 0;
  file->dirty           = true;

  /* Update directory entry */
  update_dirent(file);

  return 0;
}

/**
 * @brief Flush file changes to disk
 * @param file File handle
 * @return 0 on success, negative on error
 */
i64 fat32_flush(fat32_file_t *file)
{
  if(!file || !file->in_use)
    return -1;

  if(file->dirty) {
    if(update_dirent(file) < 0) {
      return -1;
    }
    file->dirty = false;
  }

  return 0;
}

/**
 * @brief Find a free entry in a directory
 * @param vol Volume
 * @param dir_cluster Directory cluster
 * @param out_cluster Output: cluster containing free entry
 * @param out_offset Output: offset of free entry within cluster
 * @return 0 on success, negative on error
 */
static i64 find_free_dirent(
    const fat32_volume_t *vol, u32 dir_cluster, u32 *out_cluster, u32 *out_offset
)
{
  u8 *cluster_buf = kmalloc(vol->bytes_per_cluster);
  if(!cluster_buf)
    return -1;

  u32 cluster = dir_cluster;

  while(!cluster_is_end(cluster)) {
    if(vol_read_cluster(vol, cluster, cluster_buf) < 0) {
      kfree(cluster_buf);
      return -1;
    }

    const fat32_dirent_t *entries = (const fat32_dirent_t *)cluster_buf;
    u32 entries_per_cluster = vol->bytes_per_cluster / FAT32_DIR_ENTRY_SIZE;

    for(u32 i = 0; i < entries_per_cluster; i++) {
      /* Free entry (0x00 = end, 0xE5 = deleted) */
      if(entries[i].name[0] == 0x00 || entries[i].name[0] == 0xE5) {
        *out_cluster = cluster;
        *out_offset  = i * FAT32_DIR_ENTRY_SIZE;
        kfree(cluster_buf);
        return 0;
      }
    }

    u32 next = fat_read_entry(vol, cluster);
    if(cluster_is_end(next)) {
      /* Need to allocate a new cluster for directory */
      u32 new_cluster = fat_alloc_cluster(vol);
      if(new_cluster == 0) {
        kfree(cluster_buf);
        return -1;
      }
      fat_write_entry(vol, cluster, new_cluster);

      /* Zero out the new cluster */
      kzero(cluster_buf, vol->bytes_per_cluster);
      if(vol_write_cluster(vol, new_cluster, cluster_buf) < 0) {
        kfree(cluster_buf);
        return -1;
      }

      *out_cluster = new_cluster;
      *out_offset  = 0;
      kfree(cluster_buf);
      return 0;
    }

    cluster = next;
  }

  kfree(cluster_buf);
  return -1;
}

/**
 * @brief Extract filename from path
 * @param path Full path
 * @param name Output buffer for filename
 * @param parent_path Output buffer for parent path (optional)
 */
static void path_split(const char *path, char *name, char *parent_path)
{
  u64 len        = kstrlen(path);
  i64 last_slash = -1;

  for(u64 i = 0; i < len; i++) {
    if(path[i] == '/')
      last_slash = (i64)i;
  }

  if(last_slash == -1) {
    /* No slash, entire path is filename */
    for(u64 i = 0; i < len && i < FAT32_NAME_MAX - 1; i++) {
      name[i] = path[i];
    }
    name[len < FAT32_NAME_MAX ? len : FAT32_NAME_MAX - 1] = '\0';
    if(parent_path) {
      parent_path[0] = '/';
      parent_path[1] = '\0';
    }
  } else if(last_slash == 0) {
    /* File in root directory */
    for(u64 i = 1; i < len && i < FAT32_NAME_MAX; i++) {
      name[i - 1] = path[i];
    }
    name[len - 1 < FAT32_NAME_MAX ? len - 1 : FAT32_NAME_MAX - 1] = '\0';
    if(parent_path) {
      parent_path[0] = '/';
      parent_path[1] = '\0';
    }
  } else {
    /* Extract parent path and filename */
    if(parent_path) {
      for(i64 i = 0; i < last_slash && i < FAT32_NAME_MAX - 1; i++) {
        parent_path[i] = path[i];
      }
      parent_path
          [last_slash < FAT32_NAME_MAX ? last_slash : FAT32_NAME_MAX - 1] =
              '\0';
    }
    u64 name_len = len - last_slash - 1;
    for(u64 i = 0; i < name_len && i < FAT32_NAME_MAX - 1; i++) {
      name[i] = path[last_slash + 1 + i];
    }
    name[name_len < FAT32_NAME_MAX ? name_len : FAT32_NAME_MAX - 1] = '\0';
  }
}

/**
 * @brief Create a new file
 * @param vol Volume to create file on
 * @param path Path to the new file
 * @return File handle, or NULL on error
 */
fat32_file_t *fat32_create(fat32_volume_t *vol, const char *path)
{
  if(!vol || !vol->mounted || !path)
    return NULL;

  /* Check if file already exists */
  fat32_dirent_t existing;
  if(resolve_path(vol, path, &existing, NULL, NULL, NULL) == 0) {
    /* File exists, just open it */
    return fat32_open(vol, path);
  }

  /* Find free file slot */
  fat32_file_t *file = NULL;
  for(int i = 0; i < MAX_FILES; i++) {
    if(!files[i].in_use) {
      file = &files[i];
      break;
    }
  }
  if(!file)
    return NULL;

  /* Extract parent path and filename */
  char filename[FAT32_NAME_MAX];
  char parent_path[FAT32_NAME_MAX];
  path_split(path, filename, parent_path);

  /* Find parent directory */
  fat32_dirent_t parent_entry;
  u32            parent_cluster = vol->root_cluster;

  if(parent_path[0] != '/' || parent_path[1] != '\0') {
    if(resolve_path(vol, parent_path, &parent_entry, NULL, NULL, NULL) < 0) {
      return NULL; /* Parent doesn't exist */
    }
    if(!(parent_entry.attr & FAT_ATTR_DIRECTORY)) {
      return NULL; /* Parent is not a directory */
    }
    parent_cluster =
        ((u32)parent_entry.cluster_high << 16) | parent_entry.cluster_low;
  }

  /* Find a free entry in the parent directory */
  u32 entry_cluster, entry_offset;
  if(find_free_dirent(vol, parent_cluster, &entry_cluster, &entry_offset) < 0) {
    return NULL;
  }

  /* Create the directory entry */
  u8 *cluster_buf = kmalloc(vol->bytes_per_cluster);
  if(!cluster_buf)
    return NULL;

  if(vol_read_cluster(vol, entry_cluster, cluster_buf) < 0) {
    kfree(cluster_buf);
    return NULL;
  }

  fat32_dirent_t *new_entry = (fat32_dirent_t *)(cluster_buf + entry_offset);
  kzero(new_entry, sizeof(fat32_dirent_t));
  string_to_fat_name(filename, new_entry->name);
  new_entry->attr         = FAT_ATTR_ARCHIVE;
  new_entry->cluster_high = 0;
  new_entry->cluster_low  = 0;
  new_entry->file_size    = 0;

  /* Write back the cluster */
  if(vol_write_cluster(vol, entry_cluster, cluster_buf) < 0) {
    kfree(cluster_buf);
    return NULL;
  }

  kfree(cluster_buf);

  /* Fill file handle */
  file->vol             = vol;
  file->start_cluster   = 0;
  file->current_cluster = 0;
  file->cluster_offset  = 0;
  file->file_size       = 0;
  file->position        = 0;
  file->attr            = FAT_ATTR_ARCHIVE;
  file->is_dir          = false;
  file->in_use          = true;
  file->dirty           = false;
  file->parent_cluster  = entry_cluster;
  file->dirent_offset   = entry_offset;

  return file;
}

/**
 * @brief Delete a file from the filesystem
 * @param vol Volume containing the file
 * @param path Path to the file to delete
 * @return 0 on success, negative errno on error
 */
i64 fat32_unlink(const fat32_volume_t *vol, const char *path)
{
  if(!vol || !vol->mounted || !path)
    return -EINVAL;

  /* Resolve path to get entry info */
  fat32_dirent_t entry;
  u32            entry_cluster = 0;
  u32            entry_offset  = 0;

  if(resolve_path(vol, path, &entry, NULL, &entry_cluster, &entry_offset) < 0) {
    return -ENOENT; /* File not found */
  }

  /* Can't delete directories with unlink */
  if(entry.attr & FAT_ATTR_DIRECTORY) {
    return -EISDIR;
  }

  /* Get the file's data cluster chain start */
  u32 file_cluster = ((u32)entry.cluster_high << 16) | entry.cluster_low;

  /* Mark directory entry as deleted (0xE5) */
  u8 *cluster_buf = kmalloc(vol->bytes_per_cluster);
  if(!cluster_buf)
    return -ENOMEM;

  if(vol_read_cluster(vol, entry_cluster, cluster_buf) < 0) {
    kfree(cluster_buf);
    return -EIO;
  }

  /* Set first byte of name to 0xE5 (deleted marker) */
  cluster_buf[entry_offset] = 0xE5;

  if(vol_write_cluster(vol, entry_cluster, cluster_buf) < 0) {
    kfree(cluster_buf);
    return -EIO;
  }

  kfree(cluster_buf);

  /* Free the cluster chain if file had data */
  if(file_cluster >= 2) {
    fat_free_chain(vol, file_cluster);
  }

  return 0;
}
