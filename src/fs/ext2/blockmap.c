/**
 * @file src/fs/ext2/blockmap.c
 * @brief Direct + indirect block resolution and reservation for ext2.
 *
 * ext2 spreads a file's blocks across three regions: 12 direct entries in the
 * inode, then a single-indirect block, then a double-indirect block, then a
 * triple-indirect block. @ref get_block_num walks them read-only; @ref
 * alloc_file_block grows the indirect tree on demand; @ref free_inode_blocks
 * undoes both passes. Kept in one file because the three operations share
 * the same level/range arithmetic and getting it wrong in one mirrors it in
 * the others.
 */

#include <alcor2/errno.h>
#include <alcor2/kstdlib.h>
#include <alcor2/mm/heap.h>
#include <alcor2/types.h>
#include <fs/ext2/internal.h>

/**
 * @brief Get block number for a given file block index.
 * @param vol Volume.
 * @param inode Inode.
 * @param file_block File block index.
 * @return Block number, or 0 if not allocated.
 */
u32 get_block_num(
    const ext2_volume_t *vol, const ext2_inode_t *inode, u32 file_block
)
{
  u32 ptrs_per_block = vol->block_size / 4;

  /* Direct blocks */
  if(file_block < EXT2_NDIR_BLOCKS) {
    return inode->i_block[file_block];
  }

  file_block -= EXT2_NDIR_BLOCKS;

  /* Single indirect */
  if(file_block < ptrs_per_block) {
    if(inode->i_block[EXT2_IND_BLOCK] == 0)
      return 0;

    u32 *ind_block = kmalloc(vol->block_size);
    if(!ind_block)
      return 0;

    if(vol_read_block(vol, inode->i_block[EXT2_IND_BLOCK], ind_block) < 0) {
      kfree(ind_block);
      return 0;
    }

    u32 result = ind_block[file_block];
    kfree(ind_block);
    return result;
  }

  file_block -= ptrs_per_block;

  /* Double indirect */
  if(file_block < ptrs_per_block * ptrs_per_block) {
    if(inode->i_block[EXT2_DIND_BLOCK] == 0)
      return 0;

    u32 *dind_block = kmalloc(vol->block_size);
    if(!dind_block)
      return 0;

    if(vol_read_block(vol, inode->i_block[EXT2_DIND_BLOCK], dind_block) < 0) {
      kfree(dind_block);
      return 0;
    }

    u32 ind_idx       = file_block / ptrs_per_block;
    u32 ind_block_num = dind_block[ind_idx];
    kfree(dind_block);

    if(ind_block_num == 0)
      return 0;

    u32 *ind_block = kmalloc(vol->block_size);
    if(!ind_block)
      return 0;

    if(vol_read_block(vol, ind_block_num, ind_block) < 0) {
      kfree(ind_block);
      return 0;
    }

    u32 result = ind_block[file_block % ptrs_per_block];
    kfree(ind_block);
    return result;
  }

  file_block -= ptrs_per_block * ptrs_per_block;

  /* Triple indirect */
  if(inode->i_block[EXT2_TIND_BLOCK] == 0)
    return 0;

  u32 *tind_block = kmalloc(vol->block_size);
  if(!tind_block)
    return 0;

  if(vol_read_block(vol, inode->i_block[EXT2_TIND_BLOCK], tind_block) < 0) {
    kfree(tind_block);
    return 0;
  }

  u32 dind_idx       = file_block / (ptrs_per_block * ptrs_per_block);
  u32 dind_block_num = tind_block[dind_idx];
  kfree(tind_block);

  if(dind_block_num == 0)
    return 0;

  u32 *dind_block = kmalloc(vol->block_size);
  if(!dind_block)
    return 0;

  if(vol_read_block(vol, dind_block_num, dind_block) < 0) {
    kfree(dind_block);
    return 0;
  }

  u32 remaining     = file_block % (ptrs_per_block * ptrs_per_block);
  u32 ind_idx       = remaining / ptrs_per_block;
  u32 ind_block_num = dind_block[ind_idx];
  kfree(dind_block);

  if(ind_block_num == 0)
    return 0;

  u32 *ind_block = kmalloc(vol->block_size);
  if(!ind_block)
    return 0;

  if(vol_read_block(vol, ind_block_num, ind_block) < 0) {
    kfree(ind_block);
    return 0;
  }

  u32 result = ind_block[remaining % ptrs_per_block];
  kfree(ind_block);
  return result;
}

