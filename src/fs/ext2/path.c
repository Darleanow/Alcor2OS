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
#include <alcor2/fs/ext2.h>
#include <alcor2/fs/vfs.h>
#include <alcor2/kstdlib.h>
#include <alcor2/mm/heap.h>
#include <alcor2/types.h>
#include <fs/ext2/internal.h>

/**
 * @brief Inline-storage capacity for fast symlinks.
 *
 * ext2 stores the @c i_block[] array as 15 × @c u32; targets up to 60
 * bytes fit inline and skip the data-block round trip.
 */
#define EXT2_FAST_SYMLINK_MAX 60u

/**
 * @brief Spare bytes reserved when growing @c base_for_link in
 *        @ref follow_symlink_step.
 *
 * The dummy filename appended before @ref build_symlink_path is "/X\0"
 * (3 bytes); 8 is a conservative ceiling that survives future tweaks to
 * the dummy without recomputing the bound.
 */
#define SYMLINK_BASE_RESERVE_BYTES 8u

/**
 * @brief Read the target of a symlink inode into a buffer.
 *
 * Handles both fast symlinks (target ≤ @ref EXT2_FAST_SYMLINK_MAX bytes,
 * stored inline in @c i_block[]) and slow symlinks (target stored in the
 * first data block).
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

  if(len <= EXT2_FAST_SYMLINK_MAX) {
    kmemcpy(buf, (const u8 *)inode->i_block, len);
  } else {
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
 * If @p target is absolute it is used directly. If relative, it is joined
 * to the directory part of @p base (i.e. everything up to and including
 * the last '/').
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

  u32 last_slash = 0;
  for(u32 i = 0; base[i]; i++) {
    if(base[i] == '/')
      last_slash = i + 1;
  }

  if(last_slash >= outsz) {
    kstrncpy(out, target, outsz);
    return;
  }

  kmemcpy(out, base, last_slash);
  kstrncpy(out + last_slash, target, outsz - last_slash);
}

/**
 * @brief Maximum symlink chase depth before declaring @c -ELOOP.
 *
 * Same constant Linux uses. Far above any sane userland chain, low enough
 * to keep the recursive @ref resolve_path_depth from blowing the kernel
 * stack on a hand-crafted symlink cycle.
 */
#define SYMLINK_MAX_FOLLOW 8

/**
 * @brief Advance @p *p past one path component into @p component.
 *
 * Skips leading slashes, then copies non-slash bytes (capped to
 * @ref EXT2_NAME_MAX) into @p component and NUL-terminates. Used as the
 * tokenizer for the path-walk loop.
 *
 * @param p          In/out pointer into the path; advanced past the
 *                   component on success.
 * @param component  Output buffer of @c EXT2_NAME_MAX+1 bytes.
 * @return @c true on success, @c false at end of path.
 */
static bool parse_path_component(const char **p, char *component)
{
  while(**p == '/')
    (*p)++;
  if(**p == '\0')
    return false;

  u32 i = 0;
  while(**p && **p != '/' && i < EXT2_NAME_MAX) {
    component[i++] = **p;
    (*p)++;
  }
  component[i] = '\0';
  return true;
}

/* Forward decl — @ref follow_symlink_step recurses through resolve_path_depth.
 */
static i64 resolve_path_depth(
    const ext2_volume_t *vol, const char *path, u32 *out_ino,
    ext2_inode_t *out_inode, int follow_depth
);

/**
 * @brief Reparent a path through a symlink and recurse.
 *
 * Builds @c "<dir-of-symlink>/<target>/<rest-of-original>" and feeds it
 * back into @ref resolve_path_depth with the depth counter bumped. Kept
 * separate from the path walker because the path-building math (dummy
 * filename, base offset) is its own concern.
 *
 * @param vol           Volume.
 * @param work          Original path being walked.
 * @param p             Current position in @p work (start of post-symlink
 * remainder).
 * @param link_inode    Inode of the symlink just hit.
 * @param component     Name of the symlink component within @p work.
 * @param out_ino       Output inode number.
 * @param out_inode     Output inode struct.
 * @param follow_depth  Current chase depth.
 * @return Result of the recursive resolve.
 */
