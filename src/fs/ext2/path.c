/**
 * @file src/fs/ext2/path.c
 * @brief Path resolution, symlink following, and the @ref ext2_stat /
 *        @ref ext2_readlink endpoints.
 *
 * Walks a path component-by-component, following symlinks with a recursion
 * depth cap to bound runaway loops. Symlink-target reading covers both fast
 * symlinks (target inline in @c i_block[]) and slow symlinks (target stored
 * in the first data block); centralising both in one helper keeps the
 * fast/slow choice in one place.
 */

#include <alcor2/errno.h>
#include <alcor2/fs/vfs.h>
#include <alcor2/kstdlib.h>
#include <alcor2/mm/heap.h>
#include <alcor2/types.h>
#include <fs/ext2/internal.h>

/**
 * @brief Read the target of a symlink inode into a buffer.
 *
 * Handles both fast symlinks (target ≤ 60 bytes, stored inline in i_block[])
 * and slow symlinks (target stored in the first data block).
 *
 * @param vol    Volume.
 * @param inode  Symlink inode.
 * @param buf    Output buffer.
 * @param bufsz  Buffer size.
 * @return 0 on success, negative errno on error.
 */
static i64 read_symlink_target(
    const ext2_volume_t *vol, const ext2_inode_t *inode, char *buf, u32 bufsz
)
{
  u32 len = inode->i_size;
  if(len == 0 || len >= bufsz)
    return -EINVAL;

  if(len <= 60) {
    /* Fast symlink: target stored inline in the i_block[] array. */
    kmemcpy(buf, (const u8 *)inode->i_block, len);
  } else {
    /* Slow symlink: target stored in the first data block. */
    u8 *blk = cache_get_block(vol->block_size);
    if(!blk)
      return -ENOMEM;
    if(vol_read_block(vol, inode->i_block[0], blk) < 0) {
      cache_put_block(blk);
      return -EIO;
    }
    if(len > vol->block_size)
      len = (u32)vol->block_size;
    if(len >= bufsz)
      len = bufsz - 1;
    kmemcpy(buf, blk, len);
    cache_put_block(blk);
  }
  buf[len] = '\0';
  return 0;
}

/**
 * @brief Build a resolved path from a base path and a (possibly relative)
 *        symlink target.
 *
 * If @p target is absolute it is used directly.  If relative, it is joined
 * to the directory part of @p base (i.e. everything up to and including the
 * last '/').
 *
 * @param base    Path that contained the symlink (relative to mount root).
 * @param target  Symlink target as read from the inode.
 * @param out     Output buffer.
 * @param outsz   Output buffer size.
 */
static void build_symlink_path(
    const char *base, const char *target, char *out, u32 outsz
)
{
  if(target[0] == '/') {
    kstrncpy(out, target, outsz);
    return;
  }

  /* Find the last '/' in base to get the parent directory. */
  u32 last_slash = 0;
  for(u32 i = 0; base[i]; i++) {
    if(base[i] == '/')
      last_slash = i + 1; /* include the slash */
  }

  if(last_slash >= outsz) {
    kstrncpy(out, target, outsz);
    return;
  }

  kmemcpy(out, base, last_slash);
  kstrncpy(out + last_slash, target, outsz - last_slash);
}

/**
 * @brief Resolve a path to an inode, following symlinks.
 * @param vol          Volume.
 * @param path         Path to resolve (relative to volume root).
 * @param out_ino      Output inode number.
 * @param out_inode    Output inode structure.
 * @param follow_depth Current symlink-follow depth (prevents loops).
 * @return 0 on success, negative errno on error.
 */
#define SYMLINK_MAX_FOLLOW 8