/**
 * @brief Allocate and set a block for a given file block index.
 * @param vol Volume.
 * @param inode Inode (will be modified).
 * @param file_block File block index.
 * @param preferred_group Preferred block group for allocation.
 * @return Block number, or 0 on failure.
 */
u32 alloc_file_block(
    ext2_volume_t *vol, ext2_inode_t *inode, u32 file_block, u32 preferred_group
)
{
  u32 ptrs_per_block = vol->block_size / 4;

  /* Direct blocks */
  if(file_block < EXT2_NDIR_BLOCKS) {
    if(inode->i_block[file_block] == 0) {
      u32 block = alloc_block(vol, preferred_group);
      if(block == 0)
        return 0;
      inode->i_block[file_block] = block;
      inode->i_blocks += vol->block_size / 512;

      /* Zero the new block */
      u8 *zero = kmalloc(vol->block_size);
      if(zero) {
        kzero(zero, vol->block_size);
        vol_write_block(vol, block, zero);
        kfree(zero);
      }
    }
    return inode->i_block[file_block];
  }

  file_block -= EXT2_NDIR_BLOCKS;

  /* Single indirect */
  if(file_block < ptrs_per_block) {
    /* Allocate indirect block if needed */
    if(inode->i_block[EXT2_IND_BLOCK] == 0) {
      u32 ind = alloc_block(vol, preferred_group);
      if(ind == 0)
        return 0;
      inode->i_block[EXT2_IND_BLOCK] = ind;
      inode->i_blocks += vol->block_size / 512;

      u8 *zero = kmalloc(vol->block_size);
      if(zero) {
        kzero(zero, vol->block_size);
        vol_write_block(vol, ind, zero);
        kfree(zero);
      }
    }

    u32 *ind_block = kmalloc(vol->block_size);
    if(!ind_block)
      return 0;

    if(vol_read_block(vol, inode->i_block[EXT2_IND_BLOCK], ind_block) < 0) {
      kfree(ind_block);
      return 0;
    }

    if(ind_block[file_block] == 0) {
      u32 block = alloc_block(vol, preferred_group);
      if(block == 0) {
        kfree(ind_block);
        return 0;
      }
      ind_block[file_block] = block;
      inode->i_blocks += vol->block_size / 512;

      vol_write_block(vol, inode->i_block[EXT2_IND_BLOCK], ind_block);

      u8 *zero = kmalloc(vol->block_size);
      if(zero) {
        kzero(zero, vol->block_size);
        vol_write_block(vol, block, zero);
        kfree(zero);
      }
    }

    u32 result = ind_block[file_block];
    kfree(ind_block);
    return result;
  }

  file_block -= ptrs_per_block;

  /* Double indirect */
  if(file_block < ptrs_per_block * ptrs_per_block) {
    /* Allocate double indirect block if needed */
    if(inode->i_block[EXT2_DIND_BLOCK] == 0) {
      u32 dind = alloc_block(vol, preferred_group);
      if(dind == 0)
        return 0;
      inode->i_block[EXT2_DIND_BLOCK] = dind;
      inode->i_blocks += vol->block_size / 512;

      u8 *zero = kmalloc(vol->block_size);
      if(zero) {
        kzero(zero, vol->block_size);
        vol_write_block(vol, dind, zero);
        kfree(zero);
      }
    }

    u32 *dind_block = kmalloc(vol->block_size);
    if(!dind_block)
      return 0;

    if(vol_read_block(vol, inode->i_block[EXT2_DIND_BLOCK], dind_block) < 0) {
      kfree(dind_block);
      return 0;
    }

    u32 ind_idx = file_block / ptrs_per_block;

    /* Allocate indirect block if needed */
    if(dind_block[ind_idx] == 0) {
      u32 ind = alloc_block(vol, preferred_group);
      if(ind == 0) {
        kfree(dind_block);
        return 0;
      }
      dind_block[ind_idx] = ind;
      inode->i_blocks += vol->block_size / 512;

      vol_write_block(vol, inode->i_block[EXT2_DIND_BLOCK], dind_block);

      u8 *zero = kmalloc(vol->block_size);
      if(zero) {
        kzero(zero, vol->block_size);
        vol_write_block(vol, ind, zero);
        kfree(zero);
      }
    }

    u32 ind_block_num = dind_block[ind_idx];
    kfree(dind_block);

    u32 *ind_block = kmalloc(vol->block_size);
    if(!ind_block)
      return 0;

    if(vol_read_block(vol, ind_block_num, ind_block) < 0) {
      kfree(ind_block);
      return 0;
    }

    u32 ind_offset = file_block % ptrs_per_block;
    if(ind_block[ind_offset] == 0) {
      u32 block = alloc_block(vol, preferred_group);
      if(block == 0) {
        kfree(ind_block);
        return 0;
      }
      ind_block[ind_offset] = block;
      inode->i_blocks += vol->block_size / 512;

      vol_write_block(vol, ind_block_num, ind_block);

      u8 *zero = kmalloc(vol->block_size);
      if(zero) {
        kzero(zero, vol->block_size);
        vol_write_block(vol, block, zero);
        kfree(zero);
      }
    }

    u32 result = ind_block[ind_offset];
    kfree(ind_block);
    return result;
  }

  file_block -= ptrs_per_block * ptrs_per_block;

  /* Triple indirect */
  if(inode->i_block[EXT2_TIND_BLOCK] == 0) {
    u32 tind = alloc_block(vol, preferred_group);
    if(tind == 0)
      return 0;
    inode->i_block[EXT2_TIND_BLOCK] = tind;
    inode->i_blocks += vol->block_size / 512;

    u8 *zero = kmalloc(vol->block_size);
    if(zero) {
      kzero(zero, vol->block_size);
      vol_write_block(vol, tind, zero);
      kfree(zero);
    }
  }

  u32 *tind_block = kmalloc(vol->block_size);
  if(!tind_block)
    return 0;

  if(vol_read_block(vol, inode->i_block[EXT2_TIND_BLOCK], tind_block) < 0) {
    kfree(tind_block);
    return 0;
  }

  u32 dind_idx = file_block / (ptrs_per_block * ptrs_per_block);

  if(tind_block[dind_idx] == 0) {
    u32 dind = alloc_block(vol, preferred_group);
    if(dind == 0) {
      kfree(tind_block);
      return 0;
    }
    tind_block[dind_idx] = dind;
    inode->i_blocks += vol->block_size / 512;

    vol_write_block(vol, inode->i_block[EXT2_TIND_BLOCK], tind_block);

    u8 *zero = kmalloc(vol->block_size);
    if(zero) {
      kzero(zero, vol->block_size);
      vol_write_block(vol, dind, zero);
      kfree(zero);
    }
  }

  u32 dind_block_num = tind_block[dind_idx];
  kfree(tind_block);

  u32 *dind_block = kmalloc(vol->block_size);
  if(!dind_block)
    return 0;

  if(vol_read_block(vol, dind_block_num, dind_block) < 0) {
    kfree(dind_block);
    return 0;
  }

  u32 remaining = file_block % (ptrs_per_block * ptrs_per_block);
  u32 ind_idx   = remaining / ptrs_per_block;

  if(dind_block[ind_idx] == 0) {
    u32 ind = alloc_block(vol, preferred_group);
    if(ind == 0) {
      kfree(dind_block);
      return 0;
    }
    dind_block[ind_idx] = ind;
    inode->i_blocks += vol->block_size / 512;

    vol_write_block(vol, dind_block_num, dind_block);

    u8 *zero = kmalloc(vol->block_size);
    if(zero) {
      kzero(zero, vol->block_size);
      vol_write_block(vol, ind, zero);
      kfree(zero);
    }
  }

  u32 ind_block_num = dind_block[ind_idx];
  kfree(dind_block);

  u32 *ind_block = kmalloc(vol->block_size);
  if(!ind_block)
    return 0;

  if(vol_read_block(vol, ind_block_num, ind_block) < 0) {
    kfree(ind_block);
    return 0;
  }

  u32 ind_offset = remaining % ptrs_per_block;
  if(ind_block[ind_offset] == 0) {
    u32 block = alloc_block(vol, preferred_group);
    if(block == 0) {
      kfree(ind_block);
      return 0;
    }
    ind_block[ind_offset] = block;
    inode->i_blocks += vol->block_size / 512;

    vol_write_block(vol, ind_block_num, ind_block);

    u8 *zero = kmalloc(vol->block_size);
    if(zero) {
      kzero(zero, vol->block_size);
      vol_write_block(vol, block, zero);
      kfree(zero);
    }
  }

  u32 result = ind_block[ind_offset];
  kfree(ind_block);
  return result;
}

