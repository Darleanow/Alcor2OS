/**
 * @file src/fs/vfs.c
 * @brief Virtual File System (ramfs + mountable filesystems).
 */

#include <alcor2/drivers/console.h>
#include <alcor2/errno.h>
#include <alcor2/fs/ext2.h>
#include <alcor2/fs/vfs.h>
#include <alcor2/kstdlib.h>
#include <alcor2/mm/heap.h>
#include <alcor2/proc/proc.h>
#include <alcor2/sys/internal.h>

#define VFS_MAX_MOUNTS  8
#define VFS_MAX_FSTYPES 8

/** @brief Mount point descriptor. */
typedef struct
{
  void            *fs_data;            /**< FS-specific data */
  const fs_ops_t  *ops;                /**< Filesystem operations */
  bool             active;             /**< Mount active */
  char             path[VFS_PATH_MAX]; /**< Mount point path */
  const fs_type_t *fstype;             /**< Filesystem type (for unmount) */
} vfs_mount_t;

static vfs_mount_t mounts[VFS_MAX_MOUNTS];

/** @brief Registered filesystem types */
static const fs_type_t *fs_types[VFS_MAX_FSTYPES];
static int              fs_type_count = 0;

static vfs_node_t      *root_node = NULL;
static vfs_fd_t         fd_table[VFS_MAX_FD];
static vfs_dir_t        dir_table[VFS_MAX_FD];
static char             cwd[VFS_PATH_MAX] = "/";

static vfs_mount_t     *find_mount(const char *path);
static const char *
    get_relative_path(const char *path, const vfs_mount_t *mount);

/**
 * @brief True if the OFT entry refers to a mounted filesystem (vs ramfs).
 */
static inline bool is_mounted_oft(i32 oft_idx)
{
  return fd_table[oft_idx].ops != NULL;
}

/* ====================================================================== */
/*  Open file table + per-process fd table                                */
/* ====================================================================== */

/**
 * @brief Allocate a free slot in the open file table, refcount=1, kind=FILE.
 * @return Index, or -ENFILE.
 */
static i32 oft_alloc_file(void)
{
  for(i32 i = 0; i < VFS_MAX_FD; i++) {
    if(!fd_table[i].in_use) {
      fd_table[i].in_use   = true;
      fd_table[i].kind     = VFS_FD_FILE;
      fd_table[i].refcount = 1;
      fd_table[i].node     = NULL;
      fd_table[i].ops      = NULL;
      fd_table[i].pipe     = NULL;
      fd_table[i].offset   = 0;
      fd_table[i].flags    = 0;
      fd_table[i].st_dev   = 0;
      return i;
    }
  }
  return -ENFILE;
}

i32 vfs_oft_alloc_pipe(i32 kind, void *pipe)
{
  for(i32 i = 0; i < VFS_MAX_FD; i++) {
    if(!fd_table[i].in_use) {
      fd_table[i].in_use   = true;
      fd_table[i].kind     = kind;
      fd_table[i].refcount = 1;
      fd_table[i].node     = NULL;
      fd_table[i].ops      = NULL;
      fd_table[i].pipe     = pipe;
      fd_table[i].offset   = 0;
      fd_table[i].flags    = 0;
      fd_table[i].st_dev   = 0;
      return i;
    }
  }
  return -ENFILE;
}

void vfs_oft_retain(i32 oft_idx)
{
  if(oft_idx < 0 || oft_idx >= VFS_MAX_FD || !fd_table[oft_idx].in_use)
    return;
  fd_table[oft_idx].refcount++;
  if(fd_table[oft_idx].pipe)
    pipe_oft_retain(fd_table[oft_idx].kind, fd_table[oft_idx].pipe);
}

void vfs_oft_release(i32 oft_idx)
{
  if(oft_idx < 0 || oft_idx >= VFS_MAX_FD || !fd_table[oft_idx].in_use)
    return;

  /* Notify the pipe on every close so read_open/write_open accurately track
   * how many fd-holders remain across all processes (fork bumps the count
   * via vfs_oft_retain; each close decrements it). */
  if(fd_table[oft_idx].pipe)
    pipe_oft_release(fd_table[oft_idx].kind, fd_table[oft_idx].pipe);

  if(--fd_table[oft_idx].refcount > 0)
    return;

  switch(fd_table[oft_idx].kind) {
  case VFS_FD_FILE:
    if(fd_table[oft_idx].ops) {
      fs_file_t fh = (fs_file_t)fd_table[oft_idx].node;
      if(fd_table[oft_idx].ops->flush)
        fd_table[oft_idx].ops->flush(fh);
      fd_table[oft_idx].ops->close(fh);
    }
    break;
  case VFS_FD_PIPE_READ:
  case VFS_FD_PIPE_WRITE:
    /* pipe_oft_release already called above for every close */
    break;
  }
  fd_table[oft_idx].in_use = false;
  fd_table[oft_idx].ops    = NULL;
  fd_table[oft_idx].node   = NULL;
  fd_table[oft_idx].pipe   = NULL;
}

