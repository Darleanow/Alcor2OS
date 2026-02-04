/**
 * @file src/fs/vfs.c
 * @brief Virtual File System (ramfs + FAT32 mounts).
 */

#include <alcor2/console.h>
#include <alcor2/errno.h>
#include <alcor2/fat32.h>
#include <alcor2/heap.h>
#include <alcor2/kstdlib.h>
#include <alcor2/proc.h>
#include <alcor2/vfs.h>

#define VFS_MAX_MOUNTS 8

/** @brief Filesystem type for mount point. */
typedef enum
{
  FS_TYPE_RAMFS,
  FS_TYPE_FAT32
} fs_type_t;

/** @brief Mount point descriptor. */
typedef struct
{
  void     *fs_data;            /**< FS-specific data */
  fs_type_t type;               /**< Filesystem type */
  bool      active;             /**< Mount active */
  char      path[VFS_PATH_MAX]; /**< Mount point path */
} vfs_mount_t;

static vfs_mount_t mounts[VFS_MAX_MOUNTS];

static vfs_node_t *root_node = NULL;
static vfs_fd_t    fd_table[VFS_MAX_FD];
static vfs_dir_t   dir_table[VFS_MAX_FD];
static char        cwd[VFS_PATH_MAX] = "/";

/** @brief Magic value for FAT32 file descriptors (bit 31). */
#define FAT32_FD_MAGIC 0x80000000

static bool         is_fat32_fd(i64 fd);
static vfs_mount_t *find_mount(const char *path);
static const char  *get_relative_path(const char *path, const vfs_mount_t *mount);

/*
 * @brief Normalize a path by resolving . and .. components.
 * @param path Path to normalize (modified in place).
 */
static void normalize_path(char *path)
{
  if(!path || path[0] != '/')
    return;

  /* Work buffer to build normalized path */
  char   result[VFS_PATH_MAX];
  char  *out   = result;
  char  *p     = path + 1; /* Skip initial '/' */

  *out++ = '/';

  while(*p) {
    /* Skip consecutive slashes */
    while(*p == '/')
      p++;
    if(!*p)
      break;

    /* Extract component into temp buffer */
    char comp[VFS_NAME_MAX];
    u64  len = 0;
    while(*p && *p != '/' && len < VFS_NAME_MAX - 1) {
      comp[len++] = *p++;
    }
    comp[len] = '\0';

    /* Handle . (current dir) - skip it */
    if(len == 1 && comp[0] == '.') {
      continue;
    }

    /* Handle .. (parent dir) - go back to previous / */
    if(len == 2 && comp[0] == '.' && comp[1] == '.') {
      /* Back up to previous component */
      if(out > result + 1) {
        out--; /* Remove trailing char */
        while(out > result + 1 && *(out - 1) != '/')
          out--;
      }
      continue;
    }

    /* Regular component - append to result */
    if(out > result + 1) {
      *out++ = '/';
    }
    for(u64 i = 0; i < len && out < result + VFS_PATH_MAX - 1; i++) {
      *out++ = comp[i];
    }
  }

  /* Ensure at least "/" */
  if(out == result + 1 && result[0] == '/') {
    /* Just "/" is fine */
  }
  *out = '\0';

  /* Copy back to original */
  kstrncpy(path, result, VFS_PATH_MAX);
}

/**
 * @brief Convert a relative path to absolute path using cwd.
 * @param path Input path (relative or absolute).
 * @param out Output buffer for absolute path.
 * @param out_size Size of output buffer.
 */
static void make_absolute_path(const char *path, char *out, u64 out_size)
{
  if(!path || !out || out_size == 0) {
    if(out && out_size > 0) out[0] = '\0';
    return;
  }

  /* Build combined path first */
  if(path[0] == '/') {
    /* Already absolute */
    kstrncpy(out, path, out_size);
  } else {
    /* Build absolute path from cwd + path */
    u64 path_len = kstrlen(path);

    /* Copy cwd first */
    kstrncpy(out, cwd, out_size);

    /* Add separator if needed */
    u64 pos = kstrlen(out);
    if(pos > 0 && out[pos - 1] != '/' && pos + 1 < out_size) {
      out[pos++] = '/';
      out[pos]   = '\0';
    }

    /* Append path */
    for(u64 i = 0; i < path_len && pos + i < out_size - 1; i++) {
      out[pos + i] = path[i];
    }
    out[pos + path_len < out_size ? pos + path_len : out_size - 1] = '\0';
  }

  /* Normalize to resolve . and .. */
  normalize_path(out);
}

/**
 * @brief Resolve path to VFS node.
 * @param path Path to resolve (absolute or relative).
 * @return Node pointer or NULL.
 */
static vfs_node_t *resolve_path(const char *path)
{
  if(!path || !path[0]) {
    return NULL;
  }

  vfs_node_t *node;
  char        component[VFS_NAME_MAX];
  u64         i = 0;

  /* Absolute vs relative */
  if(path[0] == '/') {
    node = root_node;
    i    = 1;
  } else {
    /* Resolve from current working directory */
    /* First, find the cwd node */
    node     = root_node;
    u64 cwdi = 0;
    if(cwd[0] == '/')
      cwdi = 1;

    while(cwd[cwdi]) {
      while(cwd[cwdi] == '/')
        cwdi++;
      if(!cwd[cwdi])
        break;

      u64  j = 0;
      char comp[VFS_NAME_MAX];
      while(cwd[cwdi] && cwd[cwdi] != '/' && j < VFS_NAME_MAX - 1) {
        comp[j++] = cwd[cwdi++];
      }
      comp[j] = '\0';

      if(j == 0)
        break;

      vfs_node_t *child = node->children;
      while(child) {
        if(kstreq(child->name, comp)) {
          node = child;
          break;
        }
        child = child->next;
      }
      if(!child) {
        /* cwd is invalid, fallback to root */
        node = root_node;
        break;
      }
    }
  }

  /* Root path */
  if(path[0] == '/' && path[1] == '\0') {
    return root_node;
  }

  while(path[i]) {
    /* Skip slashes */
    while(path[i] == '/')
      i++;
    if(!path[i])
      break;

    /* Extract component */
    u64 j = 0;
    while(path[i] && path[i] != '/' && j < VFS_NAME_MAX - 1) {
      component[j++] = path[i++];
    }
    component[j] = '\0';

    /* Handle . and .. */
    if(kstreq(component, ".")) {
      continue;
    }
    if(kstreq(component, "..")) {
      if(node->parent) {
        node = node->parent;
      }
      continue;
    }

    /* Find child */
    if(node->type != VFS_DIRECTORY) {
      return NULL;
    }

    vfs_node_t *child = node->children;
    while(child) {
      if(kstreq(child->name, component)) {
        break;
      }
      child = child->next;
    }

    if(!child) {
      return NULL;
    }

    node = child;
  }

  return node;
}

