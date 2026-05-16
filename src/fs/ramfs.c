/**
 * @file src/fs/ramfs.c
 * @brief Standalone ramfs driver for Alcor2.
 *
 * Implements a simple tree-based in-memory filesystem.
 */

#include <alcor2/errno.h>
#include <alcor2/fs/vfs.h>
#include <alcor2/kstdlib.h>
#include <alcor2/mm/heap.h>

/** @brief Internal ramfs node. */
typedef struct ram_node
{
  char             name[VFS_NAME_MAX];
  u8               type;
  u64              size;
  u8              *data;
  u64              capacity;
  struct ram_node *parent;
  struct ram_node *children;
  struct ram_node *next;
} ram_node_t;

static ram_node_t *root = NULL;

/** @brief Allocate and zero a new ramfs node with the given @p name and @p
 * type. */
static ram_node_t *ram__create_node(const char *name, u8 type)
{
  ram_node_t *node = kzalloc(sizeof(ram_node_t));
  if(!node)
    return NULL;

  kstrncpy(node->name, name, VFS_NAME_MAX);
  node->type = type;
  return node;
}

/** @brief Prepend @p child to @p parent's children list and set its parent
 * pointer. */
static void ram__add_child(ram_node_t *parent, ram_node_t *child)
{
  child->parent    = parent;
  child->next      = parent->children;
  parent->children = child;
}

/**
 * @brief Walk @p path from the ramfs root and return the matching node.
 *
 * @p path must start with @c / and be normalised (no consecutive slashes,
 * no @c . or @c .. components).
 *
 * @return Pointer to the node, or @c NULL if any component is not found.
 */
static ram_node_t *ram__resolve(const char *path)
{
  if(!path || path[0] != '/')
    return NULL;
  if(path[1] == '\0')
    return root;

  ram_node_t *node = root;
  const char *p    = path + 1;

  while(*p) {
    /* Skip any leading slashes for this component. */
    while(*p == '/')
      p++;
    if(!*p)
      break;

    char comp[VFS_NAME_MAX];
    u32  i = 0;
    while(*p && *p != '/' && i < VFS_NAME_MAX - 1)
      comp[i++] = *p++;
    comp[i] = '\0';

    /* Reject components that exceed VFS_NAME_MAX-1 bytes. */
    if(i == VFS_NAME_MAX - 1 && *p && *p != '/')
      return NULL;

    ram_node_t *child = node->children;
    while(child) {
      if(kstreq(child->name, comp))
        break;
      child = child->next;
    }
    if(!child)
      return NULL;
    node = child;
  }
  return node;
}

static fs_handle_t ram_open(void *fs_data, const char *path, u32 flags)
{
  (void)fs_data;
  ram_node_t *node = ram__resolve(path);

  if(!node) {
    if(flags & O_CREAT) {
      /* Need to find parent */
      char  parent_path[VFS_PATH_MAX];
      char  name[VFS_NAME_MAX];
      char *last_slash = kstrrchr(path, '/');
      if(!last_slash)
        return NULL;

      if(last_slash == path) {
        kstrncpy(parent_path, "/", 2);
      } else {
        u64 len = (u64)(last_slash - path);
        kmemcpy(parent_path, path, len);
        parent_path[len] = '\0';
      }
      kstrncpy(name, last_slash + 1, VFS_NAME_MAX);

      ram_node_t *parent = ram__resolve(parent_path);
      if(!parent || parent->type != VFS_DIRECTORY)
        return NULL;

      node = ram__create_node(name, VFS_FILE);
      if(!node)
        return NULL;
      ram__add_child(parent, node);
    } else {
      return NULL;
    }
  }

  if((flags & O_TRUNC) && node->type == VFS_FILE)
    node->size = 0;

  return (fs_handle_t)node;
}

static void ram_close(fs_handle_t fh)
{
  (void)fh;
}

static i64 ram_read(fs_handle_t fh, void *buf, u64 count, u64 offset)
{
  ram_node_t *node = (ram_node_t *)fh;
  if(node->type != VFS_FILE)
    return -EISDIR;
  if(offset >= node->size)
    return 0;

  u64 avail = node->size - offset;
  if(count > avail)
    count = avail;
  kmemcpy(buf, node->data + offset, count);
  return (i64)count;
}

static i64 ram_write(fs_handle_t fh, const void *buf, u64 count, u64 offset)
{
  ram_node_t *node = (ram_node_t *)fh;
  if(node->type != VFS_FILE)
    return -EISDIR;

  u64 end = offset + count;
  if(end > node->capacity) {
    u64   new_cap  = (end + 1023) & ~1023ULL;
    void *new_data = krealloc(node->data, new_cap);
    if(!new_data)
      return -ENOMEM;
    node->data     = new_data;
    node->capacity = new_cap;
  }

  kmemcpy(node->data + offset, buf, count);
  if(end > node->size)
    node->size = end;
  return (i64)count;
}

