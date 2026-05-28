/**
 * @file src/fs/ext2/inode.c
 * @brief On-disk inode read + write.
 *
 * Two functions, almost-symmetric. They share the same block-locate math
 * (group → inode table → offset) because the inode lives in a packed
 * variable-stride table; centralising here keeps the math in exactly one
 * place per direction.
 */

#include <alcor2/errno.h>
#include <alcor2/kstdlib.h>
#include <alcor2/mm/heap.h>
#include <alcor2/types.h>
#include <fs/ext2/internal.h>

i64 read_inode(const ext2_volume_t *vol, u32 ino, ext2_inode_t *inode)
{
  if(ino < 1 || ino > vol->inodes_count)
    return -EINVAL;

  u32 group       = (ino - 1) / vol->inodes_per_group;
  u32 index       = (ino - 1) % vol->inodes_per_group;
  u32 inode_table = vol->groups[group].bg_inode_table;

  u32 inodes_per_block = vol->block_size / vol->inode_size;
  u32 block            = inode_table + (index / inodes_per_block);
  u32 offset           = (index % inodes_per_block) * vol->inode_size;

  u8 *buf = kmalloc(vol->block_size);
  if(!buf)
    return -ENOMEM;

  if(vol_read_block(vol, block, buf) < 0) {
    kfree(buf);
    return -EIO;
  }

  kmemcpy(inode, buf + offset, sizeof(ext2_inode_t));
  kfree(buf);
  return 0;
}

i64 write_inode(const ext2_volume_t *vol, u32 ino, const ext2_inode_t *inode)
{
  if(ino < 1 || ino > vol->inodes_count)
    return -EINVAL;

  u32 group       = (ino - 1) / vol->inodes_per_group;
  u32 index       = (ino - 1) % vol->inodes_per_group;
  u32 inode_table = vol->groups[group].bg_inode_table;

  u32 inodes_per_block = vol->block_size / vol->inode_size;
  u32 block            = inode_table + (index / inodes_per_block);
  u32 offset           = (index % inodes_per_block) * vol->inode_size;

  u8 *buf = kmalloc(vol->block_size);
  if(!buf)
    return -ENOMEM;

  if(vol_read_block(vol, block, buf) < 0) {
    kfree(buf);
    return -EIO;
  }

  kmemcpy(buf + offset, inode, sizeof(ext2_inode_t));

  if(vol_write_block(vol, block, buf) < 0) {
    kfree(buf);
    return -EIO;
  }

  kfree(buf);
  return 0;
}