/**
 * @brief Resolve parent directory and extract filename from path.
 * 
 * Splits a path into its parent directory node and the final filename component.
 * Handles both absolute and relative paths.
 * 
 * @param path Path to resolve.
 * @param name_out Output buffer for filename (VFS_NAME_MAX bytes).
 * @return Parent directory node, or NULL on error.
 */
static vfs_node_t *resolve_parent(const char *path, char *name_out)
{
  if(!path || !path[0]) {
    return NULL;
  }

  /* Find last slash */
  u64 len        = kstrlen(path);
  i64 last_slash = -1;

  for(u64 i = 0; i < len; i++) {
    if(path[i] == '/') {
      last_slash = (i64)i;
    }
  }

  if(last_slash == -1) {
    /* No slash - parent is cwd */
    kstrncpy(name_out, path, VFS_NAME_MAX);
    /* Resolve cwd to get the actual current directory node */
    return resolve_path(cwd);
  }

  if(last_slash == 0) {
    /* Root directory is parent */
    kstrncpy(name_out, path + 1, VFS_NAME_MAX);
    return root_node;
  }

  /* Extract parent path */
  char parent_path[VFS_PATH_MAX];
  for(i64 i = 0; i < last_slash; i++) {
    parent_path[i] = path[i];
  }
  parent_path[last_slash] = '\0';

  /* Extract name */
  kstrncpy(name_out, path + last_slash + 1, VFS_NAME_MAX);

  return resolve_path(parent_path);
}

/**
 * @brief Create a new VFS node.
 * 
 * Allocates and initializes a new node with the given name and type.
 * 
 * @param name Node name (copied into node).
 * @param type Node type (VFS_FILE or VFS_DIRECTORY).
 * @return Pointer to new node, or NULL on allocation failure.
 */
static vfs_node_t *create_node(const char *name, u8 type)
{
  vfs_node_t *node = kzalloc(sizeof(vfs_node_t));
  if(!node)
    return NULL;

  kstrncpy(node->name, name, VFS_NAME_MAX);
  node->type     = type;
  node->size     = 0;
  node->data     = NULL;
  node->capacity = 0;
  node->parent   = NULL;
  node->children = NULL;
  node->next     = NULL;

  return node;
}

/**
 * @brief Add a child node to a parent directory.
 * 
 * Links the child into the parent's children list and sets the child's parent pointer.
 * 
 * @param parent Parent directory node.
 * @param child Child node to add.
 */
static void add_child(vfs_node_t *parent, vfs_node_t *child)
{
  child->parent    = parent;
  child->next      = parent->children;
  parent->children = child;
}

/**
 * @brief Initialize the Virtual File System.
 * 
 * Creates the root directory, initializes file descriptor and directory tables,
 * and sets up the initial mount table. Also initializes FAT32 support.
 */
void vfs_init(void)
{
  /* Initialize tables */
  kzero(fd_table, sizeof(fd_table));
  kzero(dir_table, sizeof(dir_table));
  kzero(mounts, sizeof(mounts));

  /* Create root directory */
  root_node = create_node("/", VFS_DIRECTORY);
  if(!root_node) {
    console_print("[VFS] Failed to create root!\n");
    return;
  }
  root_node->parent = root_node; /* Root is its own parent */

  /* Create /dev for device nodes - kept in ramfs */
  vfs_mkdir("/dev");

  /* Create device nodes for ATA drives
   * /dev/hda = Primary Master
   * /dev/hdb = Primary Slave
   * /dev/hdc = Secondary Master
   * /dev/hdd = Secondary Slave
   */
  vfs_touch("/dev/hda");
  vfs_touch("/dev/hdb");
  vfs_touch("/dev/hdc");
  vfs_touch("/dev/hdd");

  console_print("[VFS] Initialized (minimal ramfs + /dev)\n");
}

/**
 * @brief Open a file.
 * 
 * Opens a file in ramfs or mounted FAT32 filesystem. Supports creation, truncation,
 * and various access modes. File descriptors 0-2 are reserved for stdin/stdout/stderr.
 * 
 * @param path Path to file (absolute or relative).
 * @param flags Open flags (O_RDONLY, O_WRONLY, O_RDWR, O_CREAT, O_TRUNC, etc.).
 * @return File descriptor on success, negative on error.
 */