/**
 * @brief Translate a per-process fd to an OFT index.
 * @return OFT index, or -1 if @p fd is closed or invalid.
 */
static i32 fd_to_oft(i64 fd)
{
  proc_t *p = proc_current();
  if(!p || fd < 0 || fd >= VFS_MAX_FD)
    return -1;
  i32 idx = p->fds[fd];
  if(idx < 0 || idx >= VFS_MAX_FD || !fd_table[idx].in_use)
    return -1;
  return idx;
}

i32 vfs_select_read_ready(i64 fd)
{
  i32 oft = fd_to_oft(fd);
  if(oft < 0)
    return -EBADF;
  vfs_fd_t *f = &fd_table[oft];
  if(f->kind == VFS_FD_PIPE_WRITE)
    return -EBADF;
  if(f->kind == VFS_FD_PIPE_READ)
    return pipe_poll_read_ready(f->pipe) ? 1 : 0;
  return 1;
}

i32 vfs_select_write_ready(i64 fd)
{
  i32 oft = fd_to_oft(fd);
  if(oft < 0)
    return -EBADF;
  vfs_fd_t *f = &fd_table[oft];
  if(f->kind == VFS_FD_PIPE_READ)
    return -EBADF;
  if(f->kind == VFS_FD_PIPE_WRITE)
    return pipe_poll_write_ready(f->pipe) ? 1 : 0;
  return 1;
}

/**
 * @brief Install @p oft_idx at the lowest free fd >= 3 in the current
 * process's fd table.
 * @return The fd, or -EMFILE.
 */
static i64 proc_install_fd_from(int start, i32 oft_idx)
{
  proc_t *p = proc_current();
  if(!p)
    return -EINVAL;
  for(i64 i = start; i < VFS_MAX_FD; i++) {
    if(p->fds[i] < 0) {
      p->fds[i] = oft_idx;
      return i;
    }
  }
  return -EMFILE;
}

i64 vfs_install_fd(i32 oft_idx)
{
  /* Start at 3 so newly opened resources (pipes, etc.) never shadow stdio
   * slots. Without this, in a shell that hasn't explicitly opened fds 0/1/2
   * (relies on the kernel's stdio fallback), pipe() returns fds 0 and 1 —
   * then `dup2(pipe_write, 1)` is a no-op and the subsequent close(1) drops
   * the pipe end. */
  return proc_install_fd_from(3, oft_idx);
}

void vfs_proc_init_fds(i32 *fds)
{
  for(int i = 0; i < VFS_MAX_FD; i++)
    fds[i] = -1;
}

void vfs_proc_inherit_fds(
    i32 *child_fds, u8 *child_cloexec, const i32 *parent_fds,
    const u8 *parent_cloexec
)
{
  for(int i = 0; i < VFS_MAX_FD; i++) {
    child_fds[i]     = parent_fds[i];
    child_cloexec[i] = parent_cloexec[i];
    if(child_fds[i] >= 0)
      vfs_oft_retain(child_fds[i]);
  }
}

void vfs_proc_release_fds(i32 *fds)
{
  for(int i = 0; i < VFS_MAX_FD; i++) {
    if(fds[i] >= 0) {
      vfs_oft_release(fds[i]);
      fds[i] = -1;
    }
  }
}

void vfs_proc_close_cloexec_fds(void)
{
  proc_t *p = proc_current();
  if(!p)
    return;
  for(int i = 0; i < VFS_MAX_FD; i++) {
    if(p->fd_cloexec[i] && p->fds[i] >= 0) {
      vfs_oft_release(p->fds[i]);
      p->fds[i]        = -1;
      p->fd_cloexec[i] = 0;
    }
  }
}

/**
 * @brief Normalize a path by resolving . and .. components.
 * @param path Path to normalize (modified in place).
 */