static i64 resolve_path_depth(
    const ext2_volume_t *vol, const char *path, u32 *out_ino,
    ext2_inode_t *out_inode, int follow_depth
)
{
  if(follow_depth > SYMLINK_MAX_FOLLOW)
    return -ELOOP;

  u32          current_ino = EXT2_ROOT_INODE;
  ext2_inode_t current_inode;

  if(read_inode(vol, current_ino, &current_inode) < 0)
    return -EIO;

  /* Skip leading slash */
  if(path[0] == '/')
    path++;

  /* Empty path = root */
  if(path[0] == '\0') {
    *out_ino   = current_ino;
    *out_inode = current_inode;
    return 0;
  }

  /* Keep a working copy of the full path for symlink target construction. */
  char work[VFS_PATH_MAX];
  kstrncpy(work, path, VFS_PATH_MAX);
  const char *p = work;

  char        component[EXT2_NAME_MAX + 1];

  while(*p) {
    /* Skip slashes */
    while(*p == '/')
      p++;
    if(*p == '\0')
      break;

    /* Extract component */
    u32 i = 0;
    while(*p && *p != '/' && i < EXT2_NAME_MAX) {
      component[i++] = *p++;
    }
    component[i] = '\0';

    /* Must be a directory to traverse */
    if((current_inode.i_mode & EXT2_S_IFMT) != EXT2_S_IFDIR)
      return -ENOTDIR;

    /* Find entry */
    u32 entry_ino;
    u8  entry_type;
    if(dir_find_entry(vol, &current_inode, component, &entry_ino, &entry_type) <
       0)
      return -ENOENT;

    current_ino = entry_ino;
    if(read_inode(vol, current_ino, &current_inode) < 0)
      return -EIO;

    /* Follow symlinks (both intermediate and final). */
    if((current_inode.i_mode & EXT2_S_IFMT) == EXT2_S_IFLNK) {
      char target[VFS_PATH_MAX];
      if(read_symlink_target(vol, &current_inode, target, sizeof(target)) < 0)
        return -EIO;

      /* Position of this component in `work`: just before component start. */
      u32 comp_offset = (u32)(p - work) - kstrlen(component);
      /* Remaining path after symlink component (may be empty or "/..."). */
      const char *rest = p;

      /* Build: parent(work[0..comp_offset-1]) + target + rest */
      char followed[VFS_PATH_MAX];
      char base_for_link[VFS_PATH_MAX];
      kstrncpy(
          base_for_link, work,
          comp_offset < VFS_PATH_MAX ? comp_offset : VFS_PATH_MAX
      );
      if(comp_offset < VFS_PATH_MAX)
        base_for_link[comp_offset] = '\0';

      /* Append a dummy filename so build_symlink_path strips correctly. */
      u32 bl = kstrlen(base_for_link);
      if(bl + 8 < VFS_PATH_MAX) {
        base_for_link[bl]     = '/';
        base_for_link[bl + 1] = 'X'; /* dummy */
        base_for_link[bl + 2] = '\0';
      }
      build_symlink_path(base_for_link, target, followed, VFS_PATH_MAX);

      /* Append remaining path components. */
      if(*rest) {
        u32 flen = kstrlen(followed);
        if(flen + 1 + kstrlen(rest) < VFS_PATH_MAX) {
          followed[flen] = '/';
          kstrncpy(followed + flen + 1, rest, VFS_PATH_MAX - flen - 1);
        }
      }

      return resolve_path_depth(
          vol, followed, out_ino, out_inode, follow_depth + 1
      );
    }
  }

  *out_ino   = current_ino;
  *out_inode = current_inode;
  return 0;
}

/**
 * @brief Resolve a path to an inode.
 * @param vol Volume.
 * @param path Path to resolve.
 * @param out_ino Output inode number.
 * @param out_inode Output inode structure.
 * @return 0 on success, negative errno on error.
 */
i64 resolve_path(
    const ext2_volume_t *vol, const char *path, u32 *out_ino,
    ext2_inode_t *out_inode
)
{
  return resolve_path_depth(vol, path, out_ino, out_inode, 0);
}

/**
 * @brief Get parent directory path and filename from a path.
 * @param path Full path.
 * @param parent Output buffer for parent path.
 * @param name Output buffer for filename.
 */