i64 vfs_open(const char *path, u32 flags)
{
  /* Convert to absolute path for mount point detection */
  char abs_path[VFS_PATH_MAX];
  make_absolute_path(path, abs_path, VFS_PATH_MAX);

  /* Check if path is on a mounted filesystem */
  vfs_mount_t *mount = find_mount(abs_path);
  if(mount && mount->type == FS_TYPE_FAT32) {
    /* Route to FAT32 */
    fat32_volume_t *vol      = mount->fs_data;
    const char     *rel_path = get_relative_path(abs_path, mount);

    /* Find free fd (skip 0,1,2 reserved for stdin/stdout/stderr) */
    i64 fd = -1;
    for(i64 i = 3; i < VFS_MAX_FD; i++) {
      if(!fd_table[i].in_use) {
        fd = i;
        break;
      }
    }
    if(fd < 0)
      return -1;

    fat32_file_t *file = NULL;

    /* Handle create flag */
    if(flags & O_CREAT) {
      file = fat32_create(vol, rel_path);
    } else {
      file = fat32_open(vol, rel_path);
    }

    if(!file)
      return -1;

    /* Handle truncate flag */
    if(flags & O_TRUNC) {
      fat32_truncate(file);
    }

    /* Handle append flag - seek to end */
    if(flags & O_APPEND) {
      fat32_seek(file, 0, 2); /* SEEK_END */
    }

    fd_table[fd].in_use = true;
    fd_table[fd].node   = (vfs_node_t *)file;
    fd_table[fd].offset = file->position;
    fd_table[fd].flags  = flags | FAT32_FD_MAGIC;
    
    /* Set owner PID */
    proc_t *p = proc_current();
    fd_table[fd].owner_pid = p ? p->pid : 0;

    return fd;
  }

  /* Ramfs path - use abs_path for consistency */
  vfs_node_t *node = resolve_path(abs_path);

  /* Create if needed */
  if(!node && (flags & O_CREAT)) {
    char        name[VFS_NAME_MAX];
    vfs_node_t *parent = resolve_parent(abs_path, name);

    if(!parent || parent->type != VFS_DIRECTORY) {
      return -1;
    }

    node = create_node(name, VFS_FILE);
    if(!node)
      return -1;

    add_child(parent, node);
  }

  if(!node || node->type != VFS_FILE) {
    /* Allow opening directories if O_DIRECTORY is set */
    if(node && node->type == VFS_DIRECTORY && (flags & O_DIRECTORY)) {
      /* Open directory for reading (getdents) */
      for(i64 i = 3; i < VFS_MAX_FD; i++) {
        if(!fd_table[i].in_use) {
          fd_table[i].offset = 0;
          fd_table[i].flags  = flags;
          fd_table[i].in_use = true;
          
          /* Set owner PID */
          proc_t *p = proc_current();
          fd_table[i].owner_pid = p ? p->pid : 0;
          
          return i;
        }
      }
      return -1;
    }
    return -1;
  }

  /* Find free fd (skip 0,1,2 reserved for stdin/stdout/stderr) */
  for(i64 i = 3; i < VFS_MAX_FD; i++) {
    if(!fd_table[i].in_use) {
      fd_table[i].offset = (flags & O_APPEND) ? node->size : 0;
      fd_table[i].flags  = flags;
      fd_table[i].in_use = true;
      
      /* Set owner PID */
      proc_t *p = proc_current();
      fd_table[i].owner_pid = p ? p->pid : 0;

      /* Truncate if needed */
      if(flags & O_TRUNC) {
        node->size = 0;
      }

      return i;
    }
  }

  return -1; /* No free fd */
}

/**
 * @brief Close a file descriptor.
 * 
 * Closes an open file descriptor and marks it as available for reuse.
 * For FAT32 files, flushes changes and calls the FAT32 close handler.
 * 
 * @param fd File descriptor to close.
 * @return 0 on success, negative on error.
 */
i64 vfs_close(i64 fd)
{
  if(fd < 0 || fd >= VFS_MAX_FD || !fd_table[fd].in_use) {
    return -1;
  }

  if(is_fat32_fd(fd)) {
    fat32_file_t *fatfile = (fat32_file_t *)fd_table[fd].node;
    fat32_flush(fatfile);
    fat32_close(fatfile);
    fd_table[fd].in_use = false;
    return 0;
  }

  fd_table[fd].in_use = false;
  return 0;
}

/**
 * @brief Read data from an open file.
 * 
 * Reads up to count bytes from the file at the current offset.
 * Handles both ramfs and FAT32 files.
 * 
 * @param fd File descriptor.
 * @param buf Destination buffer.
 * @param count Maximum bytes to read.
 * @return Number of bytes read, 0 on EOF, negative on error.
 */
i64 vfs_read(i64 fd, void *buf, u64 count)
{
  if(fd < 0 || fd >= VFS_MAX_FD || !fd_table[fd].in_use) {
    return -1;
  }

  if(is_fat32_fd(fd)) {
    fat32_file_t *fatfile = (fat32_file_t *)fd_table[fd].node;
    fatfile->position     = (u32)fd_table[fd].offset;
    i64 bytes             = fat32_read(fatfile, buf, count);
    if(bytes > 0) {
      fd_table[fd].offset += (u64)bytes;
    }
    return bytes;
  }

  vfs_fd_t   *f    = &fd_table[fd];
  const vfs_node_t *node = f->node;

  if(f->offset >= node->size) {
    return 0; /* EOF */
  }

  u64 available = node->size - f->offset;
  u64 to_read   = (count < available) ? count : available;

  kmemcpy(buf, node->data + f->offset, to_read);
  f->offset += to_read;

  return (i64)to_read;
}

/**
 * @brief Write data to an open file.
 * 
 * Writes up to count bytes to the file at the current offset.
 * Automatically expands the file buffer if necessary.
 * Handles both ramfs and FAT32 files.
 * 
 * @param fd File descriptor.
 * @param buf Source buffer.
 * @param count Number of bytes to write.
 * @return Number of bytes written, negative on error.
 */