static void normalize_path(char *path)
{
  if(!path || path[0] != '/')
    return;

  /* Work buffer to build normalized path */
  char  result[VFS_PATH_MAX];
  char *out = result;
  char *p   = path + 1; /* Skip initial '/' */

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
    if(out && out_size > 0)
      out[0] = '\0';
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
 * Splits a path into its parent directory node and the final filename
 * component. Handles both absolute and relative paths.
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
 * Links the child into the parent's children list and sets the child's parent
 * pointer.
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
 * and sets up the initial mount table.
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
 * Opens a file in ramfs or mounted filesystem. Supports creation,
 * truncation, and various access modes. File descriptors 0-2 are reserved for
 * stdin/stdout/stderr.
 *
 * @param path Path to file (absolute or relative).
 * @param flags Open flags (O_RDONLY, O_WRONLY, O_RDWR, O_CREAT, O_TRUNC, etc.).
 * @return File descriptor on success, negative on error.
 */
/* Helper: install an opened file/dir in a freshly allocated OFT slot, then
 * publish at the lowest free fd >= 3 in the current process's fd table. */
static i64 publish_open(
    vfs_node_t *node, fs_file_t fh, const fs_ops_t *ops, u64 offset, u32 flags,
    u64 st_dev
)
{
  i32 oft = oft_alloc_file();
  if(oft < 0)
    return -ENFILE;
  fd_table[oft].node   = ops ? (vfs_node_t *)fh : node;
  fd_table[oft].ops    = ops;
  fd_table[oft].offset = offset;
  fd_table[oft].flags =
      flags & ~(u32)O_CLOEXEC; /* cloexec is per-fd, not per-OFT */
  fd_table[oft].st_dev = st_dev;

  i64 fd = proc_install_fd_from(3, oft);
  if(fd < 0) {
    vfs_oft_release(oft);
    return fd;
  }
  proc_t *p = proc_current();
  if(p)
    p->fd_cloexec[fd] = (flags & O_CLOEXEC) ? 1 : 0;
  return fd;
}

i64 vfs_open(const char *path, u32 flags)
{
  char abs_path[VFS_PATH_MAX];
  make_absolute_path(path, abs_path, VFS_PATH_MAX);

  vfs_mount_t *mount = find_mount(abs_path);
  if(mount && mount->ops) {
    const char *rel_path = get_relative_path(abs_path, mount);

    fs_file_t   fh     = NULL;
    bool        is_dir = false;

    if(flags & O_CREAT) {
      fh = mount->ops->create(mount->fs_data, rel_path);
      if(fh && mount->ops->is_dir)
        is_dir = mount->ops->is_dir(fh);
    } else {
      fh = mount->ops->open(mount->fs_data, rel_path, flags, &is_dir);
    }
    /* Must return negative errno (never plain -1): musl maps raw -1 to
     * errno=EPERM. */
    if(!fh)
      return -ENOENT;

    if((flags & O_TRUNC) && mount->ops->truncate)
      mount->ops->truncate(fh, 0);
    if((flags & O_APPEND) && mount->ops->seek)
      mount->ops->seek(fh, 0, SEEK_END);

    u64 off = mount->ops->get_position ? mount->ops->get_position(fh) : 0;
    return publish_open(
        NULL, fh, mount->ops, off, flags, (u64)(mount - mounts) + 1
    );
  }

  vfs_node_t *node = resolve_path(abs_path);

  if(!node && (flags & O_CREAT)) {
    char        name[VFS_NAME_MAX];
    vfs_node_t *parent = resolve_parent(abs_path, name);
    if(!parent || parent->type != VFS_DIRECTORY)
      return parent ? -ENOTDIR : -ENOENT;
    node = create_node(name, VFS_FILE);
    if(!node)
      return -ENOMEM;
    add_child(parent, node);
  }

  if(!node || node->type != VFS_FILE) {
    if(node && node->type == VFS_DIRECTORY && (flags & O_DIRECTORY))
      return publish_open(node, NULL, NULL, 0, flags, VFS_RAMFS_ST_DEV);
    if(node && node->type == VFS_DIRECTORY)
      return -EISDIR;
    return -ENOENT;
  }

  u64 off = (flags & O_APPEND) ? node->size : 0;
  if(flags & O_TRUNC)
    node->size = 0;
  return publish_open(node, NULL, NULL, off, flags, VFS_RAMFS_ST_DEV);
}

/**
 * @brief Close a file descriptor.
 *
 * Closes an open file descriptor and marks it as available for reuse.
 * For mounted filesystem files, flushes changes and calls the close handler.
 *
 * @param fd File descriptor to close.
 * @return 0 on success, negative on error.
 */
i64 vfs_close(i64 fd)
{
  proc_t *p = proc_current();
  if(!p || fd < 0 || fd >= VFS_MAX_FD)
    return -EBADF;
  i32 oft = p->fds[fd];
  if(oft < 0)
    return -EBADF;
  vfs_oft_release(oft);
  p->fds[fd]        = -1;
  p->fd_cloexec[fd] = 0;
  return 0;
}

/**
 * @brief Read data from an open file.
 *
 * Reads up to count bytes from the file at the current offset.
 * Handles both ramfs and mounted filesystem files.
 *
 * @param fd File descriptor.
 * @param buf Destination buffer.
 * @param count Maximum bytes to read.
 * @return Number of bytes read, 0 on EOF, negative on error.
 */
i64 vfs_read(i64 fd, void *buf, u64 count)
{
  i32 oft = fd_to_oft(fd);
  if(oft < 0)
    return -EBADF;

  vfs_fd_t *f = &fd_table[oft];

  if(f->kind == VFS_FD_PIPE_READ)
    return pipe_read_obj(f->pipe, buf, count);
  if(f->kind == VFS_FD_PIPE_WRITE)
    return -EBADF;

  if(is_mounted_oft(oft)) {
    fs_file_t fh = (fs_file_t)f->node;
    if(f->ops->seek)
      f->ops->seek(fh, (i64)f->offset, SEEK_SET);
    i64 bytes = f->ops->read(fh, buf, count);
    if(bytes > 0)
      f->offset += (u64)bytes;
    return bytes;
  }

  const vfs_node_t *node = f->node;
  if(f->offset >= node->size)
    return 0;

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
 * Handles both ramfs and mounted filesystem files.
 *
 * @param fd File descriptor.
 * @param buf Source buffer.
 * @param count Number of bytes to write.
 * @return Number of bytes written, negative on error.
 */
i64 vfs_write(i64 fd, const void *buf, u64 count)
{
  i32 oft = fd_to_oft(fd);
  if(oft < 0)
    return -EBADF;

  vfs_fd_t *f = &fd_table[oft];

  if(f->kind == VFS_FD_PIPE_WRITE)
    return pipe_write_obj(f->pipe, buf, count);
  if(f->kind == VFS_FD_PIPE_READ)
    return -EBADF;

  if(is_mounted_oft(oft)) {
    fs_file_t fh = (fs_file_t)f->node;
    if(f->ops->seek)
      f->ops->seek(fh, (i64)f->offset, SEEK_SET);
    i64 bytes = f->ops->write(fh, buf, count);
    if(bytes > 0)
      f->offset += (u64)bytes;
    return bytes;
  }

  vfs_node_t *node    = f->node;
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
 * Handles both ramfs and mounted filesystem files.
 *
 * @param fd File descriptor.
 * @param offset Offset to seek to (interpretation depends on whence).
 * @param whence Seek mode: SEEK_SET (absolute), SEEK_CUR (relative), SEEK_END
 * (from end).
 * @return New file offset, negative on error.
 */
i64 vfs_seek(i64 fd, i64 offset, i32 whence)
{
  i32 oft = fd_to_oft(fd);
  if(oft < 0)
    return -EBADF;

  if(fd_table[oft].kind != VFS_FD_FILE)
    return -ESPIPE;

  if(is_mounted_oft(oft)) {
    fs_file_t fh      = (fs_file_t)fd_table[oft].node;
    i64       new_pos = fd_table[oft].ops->seek(fh, offset, whence);
    if(new_pos >= 0)
      fd_table[oft].offset = (u64)new_pos;
    return new_pos;
  }

  vfs_fd_t *f = &fd_table[oft];
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
 * Handles both ramfs and mounted filesystem files.
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

  /* Check if path is on a mounted filesystem with ops */
  const vfs_mount_t *mount = find_mount(abs_path);
  if(mount && mount->ops && mount->ops->stat) {
    const char *rel_path = get_relative_path(abs_path, mount);
    u64         size;
    u8          type;
    u64         ino = 0;
    if(mount->ops->stat(mount->fs_data, rel_path, &size, &type, &ino) < 0) {
      return -1;
    }

    stat->size     = size;
    stat->type     = type;
    stat->created  = 0;
    stat->modified = 0;
    stat->dev      = (u64)(mount - mounts) + 1;
    stat->ino      = ino;
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
  stat->dev      = VFS_RAMFS_ST_DEV;
  stat->ino      = (u64)(uintptr_t)node;

  return 0;
}

/**
 * @brief Create a new directory.
 *
 * Creates a directory in ramfs or mounted filesystem. Parent directory must
 * exist. Returns error if directory already exists.
 *
 * @param path Path to new directory.
 * @return 0 on success, negative on error.
 */
i64 vfs_mkdir(const char *path)
{
  /* Convert to absolute path */
  char abs_path[VFS_PATH_MAX];
  make_absolute_path(path, abs_path, VFS_PATH_MAX);

  /* Check if on mounted filesystem with ops */
  vfs_mount_t *mount = find_mount(abs_path);
  if(mount && mount->ops && mount->ops->mkdir) {
    const char *rel_path = get_relative_path(abs_path, mount);
    return mount->ops->mkdir(mount->fs_data, rel_path);
  }

  /* Check if already exists in ramfs */
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
 * Handles both ramfs and mounted filesystem directories.
 *
 * @param path Path to directory.
 * @return Directory handle on success, negative on error.
 */
// cppcheck-suppress unusedFunction
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

  /* Check if path is on a mounted filesystem with ops */
  vfs_mount_t *mount = find_mount(abs_path);
  if(mount && mount->ops && mount->ops->open) {
    const char *rel_path = get_relative_path(abs_path, mount);
    bool        is_dir   = false;
    fs_file_t   fh = mount->ops->open(mount->fs_data, rel_path, 0, &is_dir);
    if(!fh) {
      return -1;
    }
    if(!is_dir) {
      mount->ops->close(fh);
      return -1;
    }

    dir_table[dirfd].in_use  = true;
    dir_table[dirfd].node    = (vfs_node_t *)fh;
    dir_table[dirfd].current = NULL;
    dir_table[dirfd].index   = 0;
    dir_table[dirfd].ops     = mount->ops;

    return dirfd;
  }

  /* Ramfs path */
  vfs_node_t *node = resolve_path(abs_path);
  if(!node || node->type != VFS_DIRECTORY) {
    return -1;
  }

  dir_table[dirfd].in_use  = true;
  dir_table[dirfd].node    = node;
  dir_table[dirfd].current = node->children;
  dir_table[dirfd].index   = 0;
  dir_table[dirfd].ops     = NULL;

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
// cppcheck-suppress unusedFunction
i64 vfs_readdir(i64 dirfd, vfs_dirent_t *entry)
{
  if(dirfd < 0 || dirfd >= VFS_MAX_FD || !dir_table[dirfd].in_use) {
    return -1;
  }

  vfs_dir_t *d = &dir_table[dirfd];

  /* Mounted filesystem directory (has ops) */
  if(d->ops && d->ops->readdir) {
    fs_file_t fh = (fs_file_t)d->node;
    u64       inode;
    i64       ret =
        d->ops->readdir(fh, entry->name, &entry->type, &entry->size, &inode);
    return ret;
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
// cppcheck-suppress unusedFunction
i64 vfs_closedir(i64 dirfd)
{
  if(dirfd < 0 || dirfd >= VFS_MAX_FD || !dir_table[dirfd].in_use) {
    return -1;
  }

  vfs_dir_t *d = &dir_table[dirfd];

  /* Close mounted FS handle if needed */
  if(d->ops && d->ops->close && d->node) {
    d->ops->close((fs_file_t)d->node);
  }

  /* Clear the entry */
  d->in_use  = false;
  d->node    = NULL;
  d->current = NULL;
  d->index   = 0;
  d->ops     = NULL;

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

  /* Check if path is on a mounted filesystem with ops */
  const vfs_mount_t *mount = find_mount(abs_path);
  if(mount && mount->ops && mount->ops->unlink) {
    const char *rel_path = get_relative_path(abs_path, mount);
    return mount->ops->unlink(mount->fs_data, rel_path);
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
 * @brief Remove an empty directory.
 * @param path Path to directory to remove.
 * @return 0 on success, negative errno on error.
 */
i64 vfs_rmdir(const char *path)
{
  /* Convert to absolute path */
  char abs_path[VFS_PATH_MAX];
  make_absolute_path(path, abs_path, VFS_PATH_MAX);

  /* Check if path is on a mounted filesystem with ops */
  const vfs_mount_t *mount = find_mount(abs_path);
  if(mount && mount->ops && mount->ops->rmdir) {
    const char *rel_path = get_relative_path(abs_path, mount);
    return mount->ops->rmdir(mount->fs_data, rel_path);
  }

  /* Ramfs path */
  vfs_node_t *node = resolve_path(abs_path);
  if(!node) {
    return -ENOENT;
  }
  if(node->type != VFS_DIRECTORY) {
    return -ENOTDIR;
  }
  if(node->children != NULL) {
    return -ENOTEMPTY;
  }
  if(node == root_node) {
    return -EINVAL;
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

  kfree(node);
  return 0;
}

/**
 * @brief Duplicate a file descriptor to the lowest free slot (>= 3).
 *
 * Both the old and the new descriptor share the same node pointer and
 * start with the same offset (POSIX: they share the open file description).
 *
 * @param oldfd Source file descriptor.
 * @return New fd on success, -EBADF / -EMFILE on error.
 */
i64 vfs_dup(i64 oldfd)
{
  i32 oft = fd_to_oft(oldfd);
  if(oft < 0)
    return -EBADF;

  vfs_oft_retain(oft);
  i64 new_fd = proc_install_fd_from(0, oft);
  if(new_fd < 0) {
    vfs_oft_release(oft);
    return new_fd;
  }
  /* dup always clears FD_CLOEXEC on the new descriptor (POSIX). */
  proc_t *p = proc_current();
  if(p)
    p->fd_cloexec[new_fd] = 0;
  return new_fd;
}

/**
 * @brief Duplicate a file descriptor to a specific slot.
 *
 * Closes newfd first if it is already open. If oldfd == newfd, returns
 * immediately without any side-effects.
 *
 * @param oldfd Source file descriptor.
 * @param newfd Target file descriptor.
 * @return newfd on success, -EBADF on error.
 */
i64 vfs_dup2(i64 oldfd, i64 newfd)
{
  i32 oft = fd_to_oft(oldfd);
  if(oft < 0)
    return -EBADF;
  if(newfd < 0 || newfd >= VFS_MAX_FD)
    return -EBADF;
  if(oldfd == newfd)
    return newfd;

  proc_t *p = proc_current();
  if(!p)
    return -EINVAL;

  if(p->fds[newfd] >= 0)
    vfs_oft_release(p->fds[newfd]);

  vfs_oft_retain(oft);
  p->fds[newfd] = oft;
  p->fd_cloexec[newfd] =
      0; /* dup2 always clears FD_CLOEXEC on newfd (POSIX). */
  return newfd;
}

/**
 * @brief Get file statistics from an open file descriptor.
 *
 * For ramfs files the node size and type are returned directly.
 * For mounted-filesystem files the file size is obtained by seeking to
 * the end and back.
 *
 * @param fd   Open file descriptor.
 * @param st   Output stat buffer.
 * @return 0 on success, -EBADF on error.
 */
i64 vfs_fstat_fd(i64 fd, vfs_stat_t *st)
{
  i32 oft = fd_to_oft(fd);
  if(oft < 0)
    return -EBADF;

  if(is_mounted_oft(oft)) {
    fs_file_t fh = (fs_file_t)fd_table[oft].node;
    if(fd_table[oft].ops->fstat) {
      i64 r = fd_table[oft].ops->fstat(fh, st);
      if(r < 0)
        return r;
      st->dev = fd_table[oft].st_dev;
      return 0;
    }

    u64 saved   = fd_table[oft].offset;
    i64 end_pos = 0;

    if(fd_table[oft].ops->seek) {
      end_pos = fd_table[oft].ops->seek(fh, 0, SEEK_END);
      fd_table[oft].ops->seek(fh, (i64)saved, SEEK_SET);
    }

    st->size     = (end_pos >= 0) ? (u64)end_pos : 0;
    st->type     = (fd_table[oft].ops->is_dir && fd_table[oft].ops->is_dir(fh))
                       ? VFS_DIRECTORY
                       : VFS_FILE;
    st->created  = 0;
    st->modified = 0;
    st->ino      = 0;
    st->dev      = fd_table[oft].st_dev;
    return 0;
  }

  const vfs_node_t *node = fd_table[oft].node;
  st->size               = node->size;
  st->type               = node->type;
  st->created            = 0;
  st->modified           = 0;
  st->ino                = (u64)(uintptr_t)node;
  st->dev                = fd_table[oft].st_dev;
  return 0;
}

/**
 * @brief Return the open flags of a file descriptor.
 * @param fd File descriptor.
 * @return Flags on success, -EBADF on error.
 */
i64 vfs_get_flags(i64 fd)
{
  i32 oft = fd_to_oft(fd);
  if(oft < 0)
    return -EBADF;
  return (i64)fd_table[oft].flags;
}

/**
 * @brief Set the open flags of a file descriptor (O_APPEND, O_NONBLOCK…).
 *
 * Only flags that make sense to change after open are updated;
 * O_RDONLY / O_WRONLY / O_RDWR bits are preserved from the original.
 *
 * @param fd    File descriptor.
 * @param flags Replacement flags.
 * @return 0 on success, -EBADF on error.
 */
i64 vfs_set_flags(i64 fd, u32 flags)
{
  i32 oft = fd_to_oft(fd);
  if(oft < 0)
    return -EBADF;
  u32 access          = fd_table[oft].flags & O_RDWR;
  fd_table[oft].flags = access | (flags & ~(u32)O_RDWR);
  return 0;
}

/**
 * @brief Truncate an open file to the given length.
 *
 * For ramfs files: sets node->size.  Extension with zero-fill is not
 * yet supported (returns -ENOSYS when length > current size).
 * For mounted-FS files: only length == 0 is supported via ops->truncate.
 *
 * @param fd     Open, writable file descriptor.
 * @param length Target length.
 * @return 0 on success, negative errno on error.
 */
i64 vfs_ftruncate(i64 fd, i64 length)
{
  i32 oft = fd_to_oft(fd);
  if(oft < 0)
    return -EBADF;
  if(length < 0)
    return -EINVAL;

  if(is_mounted_oft(oft)) {
    if(fd_table[oft].ops->truncate)
      return fd_table[oft].ops->truncate(
          (fs_file_t)fd_table[oft].node, (u64)length
      );
    return (length == 0) ? 0 : -ENOSYS;
  }

  vfs_node_t *node = fd_table[oft].node;
  if(!node || node->type != VFS_FILE)
    return -EINVAL;

  if((u64)length <= node->size) {
    node->size = (u64)length;
    if(fd_table[oft].offset > node->size)
      fd_table[oft].offset = node->size;
    return 0;
  }

  return -ENOSYS;
}

/**
 * @brief Get current working directory
 * @return Pointer to static buffer containing current directory path
 */
const char *vfs_getcwd(void)
{
  return cwd;
}

/**
 * @brief Get directory entries (Linux getdents syscall)
 * @param fd Directory file descriptor
 * @param buf Buffer to fill with dirent structures
 * @param count Size of buffer in bytes
 * @return Bytes written, 0 at end, negative on error
 */
i64 vfs_getdents(i64 fd, void *buf, u64 count)
{
  i32 oft = fd_to_oft(fd);
  if(oft < 0)
    return -1;

  vfs_fd_t *fde = &fd_table[oft];

  /* Must be opened with O_DIRECTORY */
  if(!(fde->flags & O_DIRECTORY)) {
    return -1;
  }

  u8 *out     = (u8 *)buf;
  u64 written = 0;

  /* Handle mounted filesystem directories */
  if(is_mounted_oft(oft)) {
    fs_file_t fh = (fs_file_t)fde->node;

    /* Check if it's a directory */
    if(fde->ops->is_dir && !fde->ops->is_dir(fh)) {
      return -1;
    }

    if(!fde->ops->readdir) {
      return -1;
    }

    char name[VFS_NAME_MAX];
    u8   type;
    u64  size;
    u64  inode;

    while(written < count) {
      i64 ret = fde->ops->readdir(fh, name, &type, &size, &inode);
      if(ret <= 0) {
        break; /* End of directory or error */
      }

      u64 namelen = kstrlen(name);
      u64 reclen  = 8 + 8 + 2 + 1 + namelen + 1;
      reclen      = (reclen + 7) & ~7; /* Align to 8 bytes */

      if(written + reclen > count) {
        break; /* No more room */
      }

      u8 *p            = out + written;
      *(u64 *)p        = inode;                  /* d_ino */
      *(i64 *)(p + 8)  = (i64)(fde->offset + 1); /* d_off */
      *(u16 *)(p + 16) = (u16)reclen;            /* d_reclen */
      *(u8 *)(p + 18)  = (type == VFS_DIRECTORY) ? DT_DIR : DT_REG; /* d_type */
      kstrncpy((char *)(p + 19), name, namelen + 1);                /* d_name */

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
 * Handles both ramfs and mounted filesystem directories.
 *
 * @param path Path to new directory (absolute or relative).
 * @return 0 on success, negative on error.
 */
i64 vfs_chdir(const char *path)
{
  /* Build absolute path first */
  char abs_path[VFS_PATH_MAX];
  make_absolute_path(path, abs_path, VFS_PATH_MAX);

  /* Check if on mounted filesystem with ops */
  vfs_mount_t *mount = find_mount(abs_path);
  if(mount && mount->ops && mount->ops->open) {
    const char *rel_path = get_relative_path(abs_path, mount);

    /* Verify directory exists */
    bool      is_dir = false;
    fs_file_t dir    = mount->ops->open(mount->fs_data, rel_path, 0, &is_dir);
    if(!dir || !is_dir) {
      if(dir)
        mount->ops->close(dir);
      return -1;
    }
    mount->ops->close(dir);

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
 * @param fstype Filesystem type
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

  /* Handle ramfs specially (no device needed) */
  if(kstreq(fstype, "ramfs")) {
    mounts[slot].ops     = NULL;
    mounts[slot].fs_data = NULL;
    mounts[slot].fstype  = NULL;
    kstrncpy(mounts[slot].path, target, VFS_PATH_MAX);
    mounts[slot].active = true;

    console_printf("[vfs] mounted ramfs at %s\n", target);
    return 0;
  }

  /* Look up registered filesystem by name */
  const fs_type_t *fs = NULL;
  for(int i = 0; i < fs_type_count; i++) {
    if(kstreq(fs_types[i]->name, fstype)) {
      fs = fs_types[i];
      break;
    }
  }
  if(!fs) {
    console_printf("[vfs] mount: unknown filesystem type '%s'\n", fstype);
    return -1;
  }

  /* Parse device path */
  i32 drive = parse_device_path(source);
  if(drive < 0) {
    console_printf("[vfs] mount: invalid device %s\n", source);
    return -1;
  }

  /* Mount via registered fs type */
  void *vol = fs->mount((u8)drive, 0);
  if(!vol) {
    console_printf(
        "[vfs] mount: failed to mount %s from drive %d\n", fstype, drive
    );
    return -1;
  }

  mounts[slot].ops     = fs->ops;
  mounts[slot].fs_data = vol;
  mounts[slot].fstype  = fs;
  kstrncpy(mounts[slot].path, target, VFS_PATH_MAX);
  mounts[slot].active = true;

  console_printf(
      "[vfs] mounted %s (%s) on %s\n", source ? source : "/dev/hda", fstype,
      target
  );
  return 0;
}

/**
 * @brief Unmount a filesystem
 * @param target Mount point path to unmount
 * @return 0 on success, negative on error
 */
// cppcheck-suppress unusedFunction
i64 vfs_umount(const char *target)
{
  if(!target)
    return -1;

  for(int i = 0; i < VFS_MAX_MOUNTS; i++) {
    if(mounts[i].active && kstreq(mounts[i].path, target)) {
      /* Unmount via registered fs type if present */
      if(mounts[i].fstype && mounts[i].fstype->unmount && mounts[i].fs_data) {
        mounts[i].fstype->unmount(mounts[i].fs_data);
      }
      mounts[i].active = false;
      mounts[i].fstype = NULL;
      console_printf("[vfs] unmounted %s\n", target);
      return 0;
    }
  }

  return -1;
}

/**
 * @brief Register a filesystem type.
 *
 * Called by filesystem drivers during init to make themselves mountable.
 *
 * @param fs Filesystem type descriptor.
 * @return 0 on success, negative on error.
 */
i64 vfs_register_fs(const fs_type_t *fs)
{
  if(!fs || !fs->name || !fs->ops) {
    return -1;
  }

  if(fs_type_count >= VFS_MAX_FSTYPES) {
    console_printf("[vfs] register_fs: too many filesystem types\n");
    return -1;
  }

  /* Check for duplicate */
  for(int i = 0; i < fs_type_count; i++) {
    if(kstreq(fs_types[i]->name, fs->name)) {
      console_printf("[vfs] register_fs: '%s' already registered\n", fs->name);
      return -1;
    }
  }

  fs_types[fs_type_count++] = fs;
  console_printf("[vfs] registered filesystem: %s\n", fs->name);
  return 0;
}

i64 vfs_readlink(const char *path, char *buf, u64 cap)
{
  if(!path || !buf || cap == 0)
    return -EINVAL;

  char abs_path[VFS_PATH_MAX];
  make_absolute_path(path, abs_path, VFS_PATH_MAX);

  vfs_mount_t *mount = find_mount(abs_path);
  if(!mount || !mount->active || !mount->fs_data || !mount->fstype)
    return -ENOENT;

  if(!kstreq(mount->fstype->name, "ext2"))
    return -ENOENT;

  const char *rel = get_relative_path(abs_path, mount);
  return ext2_readlink((ext2_volume_t *)mount->fs_data, rel, buf, cap);
}