static i64 follow_symlink_step(
    const ext2_volume_t *vol, const char *work, const char *p,
    const ext2_inode_t *link_inode, const char *component, u32 *out_ino,
    ext2_inode_t *out_inode, int follow_depth
)
{
  char target[VFS_PATH_MAX];
  if(read_symlink_target(vol, link_inode, target, sizeof(target)) < 0)
    return -EIO;

  u32         comp_offset = (u32)(p - work) - kstrlen(component);
  const char *rest        = p;

  char        followed[VFS_PATH_MAX];
  char        base_for_link[VFS_PATH_MAX];
  kstrncpy(
      base_for_link, work,
      comp_offset < VFS_PATH_MAX ? comp_offset : VFS_PATH_MAX
  );
  if(comp_offset < VFS_PATH_MAX)
    base_for_link[comp_offset] = '\0';

  u32 bl = kstrlen(base_for_link);
  if(bl + SYMLINK_BASE_RESERVE_BYTES < VFS_PATH_MAX) {
    base_for_link[bl]     = '/';
    base_for_link[bl + 1] = 'X';
    base_for_link[bl + 2] = '\0';
  }
  build_symlink_path(base_for_link, target, followed, VFS_PATH_MAX);

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

/**
 * @brief Look up @p component inside @p current_inode and advance the
 *        cursor to the resolved inode.
 *
 * Wraps the "must be a directory + dir_find_entry + read_inode" sequence
 * so the path walker doesn't carry it inline.
 *
 * @param vol            Volume.
 * @param current_ino    In/out inode number; updated to the resolved entry.
 * @param current_inode  In/out inode struct; updated to the resolved entry.
 * @param component      Name to look up.
 * @return 0 on success, -ENOTDIR if @p current_inode isn't a directory,
 *         -ENOENT if @p component is absent, -EIO on read failure.
 */
static i64 walk_one_component(
    const ext2_volume_t *vol, u32 *current_ino, ext2_inode_t *current_inode,
    const char *component
)
{
  if((current_inode->i_mode & EXT2_S_IFMT) != EXT2_S_IFDIR)
    return -ENOTDIR;
  u32 entry_ino;
  u8  entry_type;
  if(dir_find_entry(vol, current_inode, component, &entry_ino, &entry_type) < 0)
    return -ENOENT;
  *current_ino = entry_ino;
  if(read_inode(vol, *current_ino, current_inode) < 0)
    return -EIO;
  return 0;
}

/**
 * @brief Resolve a path to an inode, following symlinks.
 *
 * Owns the path-walk state (current inode, position in @c work) and
 * delegates the per-step work to @ref parse_path_component, @ref
 * walk_one_component, and @ref follow_symlink_step.
 *
 * @param vol          Volume.
 * @param path         Path to resolve (relative to volume root).
 * @param out_ino      Output inode number.
 * @param out_inode    Output inode structure.
 * @param follow_depth Current symlink-follow depth (prevents loops).
 * @return 0 on success, negative errno on error.
 */
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

  if(path[0] == '/')
    path++;
  if(path[0] == '\0') {
    *out_ino   = current_ino;
    *out_inode = current_inode;
    return 0;
  }

  char work[VFS_PATH_MAX];
  kstrncpy(work, path, VFS_PATH_MAX);
  const char *p = work;
  char        component[EXT2_NAME_MAX + 1];

  while(parse_path_component(&p, component)) {
    i64 ret = walk_one_component(vol, &current_ino, &current_inode, component);
    if(ret < 0)
      return ret;

    if((current_inode.i_mode & EXT2_S_IFMT) == EXT2_S_IFLNK)
      return follow_symlink_step(
          vol, work, p, &current_inode, component, out_ino, out_inode,
          follow_depth
      );
  }

  *out_ino   = current_ino;
  *out_inode = current_inode;
  return 0;
}

/**
 * @brief Resolve a path to an inode.
 *
 * Entry point for callers that don't track recursion depth; seeds the
 * follow counter to 0.
 *
 * @param vol        Volume.
 * @param path       Path to resolve.
 * @param out_ino    Output inode number.
 * @param out_inode  Output inode structure.
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
 * @brief Translate an ext2 on-disk mode field to a directory-entry
 *        file_type code.
 *
 * @c i_mode encodes the kind of inode in its high bits; @c file_type is
 * the user-facing summary stat reports. The mapping is a one-line per
 * case but spelling it out as a switch makes adding a new EXT2_FT_* code
 * a single-line change.
 *
 * @param mode  Raw @c i_mode value.
 * @return Matching @c EXT2_FT_* code (or @c EXT2_FT_UNKNOWN).
 */
static u8 mode_to_file_type(u16 mode)
{
  switch(mode & EXT2_S_IFMT) {
  case EXT2_S_IFREG:
    return EXT2_FT_REG_FILE;
  case EXT2_S_IFDIR:
    return EXT2_FT_DIR;
  case EXT2_S_IFLNK:
    return EXT2_FT_SYMLINK;
  case EXT2_S_IFCHR:
    return EXT2_FT_CHRDEV;
  case EXT2_S_IFBLK:
    return EXT2_FT_BLKDEV;
  case EXT2_S_IFIFO:
    return EXT2_FT_FIFO;
  case EXT2_S_IFSOCK:
    return EXT2_FT_SOCK;
  default:
    return EXT2_FT_UNKNOWN;
  }
}

/**
 * @brief Copy the basename of @p path into @p name.
 *
 * Centralised so @ref ext2_stat doesn't carry a second copy of the
 * last-slash scan that already lives in @ref path_split.
 *
 * @param path  Full path.
 * @param name  Output buffer of @c EXT2_NAME_MAX+1 bytes.
 */
static void extract_basename(const char *path, char *name)
{
  u32 len        = kstrlen(path);
  i32 last_slash = -1;
  for(u32 i = 0; i < len; i++) {
    if(path[i] == '/')
      last_slash = (i32)i;
  }
  if(last_slash == -1 || (u32)(last_slash + 1) >= len)
    kstrncpy(name, path, EXT2_NAME_MAX);
  else
    kstrncpy(name, path + last_slash + 1, EXT2_NAME_MAX);
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

  extract_basename(path, entry->name);
  entry->inode     = ino;
  entry->size      = inode.i_size;
  entry->file_type = mode_to_file_type(inode.i_mode);
  return 0;
}

/**
 * @brief Read the target of the symlink at @p path into @p buf.
 *
 * Splits the path so the target's *containing directory* is resolved (not
 * the symlink itself — that would follow it). The final component is then
 * looked up directly, its inode validated as a symlink, and the target
 * read via the shared @ref read_symlink_target so fast/slow link layouts
 * stay in one place.
 *
 * @param vol   Volume handle.
 * @param path  Absolute path of the symlink.
 * @param buf   Output buffer (no terminating NUL appended).
 * @param cap   Capacity of @p buf.
 * @return Bytes written, or negative errno (-EINVAL for non-symlinks,
 *         -ENAMETOOLONG when the target exceeds @p cap).
 */
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