i64 vfs_write(i64 fd, const void *buf, u64 count)
{
  if(fd < 0 || fd >= VFS_MAX_FD || !fd_table[fd].in_use) {
    return -1;
  }

  /* Handle FAT32 files */
  if(is_fat32_fd(fd)) {
    fat32_file_t *fatfile = (fat32_file_t *)fd_table[fd].node;

    /* Sync position from fd table to FAT32 file */
    fat32_seek(fatfile, (i64)fd_table[fd].offset, 0); /* SEEK_SET */

    i64 bytes = fat32_write(fatfile, buf, count);
    if(bytes > 0) {
      fd_table[fd].offset += (u64)bytes;
    }
    return bytes;
  }

  vfs_fd_t   *f    = &fd_table[fd];
  vfs_node_t *node = f->node;

  u64         end_pos = f->offset + count;

  /* Grow buffer if needed */
  if(end_pos > node->capacity) {
    u64 new_cap  = (end_pos < 4096) ? 4096 : (end_pos * 2);
    u8 *new_data = kmalloc(new_cap);
    if(!new_data)
      return -1;

    if(node->data) {
      kmemcpy(new_data, node->data, node->size);
      kfree(node->data);
    }

    node->data     = new_data;
    node->capacity = new_cap;
  }

  kmemcpy(node->data + f->offset, buf, count);
  f->offset += count;

  if(f->offset > node->size) {
    node->size = f->offset;
  }

  return (i64)count;
}

/**
 * @brief Seek to a position in an open file.
 * 
 * Changes the file offset according to whence parameter.
 * Handles both ramfs and FAT32 files.
 * 
 * @param fd File descriptor.
 * @param offset Offset to seek to (interpretation depends on whence).
 * @param whence Seek mode: SEEK_SET (absolute), SEEK_CUR (relative), SEEK_END (from end).
 * @return New file offset, negative on error.
 */
i64 vfs_seek(i64 fd, i64 offset, i32 whence)
{
  if(fd < 0 || fd >= VFS_MAX_FD || !fd_table[fd].in_use) {
    return -1;
  }

  /* Handle FAT32 files */
  if(is_fat32_fd(fd)) {
    fat32_file_t *fatfile = (fat32_file_t *)fd_table[fd].node;
    i64           new_pos = fat32_seek(fatfile, offset, whence);
    if(new_pos >= 0) {
      fd_table[fd].offset = (u64)new_pos;
    }
    return new_pos;
  }

  vfs_fd_t *f = &fd_table[fd];
  i64       new_offset;

  switch(whence) {
  case SEEK_SET:
    new_offset = offset;
    break;
  case SEEK_CUR:
    new_offset = (i64)f->offset + offset;
    break;
  case SEEK_END:
    new_offset = (i64)f->node->size + offset;
    break;
  default:
    return -1;
  }

  if(new_offset < 0) {
    return -1;
  }

  f->offset = (u64)new_offset;
  return new_offset;
}

/**
 * @brief Get file status information.
 * 
 * Returns file metadata including size, type, and timestamps.
 * Handles both ramfs and FAT32 files.
 * 
 * @param path Path to file or directory.
 * @param st Output buffer for stat structure.
 * @return 0 on success, negative on error.
 */
i64 vfs_stat(const char *path, vfs_stat_t *stat)
{
  /* Convert to absolute path for mount point detection */
  char abs_path[VFS_PATH_MAX];
  make_absolute_path(path, abs_path, VFS_PATH_MAX);

  /* Check if path is on a mounted filesystem */
  vfs_mount_t *mount = find_mount(abs_path);
  if(mount && mount->type == FS_TYPE_FAT32) {
    fat32_volume_t *vol      = mount->fs_data;
    const char     *rel_path = get_relative_path(abs_path, mount);

    fat32_entry_t   fatent;
    if(fat32_stat(vol, rel_path, &fatent) < 0) {
      return -1;
    }

    stat->size     = fatent.size;
    stat->type     = (fatent.attr & 0x10) ? VFS_DIRECTORY : VFS_FILE;
    stat->created  = 0;
    stat->modified = 0;
    return 0;
  }

  /* Ramfs path - use abs_path for consistency */
  const vfs_node_t *node = resolve_path(abs_path);
  if(!node)
    return -1;

  stat->size     = node->size;
  stat->type     = node->type;
  stat->created  = 0; /* TODO: timestamps */
  stat->modified = 0;

  return 0;
}

/**
 * @brief Create a new directory.
 * 
 * Creates a directory in ramfs. Parent directory must exist.
 * Returns error if directory already exists.
 * 
 * @param path Path to new directory.
 * @return 0 on success, negative on error.
 */
i64 vfs_mkdir(const char *path)
{
  /* Convert to absolute path */
  char abs_path[VFS_PATH_MAX];
  make_absolute_path(path, abs_path, VFS_PATH_MAX);

  /* Check if already exists */
  if(resolve_path(abs_path)) {
    return -1;
  }

  char        name[VFS_NAME_MAX];
  vfs_node_t *parent = resolve_parent(abs_path, name);

  if(!parent || parent->type != VFS_DIRECTORY) {
    return -1;
  }

  vfs_node_t *dir = create_node(name, VFS_DIRECTORY);
  if(!dir)
    return -1;

  add_child(parent, dir);
  return 0;
}

/**
 * @brief Open a directory for reading.
 * 
 * Opens a directory and returns a directory handle for use with vfs_readdir().
 * Handles both ramfs and FAT32 directories.
 * 
 * @param path Path to directory.
 * @return Directory handle on success, negative on error.
 */
i64 vfs_opendir(const char *path)
{
  /* Convert to absolute path for mount point detection */
  char abs_path[VFS_PATH_MAX];
  make_absolute_path(path, abs_path, VFS_PATH_MAX);

  /* Find free dirfd first */
  i64 dirfd = -1;
  for(i64 i = 0; i < VFS_MAX_FD; i++) {
    if(!dir_table[i].in_use) {
      dirfd = i;
      break;
    }
  }
  if(dirfd < 0)
    return -1;

  /* Check if path is on a mounted FAT32 filesystem */
  vfs_mount_t *mount = find_mount(abs_path);
  if(mount && mount->type == FS_TYPE_FAT32) {
    fat32_volume_t *vol      = mount->fs_data;
    const char     *rel_path = get_relative_path(abs_path, mount);

    fat32_file_t *fatdir = fat32_open(vol, rel_path);
    if(!fatdir) {
      return -1;
    }
    if(!fatdir->is_dir) {
      fat32_close(fatdir);
      return -1;
    }

    dir_table[dirfd].in_use   = true;
    dir_table[dirfd].node     = (vfs_node_t *)fatdir;
    dir_table[dirfd].current  = NULL;
    dir_table[dirfd].index    = 0;
    dir_table[dirfd].is_fat32 = true;

    return dirfd;
  }

  /* Ramfs path */
  vfs_node_t *node = resolve_path(abs_path);
  if(!node || node->type != VFS_DIRECTORY) {
    return -1;
  }

  dir_table[dirfd].in_use   = true;
  dir_table[dirfd].node     = node;
  dir_table[dirfd].current  = node->children;
  dir_table[dirfd].index    = 0;
  dir_table[dirfd].is_fat32 = false;

  return dirfd;
}