void path_split(const char *path, char *parent, char *name)
{
  u32 len        = kstrlen(path);
  i32 last_slash = -1;

  for(u32 i = 0; i < len; i++) {
    if(path[i] == '/')
      last_slash = (i32)i;
  }

  if(last_slash <= 0) {
    parent[0] = '/';
    parent[1] = '\0';
    if(path[0] == '/')
      kstrncpy(name, path + 1, EXT2_NAME_MAX);
    else
      kstrncpy(name, path, EXT2_NAME_MAX);
  } else {
    kmemcpy(parent, path, (u64)last_slash);
    parent[last_slash] = '\0';
    kstrncpy(name, path + last_slash + 1, EXT2_NAME_MAX);
  }
}

/**
 * @brief Get file status information.
 *
 * @param vol   Volume handle.
 * @param path  Path to the file or directory.
 * @param entry Output entry with inode, size, and type.
 * @return 0 on success, negative errno on error.
 */
i64 ext2_stat(const ext2_volume_t *vol, const char *path, ext2_entry_t *entry)
{
  if(!vol || !vol->mounted || !path || !entry)
    return -EINVAL;

  u32          ino;
  ext2_inode_t inode;
  if(resolve_path(vol, path, &ino, &inode) < 0)
    return -ENOENT;

  /* Extract filename from path */
  u32 len        = kstrlen(path);
  i32 last_slash = -1;
  for(u32 i = 0; i < len; i++) {
    if(path[i] == '/')
      last_slash = (i32)i;
  }
  if(last_slash == -1 || (u32)(last_slash + 1) >= len) {
    kstrncpy(entry->name, path, EXT2_NAME_MAX);
  } else {
    kstrncpy(entry->name, path + last_slash + 1, EXT2_NAME_MAX);
  }

  entry->inode = ino;
  entry->size  = inode.i_size;

  u16 mode = inode.i_mode & EXT2_S_IFMT;
  switch(mode) {
  case EXT2_S_IFREG:
    entry->file_type = EXT2_FT_REG_FILE;
    break;
  case EXT2_S_IFDIR:
    entry->file_type = EXT2_FT_DIR;
    break;
  case EXT2_S_IFLNK:
    entry->file_type = EXT2_FT_SYMLINK;
    break;
  case EXT2_S_IFCHR:
    entry->file_type = EXT2_FT_CHRDEV;
    break;
  case EXT2_S_IFBLK:
    entry->file_type = EXT2_FT_BLKDEV;
    break;
  case EXT2_S_IFIFO:
    entry->file_type = EXT2_FT_FIFO;
    break;
  case EXT2_S_IFSOCK:
    entry->file_type = EXT2_FT_SOCK;
    break;
  default:
    entry->file_type = EXT2_FT_UNKNOWN;
    break;
  }

  return 0;
}

i64 ext2_readlink(
    const ext2_volume_t *vol, const char *path, char *buf, u64 cap
)
{
  if(!vol || !vol->mounted || !path || !buf || cap == 0)
    return -EINVAL;

  char parent_path[VFS_PATH_MAX];
  char filename[EXT2_NAME_MAX + 1];
  path_split(path, parent_path, filename);

  if(filename[0] == '\0')
    return -EINVAL;

  u32          parent_ino;
  ext2_inode_t parent_inode;
  if(resolve_path(vol, parent_path, &parent_ino, &parent_inode) < 0)
    return -ENOENT;

  if((parent_inode.i_mode & EXT2_S_IFMT) != EXT2_S_IFDIR)
    return -ENOTDIR;

  u32 entry_ino;
  u8  entry_type;
  if(dir_find_entry(vol, &parent_inode, filename, &entry_ino, &entry_type) < 0)
    return -ENOENT;

  ext2_inode_t inode;
  if(read_inode(vol, entry_ino, &inode) < 0)
    return -EIO;

  if((inode.i_mode & EXT2_S_IFMT) != EXT2_S_IFLNK)
    return -EINVAL;

  char target[VFS_PATH_MAX];
  i64  tlen = read_symlink_target(vol, &inode, target, sizeof(target));
  if(tlen < 0)
    return -EIO;

  u64 n = (u64)tlen;
  if(n >= cap)
    return -ENAMETOOLONG;

  kmemcpy(buf, target, n);
  return (i64)n;
}