/**
 * @brief Free all blocks used by an inode.
 * @param vol Volume.
 * @param inode Inode.
 * @return 0 on success, negative on error.
 */
i64 free_inode_blocks(ext2_volume_t *vol, ext2_inode_t *inode)
{
  u32 ptrs_per_block = vol->block_size / 4;

  /* Free direct blocks */
  for(u32 i = 0; i < EXT2_NDIR_BLOCKS; i++) {
    if(inode->i_block[i]) {
      free_block(vol, inode->i_block[i]);
      inode->i_block[i] = 0;
    }
  }

  /* Free single indirect */
  if(inode->i_block[EXT2_IND_BLOCK]) {
    u32 *ind_block = kmalloc(vol->block_size);
    if(ind_block) {
      if(vol_read_block(vol, inode->i_block[EXT2_IND_BLOCK], ind_block) == 0) {
        for(u32 i = 0; i < ptrs_per_block; i++) {
          if(ind_block[i])
            free_block(vol, ind_block[i]);
        }
      }
      kfree(ind_block);
    }
    free_block(vol, inode->i_block[EXT2_IND_BLOCK]);
    inode->i_block[EXT2_IND_BLOCK] = 0;
  }

  /* Free double indirect */
  if(inode->i_block[EXT2_DIND_BLOCK]) {
    u32 *dind_block = kmalloc(vol->block_size);
    if(dind_block) {
      if(vol_read_block(vol, inode->i_block[EXT2_DIND_BLOCK], dind_block) ==
         0) {
        for(u32 i = 0; i < ptrs_per_block; i++) {
          if(dind_block[i]) {
            u32 *ind_block = kmalloc(vol->block_size);
            if(ind_block) {
              if(vol_read_block(vol, dind_block[i], ind_block) == 0) {
                for(u32 j = 0; j < ptrs_per_block; j++) {
                  if(ind_block[j])
                    free_block(vol, ind_block[j]);
                }
              }
              kfree(ind_block);
            }
            free_block(vol, dind_block[i]);
          }
        }
      }
      kfree(dind_block);
    }
    free_block(vol, inode->i_block[EXT2_DIND_BLOCK]);
    inode->i_block[EXT2_DIND_BLOCK] = 0;
  }

  /* Free triple indirect */
  if(inode->i_block[EXT2_TIND_BLOCK]) {
    u32 *tind_block = kmalloc(vol->block_size);
    if(tind_block) {
      if(vol_read_block(vol, inode->i_block[EXT2_TIND_BLOCK], tind_block) ==
         0) {
        for(u32 t = 0; t < ptrs_per_block; t++) {
          if(tind_block[t]) {
            u32 *dind_block = kmalloc(vol->block_size);
            if(dind_block) {
              if(vol_read_block(vol, tind_block[t], dind_block) == 0) {
                for(u32 d = 0; d < ptrs_per_block; d++) {
                  if(dind_block[d]) {
                    u32 *ind_block = kmalloc(vol->block_size);
                    if(ind_block) {
                      if(vol_read_block(vol, dind_block[d], ind_block) == 0) {
                        for(u32 i = 0; i < ptrs_per_block; i++) {
                          if(ind_block[i])
                            free_block(vol, ind_block[i]);
                        }
                      }
                      kfree(ind_block);
                    }
                    free_block(vol, dind_block[d]);
                  }
                }
              }
              kfree(dind_block);
            }
            free_block(vol, tind_block[t]);
          }
        }
      }
      kfree(tind_block);
    }
    free_block(vol, inode->i_block[EXT2_TIND_BLOCK]);
    inode->i_block[EXT2_TIND_BLOCK] = 0;
  }

  inode->i_blocks = 0;
  return 0;
}