/**
 * @brief Read next directory entry.
 * 
 * Reads the next entry from an open directory. Returns 1 on success,
 * 0 when no more entries, negative on error.
 * 
 * @param dirfd Directory handle from vfs_opendir().
 * @param entry Output buffer for directory entry.
 * @return 1 on success, 0 at end, negative on error.
 */
i64 vfs_readdir(i64 dirfd, vfs_dirent_t *entry)
{
  if(dirfd < 0 || dirfd >= VFS_MAX_FD || !dir_table[dirfd].in_use) {
    return -1;
  }

  vfs_dir_t *d = &dir_table[dirfd];

  /* FAT32 directory */
  if(d->is_fat32) {
    fat32_file_t *fatdir = (fat32_file_t *)d->node;
    fat32_entry_t fatent;
    i64           ret = fat32_readdir(fatdir, &fatent);
    if(ret <= 0) {
      return ret;
    }

    kstrncpy(entry->name, fatent.name, VFS_NAME_MAX);
    entry->type = (fatent.attr & 0x10) ? VFS_DIRECTORY : VFS_FILE;
    entry->size = fatent.size;
    return 1;
  }

  /* Ramfs directory */
  if(!d->current) {
    return 0; /* End of directory */
  }

  kstrncpy(entry->name, d->current->name, VFS_NAME_MAX);
  entry->type = d->current->type;
  entry->size = d->current->size;

  d->current = d->current->next;
  return 1;
}

/**
 * @brief Close a directory handle
 * @param dirfd Directory handle from vfs_opendir()
 * @return 0 on success, negative on error
 */
i64 vfs_closedir(i64 dirfd)
{
  if(dirfd < 0 || dirfd >= VFS_MAX_FD || !dir_table[dirfd].in_use) {
    return -1;
  }

  vfs_dir_t *d = &dir_table[dirfd];

  /* Close FAT32 handle if needed */
  if(d->is_fat32 && d->node) {
    fat32_file_t *fatdir = (fat32_file_t *)d->node;
    fat32_close(fatdir);
  }

  /* Clear the entry */
  d->in_use   = false;
  d->node     = NULL;
  d->current  = NULL;
  d->index    = 0;
  d->is_fat32 = false;

  return 0;
}

/**
 * @brief Create an empty file (like Unix touch command).
 * 
 * Creates an empty file if it doesn't exist. Does nothing if file exists.
 * 
 * @param path Path to file.
 * @return 0 on success, negative on error.
 */
i64 vfs_touch(const char *path)
{
  /* Convert to absolute path */
  char abs_path[VFS_PATH_MAX];
  make_absolute_path(path, abs_path, VFS_PATH_MAX);

  /* If exists, just return success */
  if(resolve_path(abs_path)) {
    return 0;
  }

  /* Create empty file - vfs_open will handle the path */
  i64 fd = vfs_open(abs_path, O_CREAT | O_WRONLY);
  if(fd < 0)
    return -1;

  vfs_close(fd);
  return 0;
}

/**
 * @brief Remove/delete a file
 * @param path Path to file to remove
 * @return 0 on success, negative errno on error
 */
i64 vfs_unlink(const char *path)
{
  /* Convert to absolute path */
  char abs_path[VFS_PATH_MAX];
  make_absolute_path(path, abs_path, VFS_PATH_MAX);

  /* Check if path is on a mounted filesystem */
  vfs_mount_t *mount = find_mount(abs_path);
  if(mount && mount->type == FS_TYPE_FAT32) {
    const char *rel_path = get_relative_path(abs_path, mount);
    return fat32_unlink((fat32_volume_t *)mount->fs_data, rel_path);
  }

  /* Ramfs path */
  vfs_node_t *node = resolve_path(abs_path);
  if(!node) {
    return -ENOENT;
  }
  if(node->type != VFS_FILE) {
    return -EISDIR; /* Can't remove directories with unlink */
  }

  /* Remove from parent's children list */
  vfs_node_t *parent = node->parent;
  if(parent->children == node) {
    parent->children = node->next;
  } else {
    vfs_node_t *prev = parent->children;
    while(prev && prev->next != node) {
      prev = prev->next;
    }
    if(prev) {
      prev->next = node->next;
    }
  }

  /* Free node data */
  if(node->data) {
    kfree(node->data);
  }
  kfree(node);

  return 0;
}

/**
 * @brief Get current working directory
 * @return Pointer to static buffer containing current directory path
 */
const char *vfs_getcwd(void)
{
  return cwd;
}

/* Linux dirent structure for getdents syscall - must match kernel ABI */
struct linux_dirent
{
  u64  d_ino;    /* inode number */
  i64  d_off;    /* offset to next structure */
  u16  d_reclen; /* length of this record */
  u8   d_type;   /* file type */
  char d_name[]; /* flexible array for filename */
} __attribute__((packed));

/* DT_* types */
#define DT_UNKNOWN 0
#define DT_DIR     4
#define DT_REG     8

/**
 * @brief Get directory entries (Linux getdents syscall)
 * @param fd Directory file descriptor
 * @param buf Buffer to fill with dirent structures
 * @param count Size of buffer in bytes
 * @return Bytes written, 0 at end, negative on error
 */