static i64 ram_stat(void *fs_data, const char *path, vfs_stat_t *st)
{
  (void)fs_data;
  ram_node_t *node = ram__resolve(path);
  if(!node)
    return -ENOENT;

  st->size = node->size;
  st->type = node->type;
  st->ino  = (u64)(uintptr_t)node;
  st->dev  = VFS_RAMFS_ST_DEV;
  return 0;
}

static i64 ram_fstat(fs_handle_t fh, vfs_stat_t *st)
{
  ram_node_t *node = (ram_node_t *)fh;
  st->size         = node->size;
  st->type         = node->type;
  st->ino          = (u64)(uintptr_t)node;
  st->dev          = VFS_RAMFS_ST_DEV;
  return 0;
}

static i64 ram_mkdir(void *fs_data, const char *path)
{
  (void)fs_data;
  if(ram__resolve(path))
    return -EEXIST;

  char  parent_path[VFS_PATH_MAX];
  char  name[VFS_NAME_MAX];
  char *last_slash = kstrrchr(path, '/');
  if(!last_slash)
    return -EINVAL;

  if(last_slash == path) {
    kstrncpy(parent_path, "/", 2);
  } else {
    u64 len = (u64)(last_slash - path);
    kmemcpy(parent_path, path, len);
    parent_path[len] = '\0';
  }
  kstrncpy(name, last_slash + 1, VFS_NAME_MAX);

  ram_node_t *parent = ram__resolve(parent_path);
  if(!parent || parent->type != VFS_DIRECTORY)
    return -ENOTDIR;

  ram_node_t *node = ram__create_node(name, VFS_DIRECTORY);
  if(!node)
    return -ENOMEM;
  ram__add_child(parent, node);
  return 0;
}

static i64 ram_readdir(fs_handle_t fh, u64 index, char *name, vfs_stat_t *st)
{
  ram_node_t *node = (ram_node_t *)fh;
  if(node->type != VFS_DIRECTORY)
    return -ENOTDIR;

  ram_node_t *child = node->children;
  for(u64 i = 0; i < index && child; i++)
    child = child->next;

  if(!child)
    return 0;

  kstrncpy(name, child->name, VFS_NAME_MAX);
  if(st) {
    st->size = child->size;
    st->type = child->type;
    st->ino  = (u64)(uintptr_t)child;
  }
  return 1;
}

static i64 ram_unlink(void *fs_data, const char *path)
{
  (void)fs_data;
  ram_node_t *node = ram__resolve(path);
  if(!node || node->type == VFS_DIRECTORY)
    return -EISDIR;

  /* Remove from parent's list */
  ram_node_t *parent = node->parent;
  if(parent->children == node) {
    parent->children = node->next;
  } else {
    ram_node_t *prev = parent->children;
    while(prev && prev->next != node)
      prev = prev->next;
    if(prev)
      prev->next = node->next;
  }

  if(node->data)
    kfree(node->data);
  kfree(node);
  return 0;
}

static i64 ram_rmdir(void *fs_data, const char *path)
{
  (void)fs_data;
  ram_node_t *node = ram__resolve(path);
  if(!node)
    return -ENOENT;
  if(node->type != VFS_DIRECTORY)
    return -ENOTDIR;
  if(node->children)
    return -ENOTEMPTY;
  if(node == root)
    return -EBUSY;

  ram_node_t *parent = node->parent;
  if(parent->children == node) {
    parent->children = node->next;
  } else {
    ram_node_t *prev = parent->children;
    while(prev && prev->next != node)
      prev = prev->next;
    if(prev)
      prev->next = node->next;
  }
  kfree(node);
  return 0;
}

static i64 ram_truncate(fs_handle_t fh, u64 length)
{
  ram_node_t *node = (ram_node_t *)fh;
  if(node->type != VFS_FILE)
    return -EISDIR;

  if(length == 0) {
    if(node->data)
      kfree(node->data);
    node->data     = NULL;
    node->capacity = 0;
    node->size     = 0;
    return 0;
  }
  /* Simple truncation not implemented yet for ramfs extension */
  if(length < node->size) {
    node->size = length;
    return 0;
  }
  return -ENOSYS;
}

static const fs_ops_t ram_ops = {
    .open     = ram_open,
    .close    = ram_close,
    .read     = ram_read,
    .write    = ram_write,
    .mkdir    = ram_mkdir,
    .rmdir    = ram_rmdir,
    .unlink   = ram_unlink,
    .stat     = ram_stat,
    .fstat    = ram_fstat,
    .readdir  = ram_readdir,
    .truncate = ram_truncate,
};

static void *ram_mount_cb(const char *source, u32 flags)
{
  (void)source;
  (void)flags;
  /* Root is static, but we return a non-NULL token to signal success */
  return (void *)1;
}

static const fs_type_t ram_fstype = {
    .name  = "ramfs",
    .ops   = &ram_ops,
    .mount = ram_mount_cb,
};

void ramfs_init(void)
{
  /* Idempotent: callers (both early-init and init_storage) may invoke this
   * more than once; only the first call constructs the tree and registers
   * the driver. */
  if(root)
    return;
  root         = ram__create_node("/", VFS_DIRECTORY);
  root->parent = root;
  vfs_register_fs(&ram_fstype);
}
