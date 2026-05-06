/**
 * @file src/kernel/sys/sys_mm.c
 * @brief Memory syscalls: `mmap`, `mprotect`, `munmap`, `brk`.
 *
 * Page alignment, prot → PTE flags, filling file-backed pages through the VFS, and anonymous
 * ranges via `vmm_map_range_alloc` where applicable.
 */

#include <alcor2/errno.h>
#include <alcor2/kstdlib.h>
#include <alcor2/mm/memory_layout.h>
#include <alcor2/mm/pmm.h>
#include <alcor2/proc/proc.h>
#include <alcor2/sys/internal.h>
#include <alcor2/fs/vfs.h>
#include <alcor2/mm/vmm.h>

#define MAP_SHARED    0x01
#define MAP_PRIVATE   0x02
#define MAP_FIXED     0x10
#define MAP_ANONYMOUS 0x20

#define PROT_NONE  0x00
#define PROT_READ  0x01
#define PROT_WRITE 0x02
#define PROT_EXEC  0x04

#define PAGE_MASK_LOCAL (PAGE_SIZE - 1)

static inline u64 page_align_down(u64 value)
{
  return value & ~PAGE_MASK_LOCAL;
}

static inline u64 page_align_up(u64 value)
{
  return (value + PAGE_MASK_LOCAL) & ~PAGE_MASK_LOCAL;
}

static u64 build_vmm_flags(u64 prot)
{
  u64 flags = VMM_PRESENT | VMM_USER;
  if(prot & PROT_WRITE)
    flags |= VMM_WRITE;
  return flags;
}

static void unmap_and_free_range(u64 start, u64 length)
{
  for(u64 off = 0; off < length; off += PAGE_SIZE) {
    u64 va   = start + off;
    u64 phys = vmm_get_phys(va);
    if(phys) {
      vmm_unmap(va);
      pmm_free((void *)phys);
    }
  }
}

static i64 map_zeroed_user_range(u64 base, u64 length, u64 map_flags)
{
  u64 count = length >> 12; /* length is already page-aligned */
  if(!vmm_map_range_alloc(base, count, map_flags | VMM_WRITE)) {
    unmap_and_free_range(base, length);
    return -ENOMEM;
  }
  return 0;
}

static void apply_final_prot(u64 base, u64 length, u64 map_flags)
{
  for(u64 off = 0; off < length; off += PAGE_SIZE) {
    u64 va   = base + off;
    u64 phys = vmm_get_phys(va);
    if(phys)
      vmm_map(va, phys, map_flags);
  }
}

static void fill_file_backed_pages(u64 base, u64 length, i64 fd, u64 offset)
{
  i64 saved_pos = vfs_seek(fd, 0, SEEK_CUR);
  vfs_seek(fd, (i64)offset, SEEK_SET);

  for(u64 map_off = 0; map_off < length;) {
    u64 page_virt  = base + map_off;
    u64 phys       = vmm_get_phys(page_virt);
    if(!phys)
      break;
    u8 *dst        = (u8 *)(phys + vmm_get_hhdm());
    u64 page_local = map_off & PAGE_MASK_LOCAL;
    u64 chunk      = PAGE_SIZE - page_local;
    if(chunk > length - map_off)
      chunk = length - map_off;

    i64 n = vfs_read(fd, dst + page_local, chunk);
    if(n <= 0)
      break;
    map_off += (u64)n;
  }

  vfs_seek(fd, saved_pos, SEEK_SET);
}

u64 sys_mmap(u64 addr, u64 length, u64 prot, u64 flags, u64 fd, u64 offset)
{
  if(length == 0)
    return (u64)-EINVAL;

  proc_t *p = proc_current();
  if(!p)
    return (u64)-ENOMEM;

  const bool fixed = (flags & MAP_FIXED) != 0;
  if(fixed && addr == 0)
    return (u64)-EINVAL;

  u64 aligned_len = page_align_up(length);
  if(aligned_len < length)
    return (u64)-ENOMEM;

  u64 base = (fixed && addr != 0) ? page_align_down(addr) : page_align_down(p->mmap_base);
  u64 end  = base + aligned_len;
  if(end < base || end > USER_SPACE_END)
    return (u64)-ENOMEM;

  bool is_anon = (flags & MAP_ANONYMOUS) != 0 || fd == (u64)-1;
  if(!is_anon && (offset & PAGE_MASK_LOCAL))
    return (u64)-EINVAL;

  u64 map_flags = build_vmm_flags(prot);

  /* MAP_FIXED requires "replace any existing mapping at this range with a
   * fresh zero-filled mapping" semantics. vmm_map_range_alloc skips pages
   * already PRESENT, so the new contents would be stale parent/peer data —
   * which is what causes mallocng's a_crash() (heap metadata mismatch).
   * Unmap the range first so the subsequent alloc sees only fresh pages. */
  if(fixed)
    unmap_and_free_range(base, aligned_len);

  i64 map_ret   = map_zeroed_user_range(base, aligned_len, map_flags);
  if(map_ret < 0)
    return (u64)map_ret;

  if(!fixed)
    p->mmap_base = end;

  if(!is_anon)
    fill_file_backed_pages(base, length, (i64)fd, offset);

  if(!(prot & PROT_WRITE))
    apply_final_prot(base, aligned_len, map_flags);

  return base;
}

u64 sys_mprotect(u64 addr, u64 len, u64 prot, u64 a4, u64 a5, u64 a6)
{
  (void)a4;
  (void)a5;
  (void)a6;

  if(len == 0)
    return 0;

  u64 aligned_start = page_align_down(addr);
  u64 aligned_end   = page_align_up(addr + len);
  u64 map_flags     = build_vmm_flags(prot);

  for(u64 va = aligned_start; va < aligned_end; va += PAGE_SIZE) {
    u64 phys = vmm_get_phys(va);
    if(phys)
      vmm_map(va, phys, map_flags);
  }
  return 0;
}

u64 sys_munmap(u64 addr, u64 len, u64 a3, u64 a4, u64 a5, u64 a6)
{
  (void)a3;
  (void)a4;
  (void)a5;
  (void)a6;

  if(len == 0)
    return 0;
  if(!vmm_is_user_range((void *)addr, len))
    return (u64)-EINVAL;

  u64 aligned_start = page_align_down(addr);
  u64 aligned_end   = page_align_up(addr + len);
  unmap_and_free_range(aligned_start, aligned_end - aligned_start);
  return 0;
}

u64 sys_brk(u64 addr, u64 a2, u64 a3, u64 a4, u64 a5, u64 a6)
{
  (void)a2;
  (void)a3;
  (void)a4;
  (void)a5;
  (void)a6;

  proc_t *p = proc_current();
  if(!p)
    return 0;

  if(addr == 0)
    return p->program_break;

  if(addr > p->program_break) {
    u64 old_end = page_align_up(p->program_break);
    u64 new_end = page_align_up(addr);

    for(u64 page_addr = old_end; page_addr < new_end; page_addr += PAGE_SIZE) {
      u64 phys = (u64)pmm_alloc();
      if(!phys)
        return p->program_break;
      vmm_map(page_addr, phys, VMM_PRESENT | VMM_WRITE | VMM_USER);
      kzero((u8 *)(phys + vmm_get_hhdm()), PAGE_SIZE);
    }
    p->program_break = addr;
  }
  return p->program_break;
}