i64 vfs_getdents(i64 fd, void *buf, u64 count)
{
  if(fd < 0 || fd >= VFS_MAX_FD || !fd_table[fd].in_use) {
    return -1;
  }

  vfs_fd_t *fde = &fd_table[fd];

  /* Must be opened with O_DIRECTORY */
  if(!(fde->flags & O_DIRECTORY)) {
    return -1;
  }

  u8 *out     = (u8 *)buf;
  u64 written = 0;

  /* Handle FAT32 directories */
  if(is_fat32_fd(fd)) {
    fat32_file_t *fatdir = (fat32_file_t *)fde->node;
    if(!fatdir || !fatdir->is_dir) {
      return -1;
    }

    fat32_entry_t fatent;
    while(written < count) {
      i64 ret = fat32_readdir(fatdir, &fatent);
      if(ret <= 0) {
        break; /* End of directory or error */
      }

      u64 namelen = kstrlen(fatent.name);
      u64 reclen = 8 + 8 + 2 + 1 + namelen + 1;
      reclen     = (reclen + 7) & ~7; /* Align to 8 bytes */

      if(written + reclen > count) {
        break; /* No more room */
      }

      u8 *p            = out + written;
      *(u64 *)p        = fatent.cluster;                             /* d_ino */
      *(i64 *)(p + 8)  = (i64)(fde->offset + 1);                     /* d_off */
      *(u16 *)(p + 16) = (u16)reclen;                                /* d_reclen */
      *(u8 *)(p + 18)  = (fatent.attr & 0x10) ? DT_DIR : DT_REG;     /* d_type */
      kstrncpy((char *)(p + 19), fatent.name, namelen + 1);          /* d_name */

      written += reclen;
      fde->offset++;
    }

    return (i64)written;
  }

  /* Ramfs directory */
  vfs_node_t *dir = fde->node;
  if(!dir || dir->type != VFS_DIRECTORY) {
    return -1;
  }

  /* Get current position in directory listing */
  u64         pos   = fde->offset;
  vfs_node_t *child = dir->children;

  /* Skip to current position */
  for(u64 i = 0; i < pos && child; i++) {
    child = child->next;
  }

  if(!child) {
    return 0; /* End of directory */
  }

  while(child && written < count) {
    /* Calculate record length (must be 8-byte aligned) */
    u64 namelen = kstrlen(child->name);
    /* Size: d_ino(8) + d_off(8) + d_reclen(2) + d_type(1) + name + null */
    u64 reclen = 8 + 8 + 2 + 1 + namelen + 1;
    reclen     = (reclen + 7) & ~7; /* Align to 8 bytes */

    if(written + reclen > count) {
      break; /* No more room */
    }

    /* Write fields manually to avoid struct padding issues */
    u8 *p            = out + written;
    *(u64 *)p        = (u64)(uintptr_t)child; /* d_ino */
    *(i64 *)(p + 8)  = (i64)(pos + 1);        /* d_off */
    *(u16 *)(p + 16) = (u16)reclen;           /* d_reclen */
    *(u8 *)(p + 18) =
        (child->type == VFS_DIRECTORY) ? DT_DIR : DT_REG; /* d_type */
    kstrncpy((char *)(p + 19), child->name, namelen + 1); /* d_name */

    written += reclen;
    fde->offset++;
    pos++;
    child = child->next;
  }

  return (i64)written;
}

/**
 * @brief Change current working directory.
 * 
 * Updates the process's current working directory to the specified path.
 * Handles both ramfs and FAT32 directories.
 * 
 * @param path Path to new directory (absolute or relative).
 * @return 0 on success, negative on error.
 */
i64 vfs_chdir(const char *path)
{
  /* Build absolute path first */
  char abs_path[VFS_PATH_MAX];
  make_absolute_path(path, abs_path, VFS_PATH_MAX);

  /* Check if on mounted filesystem */
  vfs_mount_t *mount = find_mount(abs_path);
  if(mount && mount->type == FS_TYPE_FAT32) {
    fat32_volume_t *vol      = mount->fs_data;
    const char     *rel_path = get_relative_path(abs_path, mount);

    /* Verify directory exists on FAT32 */
    fat32_file_t *dir = fat32_open(vol, rel_path);
    if(!dir || !dir->is_dir) {
      if(dir)
        fat32_close(dir);
      return -1;
    }
    fat32_close(dir);

    /* Update cwd to normalized absolute path */
    kstrncpy(cwd, abs_path, VFS_PATH_MAX);
    return 0;
  }

  /* Ramfs path - verify with resolve_path */
  const vfs_node_t *node = resolve_path(path);
  if(!node || node->type != VFS_DIRECTORY) {
    return -1;
  }

  /* Update cwd to normalized absolute path */
  kstrncpy(cwd, abs_path, VFS_PATH_MAX);
  return 0;
}

/**
 * @brief Check if path starts with a specific prefix.
 * @param path Path to check.
 * @param prefix Prefix to match.
 * @return true if path starts with prefix.
 */
static bool starts_with(const char *path, const char *prefix)
{
  /* Special case: root mount point "/" matches everything starting with "/" */
  if(prefix[0] == '/' && prefix[1] == '\0') {
    return path[0] == '/';
  }

  while(*prefix) {
    if(*path != *prefix)
      return false;
    path++;
    prefix++;
  }
  /* After prefix, must be / or end */
  return (*path == '\0' || *path == '/');
}

/**
 * @brief Find mount point for path
 * @param path Path to search for mount point
 * @return Mount point structure or NULL if path is on ramfs
 */
static vfs_mount_t *find_mount(const char *path)
{
  vfs_mount_t *best     = NULL;
  u64          best_len = 0;

  for(int i = 0; i < VFS_MAX_MOUNTS; i++) {
    if(!mounts[i].active)
      continue;

    u64 len = kstrlen(mounts[i].path);
    if(starts_with(path, mounts[i].path) && len > best_len) {
      best     = &mounts[i];
      best_len = len;
    }
  }

  return best;
}

/**
 * @brief Get path relative to mount point
 * @param path Absolute path
 * @param mount Mount point
 * @return Relative path within mounted filesystem
 */
static const char *get_relative_path(const char *path, const vfs_mount_t *mount)
{
  const char *rel = path + kstrlen(mount->path);
  if(*rel == '\0')
    return "/";
  return rel;
}

/**
 * @brief Parse device path to get drive number
 * @param source Device path (e.g., "/dev/hda")
 * @return Drive number (0-3), or 0 as default
 * 
 * Mappings:
 * - /dev/hda -> 0 (Primary Master)
 * - /dev/hdb -> 1 (Primary Slave)
 * - /dev/hdc -> 2 (Secondary Master)
 * - /dev/hdd -> 3 (Secondary Slave)
 */
static i32 parse_device_path(const char *source)
{
  if(!source)
    return 0; /* Default to drive 0 */

  /* Check for /dev/hdX format */
  if(source[0] == '/' && source[1] == 'd' && source[2] == 'e' &&
     source[3] == 'v' && source[4] == '/' && source[5] == 'h' &&
     source[6] == 'd') {
    char drive_letter = source[7];
    if(drive_letter >= 'a' && drive_letter <= 'd') {
      return drive_letter - 'a';
    }
  }

  /* Try to parse as raw number */
  if(source[0] >= '0' && source[0] <= '3') {
    return source[0] - '0';
  }

  return 0; /* Default */
}

/**
 * @brief Mount a filesystem
 * @param source Device path (e.g., "/dev/hda") or NULL
 * @param target Mount point path (must exist)
 * @param fstype Filesystem type ("fat32")
 * @return 0 on success, negative on error
 */
i64 vfs_mount(const char *source, const char *target, const char *fstype)
{
  if(!target || !fstype)
    return -1;

  /* Find free mount slot */
  int slot = -1;
  for(int i = 0; i < VFS_MAX_MOUNTS; i++) {
    if(!mounts[i].active) {
      slot = i;
      break;
    }
  }

  if(slot < 0) {
    console_printf("[vfs] mount: no free mount slots\n");
    return -1;
  }

  /* Create mount point directory in ramfs if needed */
  const vfs_node_t *mp = resolve_path(target);
  if(!mp) {
    if(vfs_mkdir(target) < 0) {
      console_printf("[vfs] mount: failed to create mount point %s\n", target);
      return -1;
    }
  }

  if(kstreq(fstype, "fat32")) {
    /* Parse device path (Linux-like) */
    i32 drive = parse_device_path(source);
    if(drive < 0) {
      console_printf("[vfs] mount: invalid device %s\n", source);
      return -1;
    }

    /* Mount FAT32 volume */
    fat32_volume_t *vol = fat32_mount((u8)drive, 0);
    if(!vol) {
      console_printf(
          "[vfs] mount: failed to mount FAT32 from drive %d\n", drive
      );
      return -1;
    }

    mounts[slot].type    = FS_TYPE_FAT32;
    mounts[slot].fs_data = vol;
    kstrncpy(mounts[slot].path, target, VFS_PATH_MAX);
    mounts[slot].active = true;

    console_printf(
        "[vfs] mounted %s (FAT32) on %s\n", source ? source : "/dev/hda", target
    );
    return 0;
  }

  if(kstreq(fstype, "ramfs")) {
    /* ramfs doesn't need special handling */
    mounts[slot].type    = FS_TYPE_RAMFS;
    mounts[slot].fs_data = NULL;
    kstrncpy(mounts[slot].path, target, VFS_PATH_MAX);
    mounts[slot].active = true;

    console_printf("[vfs] mounted ramfs at %s\n", target);
    return 0;
  }

  console_printf("[vfs] mount: unknown filesystem type '%s'\n", fstype);
  return -1;
}

/**
 * @brief Unmount a filesystem
 * @param target Mount point path to unmount
 * @return 0 on success, negative on error
 */
i64 vfs_umount(const char *target)
{
  if(!target)
    return -1;

  for(int i = 0; i < VFS_MAX_MOUNTS; i++) {
    if(mounts[i].active && kstreq(mounts[i].path, target)) {
      if(mounts[i].type == FS_TYPE_FAT32 && mounts[i].fs_data) {
        fat32_unmount(mounts[i].fs_data);
      }
      mounts[i].active = false;
      console_printf("[vfs] unmounted %s\n", target);
      return 0;
    }
  }

  return -1;
}
/**
 * @brief Check if file descriptor is a FAT32 file
 * @param fd File descriptor to check
 * @return true if FAT32 file, false if ramfs file
 */
static bool is_fat32_fd(i64 fd)
{
  return (fd_table[fd].flags & FAT32_FD_MAGIC) != 0;
}

/**
 * @brief Open file with FAT32 support
 * @param path File path
 * @param flags Open flags
 * @return File descriptor on success, negative on error
 * 
 * Note: FAT32 is read-only for now
 */
i64 vfs_open_fat32(const char *path, i32 flags)
{
  vfs_mount_t *mount = find_mount(path);

  if(!mount || mount->type != FS_TYPE_FAT32) {
    /* Use ramfs */
    return vfs_open(path, (u32)flags);
  }

  /* FAT32 path */
  fat32_volume_t *vol      = mount->fs_data;
  const char     *rel_path = get_relative_path(path, mount);

  /* Find free fd */
  i64 fd = -1;
  for(i64 i = 0; i < VFS_MAX_FD; i++) {
    if(!fd_table[i].in_use) {
      fd = i;
      break;
    }
  }
  if(fd < 0)
    return -1;

  /* Open FAT32 file */
  fat32_file_t *file = fat32_open(vol, rel_path);
  if(!file)
    return -1;

  fd_table[fd].in_use = true;
  fd_table[fd].node   = (vfs_node_t *)file; /* Store FAT32 file pointer */
  fd_table[fd].offset = 0;
  fd_table[fd].flags  = (u32)flags | FAT32_FD_MAGIC; /* Mark as FAT32 */

  return fd;
}

/**
 * @brief Read from file with FAT32 support
 * @param fd File descriptor
 * @param buf Buffer to read into
 * @param count Number of bytes to read
 * @return Bytes read on success, negative on error
 */
i64 vfs_read_fat32(i64 fd, void *buf, u64 count)
{
  if(fd < 0 || fd >= VFS_MAX_FD || !fd_table[fd].in_use) {
    return -1;
  }

  if(is_fat32_fd(fd)) {
    fat32_file_t *fatfile = (fat32_file_t *)fd_table[fd].node;
    /* Seek to current offset first */
    fatfile->position = (u32)fd_table[fd].offset;
    i64 bytes         = fat32_read(fatfile, buf, count);
    if(bytes > 0) {
      fd_table[fd].offset += (u64)bytes;
    }
    return bytes;
  }

  /* Use ramfs */
  return vfs_read(fd, buf, count);
}

/**
 * @brief Write to file with FAT32 support
 * @param fd File descriptor
 * @param buf Buffer to write from
 * @param count Number of bytes to write
 * @return Bytes written on success, negative on error
 */
i64 vfs_write_fat32(i64 fd, const void *buf, u64 count)
{
  if(fd < 0 || fd >= VFS_MAX_FD || !fd_table[fd].in_use) {
    return -1;
  }

  if(is_fat32_fd(fd)) {
    fat32_file_t *fatfile = (fat32_file_t *)fd_table[fd].node;
    /* Seek to current offset first */
    fat32_seek(fatfile, (i64)fd_table[fd].offset, 0); /* SEEK_SET */
    i64 bytes = fat32_write(fatfile, buf, count);
    if(bytes > 0) {
      fd_table[fd].offset += (u64)bytes;
    }
    return bytes;
  }

  /* Use ramfs */
  return vfs_write(fd, buf, count);
}

/**
 * @brief Close file with FAT32 support
 * @param fd File descriptor to close
 * @return 0 on success, negative on error
 */
i64 vfs_close_fat32(i64 fd)
{
  if(fd < 0 || fd >= VFS_MAX_FD || !fd_table[fd].in_use) {
    return -1;
  }

  if(is_fat32_fd(fd)) {
    fat32_file_t *fatfile = (fat32_file_t *)fd_table[fd].node;
    fat32_flush(fatfile);
    fat32_close(fatfile);
    fd_table[fd].in_use = false;
    return 0;
  }

  return vfs_close(fd);
}

/**
 * @brief Get file status with FAT32 support
 * @param path File path
 * @param st Output buffer for file statistics
 * @return 0 on success, negative on error
 */
i64 vfs_stat_fat32(const char *path, vfs_stat_t *st)
{
  vfs_mount_t *mount = find_mount(path);

  if(!mount || mount->type != FS_TYPE_FAT32) {
    return vfs_stat(path, st);
  }

  fat32_volume_t *vol      = mount->fs_data;
  const char     *rel_path = get_relative_path(path, mount);

  fat32_entry_t   fatent;
  if(fat32_stat(vol, rel_path, &fatent) < 0) {
    return -1;
  }

  /* Convert FAT32 entry to VFS stat */
  st->size     = fatent.size;
  st->type     = (fatent.attr & 0x10) ? VFS_DIRECTORY : VFS_FILE;
  st->created  = 0;
  st->modified = 0;

  return 0;
}

/**
 * @brief Open directory with FAT32 support
 * @param path Directory path
 * @return Directory handle on success, negative on error
 */
i64 vfs_opendir_fat32(const char *path)
{
  vfs_mount_t *mount = find_mount(path);

  if(!mount || mount->type != FS_TYPE_FAT32) {
    return vfs_opendir(path);
  }

  fat32_volume_t *vol      = mount->fs_data;
  const char     *rel_path = get_relative_path(path, mount);

  /* Find free dirfd */
  i64 dirfd = -1;
  for(i64 i = 0; i < VFS_MAX_FD; i++) {
    if(!dir_table[i].in_use) {
      dirfd = i;
      break;
    }
  }
  if(dirfd < 0)
    return -1;

  /* Open FAT32 directory using fat32_open */
  fat32_file_t *dir = fat32_open(vol, rel_path);
  if(!dir || !dir->is_dir) {
    if(dir)
      fat32_close(dir);
    return -1;
  }

  dir_table[dirfd].in_use  = true;
  dir_table[dirfd].node    = (vfs_node_t *)dir;
  dir_table[dirfd].current = NULL;
  dir_table[dirfd].index    = 0;
  dir_table[dirfd].is_fat32 = true;

  return dirfd;
}

/**
 * @brief Read directory entry with FAT32 support
 * @param dirfd Directory handle
 * @param entry Output buffer for directory entry
 * @return 1 if entry read, 0 at end, negative on error
 */
i64 vfs_readdir_fat32(i64 dirfd, vfs_dirent_t *entry)
{
  /* Just use the unified vfs_readdir */
  return vfs_readdir(dirfd, entry);
}

/**
 * @brief Close directory with FAT32 support
 * @param dirfd Directory handle to close
 * @return 0 on success, negative on error
 */
i64 vfs_closedir_fat32(i64 dirfd)
{
  /* Just use the unified vfs_closedir */
  return vfs_closedir(dirfd);
}

/**
 * @brief Close all FDs owned by a specific PID.
 * 
 * Called by proc_exit to clean up resources.
 * 
 * @param pid Process ID.
 */
void vfs_close_for_pid(u64 pid)
{
  if(pid == 0) return;

  for(int i = 0; i < VFS_MAX_FD; i++) {
    if(fd_table[i].in_use && fd_table[i].owner_pid == pid) {
      /* Skip standard streams if they are shared/system-wide? 
       * Ideally stdin/out/err are per-process but here FDs are global.
       * Only close if actually owned by this PID.
       */
      /* console_printf("[VFS] Closing leaked FD %d for PID %d\n", i, (int)pid); */
      vfs_close(i);
    }
  }
}
