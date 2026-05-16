/**
 * @file src/mm/vmm.c
 * @brief x86_64 virtual memory manager (PML4 → PT, address-space clone, tear
 * down user maps).
 *
 * Provides `vmm_map` / `vmm_unmap`, fork-time clone, and `vmm_map_range_alloc`
 * to map a run of pages while reusing page-table levels already walked.
 */

#include <alcor2/kstdlib.h>
#include <alcor2/mm/memory_layout.h>
#include <alcor2/mm/pmm.h>
#include <alcor2/mm/vmm.h>
#include <alcor2/types.h>

static u64 *kernel_pml4;
static u64  kernel_pml4_phys;
static u64  hhdm;

/**
 * @brief Get or create next page table level.
 * @param table Current table.
 * @param index Entry index.
 * @param create Create if missing.
 * @param flags Flags to apply.
 * @return Next level table or NULL.
 */
static u64 *get_next_level(u64 *table, u64 index, bool create, u64 flags)
{
  if(table[index] & VMM_PRESENT) {
    /* If user access is needed, ensure it's set on existing entry */
    if((flags & VMM_USER) && !(table[index] & VMM_USER)) {
      table[index] |= VMM_USER;
    }
    return (u64 *)phys_to_virt(table[index] & PAGE_FRAME_MASK);
  }

  if(!create)
    return 0;

  void *page = pmm_alloc();
  if(!page)
    return 0;

  u64 *new_table = (u64 *)phys_to_virt((u64)page);
  kzero(new_table, 512 * sizeof(u64));

  /* Propagate USER flag to intermediate page table levels */
  u64 entry_flags = VMM_PRESENT | VMM_WRITE;
  if(flags & VMM_USER) {
    entry_flags |= VMM_USER;
  }
  table[index] = (u64)page | entry_flags;
  return new_table;
}

/**
 * @brief Initialize the virtual memory manager.
 *
 * Creates a new kernel PML4, copies the higher-half mappings from the
 * bootloader's PML4, and switches to the new page table.
 *
 * @param hhdm_offset Higher-half direct map offset from Limine.
 */
void vmm_init(u64 hhdm_offset)
{
  hhdm = hhdm_offset;

  void *pml4_phys  = pmm_alloc();
  kernel_pml4_phys = (u64)pml4_phys;
  kernel_pml4      = (u64 *)phys_to_virt((u64)pml4_phys);

  kzero(kernel_pml4, 512 * sizeof(u64));

  u64 cr3;
  __asm__ volatile("mov %%cr3, %0" : "=r"(cr3));
  const u64 *const old_pml4 = (const u64 *)phys_to_virt(cr3 & PAGE_FRAME_MASK);

  for(int i = 256; i < 512; i++) {
    kernel_pml4[i] = old_pml4[i];
  }

  vmm_switch((u64)pml4_phys);
}

/**
 * @brief Map a virtual page to a physical page in the current address space.
 *
 * Creates intermediate page table levels as needed. The mapping is made
 * in the currently active address space (CR3).
 *
 * @param virt Virtual address to map (will be page-aligned).
 * @param phys Physical address to map to (will be page-aligned).
 * @param flags Page flags (VMM_PRESENT, VMM_WRITE, VMM_USER, VMM_NX).
 */
void vmm_map(u64 virt, u64 phys, u64 flags)
{
  u64 cr3;
  __asm__ volatile("mov %%cr3, %0" : "=r"(cr3));
  u64 *pml4 = (u64 *)phys_to_virt(cr3 & PAGE_FRAME_MASK);

  u64 *pdpt =
      get_next_level(pml4, (virt >> 39) & PAGE_TABLE_INDEX_MASK, true, flags);
  if(!pdpt)
    return;
  u64 *pd =
      get_next_level(pdpt, (virt >> 30) & PAGE_TABLE_INDEX_MASK, true, flags);
  if(!pd)
    return;
  u64 *pt =
      get_next_level(pd, (virt >> 21) & PAGE_TABLE_INDEX_MASK, true, flags);
  if(!pt)
    return;

  pt[(virt >> 12) & PAGE_TABLE_INDEX_MASK] =
      (phys & PAGE_FRAME_MASK) | flags | VMM_PRESENT;
  __asm__ volatile("invlpg (%0)" ::"r"(virt) : "memory");
}

/**
 * @brief Allocate and map a range of consecutive virtual pages.
 *
 * Reads CR3 once and caches intermediate page table levels across pages.
 * Within the same 2 MB region (same PT), only a single PT lookup is needed
 * for up to 512 consecutive pages — greatly reducing the per-page walk cost
 * compared to calling vmm_map() in a loop.
 *
 * Skips pages that are already mapped (safe for overlapping ELF segments).
 * Returns false and stops if physical memory is exhausted.
 *
 * @param virt_start First virtual address to map (page-aligned).
 * @param count      Number of 4 KiB pages to map.
 * @param flags      Page flags (VMM_WRITE, VMM_USER, …); VMM_PRESENT is added.
 * @return true on full success, false if pmm_alloc() failed.
 */
bool vmm_map_range_alloc(u64 virt_start, u64 count, u64 flags)
{
  u64 cr3;
  __asm__ volatile("mov %%cr3, %0" : "=r"(cr3));
  u64 *pml4 = (u64 *)phys_to_virt(cr3 & PAGE_FRAME_MASK);

  /* Cached pointers — invalidated when the corresponding index changes */
  u64  c_pml4_idx = ~0ULL, c_pdpt_idx = ~0ULL, c_pd_idx = ~0ULL;
  u64 *pdpt = NULL, *pd = NULL, *pt = NULL;

  for(u64 i = 0; i < count; i++) {
    u64 virt     = virt_start + (i << 12);
    u64 pml4_idx = (virt >> 39) & PAGE_TABLE_INDEX_MASK;
    u64 pdpt_idx = (virt >> 30) & PAGE_TABLE_INDEX_MASK;
    u64 pd_idx   = (virt >> 21) & PAGE_TABLE_INDEX_MASK;
    u64 pt_idx   = (virt >> 12) & PAGE_TABLE_INDEX_MASK;

    /* Only re-walk levels that changed */
    if(pml4_idx != c_pml4_idx) {
      pdpt = get_next_level(pml4, pml4_idx, true, flags);
      if(!pdpt)
        return false;
      c_pml4_idx = pml4_idx;
      c_pdpt_idx = ~0ULL;
    }
    if(pdpt_idx != c_pdpt_idx) {
      pd = get_next_level(pdpt, pdpt_idx, true, flags);
      if(!pd)
        return false;
      c_pdpt_idx = pdpt_idx;
      c_pd_idx   = ~0ULL;
    }
    if(pd_idx != c_pd_idx) {
      pt = get_next_level(pd, pd_idx, true, flags);
      if(!pt)
        return false;
      c_pd_idx = pd_idx;
    }

    /* Skip pages already mapped (handles overlapping ELF segments) */
    if(pt[pt_idx] & VMM_PRESENT)
      continue;

    void *phys = pmm_alloc();
    if(!phys)
      return false;

    kzero(phys_to_virt((u64)phys), PAGE_SIZE);
    pt[pt_idx] = ((u64)phys & PAGE_FRAME_MASK) | flags | VMM_PRESENT;
    __asm__ volatile("invlpg (%0)" ::"r"(virt) : "memory");
  }
  return true;
}

/**
 * @brief Unmap a virtual page from the current address space.
 *
 * Removes the mapping and invalidates the TLB entry for the page.
 * Does not free the underlying physical page.
 *
 * @param virt Virtual address to unmap.
 */
// cppcheck-suppress unusedFunction
void vmm_unmap(u64 virt)
{
  /* Get current PML4 (could be kernel or process PML4) */
  u64 cr3;
  __asm__ volatile("mov %%cr3, %0" : "=r"(cr3));
  u64 *pml4 = (u64 *)phys_to_virt(cr3 & PAGE_FRAME_MASK);

  u64  pml4_idx = (virt >> 39) & PAGE_TABLE_INDEX_MASK;
  u64  pdpt_idx = (virt >> 30) & PAGE_TABLE_INDEX_MASK;
  u64  pd_idx   = (virt >> 21) & PAGE_TABLE_INDEX_MASK;
  u64  pt_idx   = (virt >> 12) & PAGE_TABLE_INDEX_MASK;

  u64 *pdpt = get_next_level(pml4, pml4_idx, false, 0);
  if(!pdpt)
    return;

  u64 *pd = get_next_level(pdpt, pdpt_idx, false, 0);
  if(!pd)
    return;

  u64 *pt = get_next_level(pd, pd_idx, false, 0);
  if(!pt)
    return;

  pt[pt_idx] = 0;

  __asm__ volatile("invlpg (%0)" : : "r"(virt) : "memory");
}

/**
 * @brief Translate virtual address to physical address in current address
 * space.
 *
 * Walks the page tables to find the physical address mapped to the given
 * virtual address.
 *
 * @param virt Virtual address to translate.
 * @return Physical address, or 0 if not mapped or not present.
 */
u64 vmm_get_phys(u64 virt)
{
  /* Get current PML4 */
  u64 cr3;
  __asm__ volatile("mov %%cr3, %0" : "=r"(cr3));
  u64 *pml4 = (u64 *)phys_to_virt(cr3 & PAGE_FRAME_MASK);

  u64  pml4_idx = (virt >> 39) & PAGE_TABLE_INDEX_MASK;
  u64  pdpt_idx = (virt >> 30) & PAGE_TABLE_INDEX_MASK;
  u64  pd_idx   = (virt >> 21) & PAGE_TABLE_INDEX_MASK;
  u64  pt_idx   = (virt >> 12) & PAGE_TABLE_INDEX_MASK;

  u64 *pdpt = get_next_level(pml4, pml4_idx, false, 0);
  if(!pdpt)
    return 0;

  u64 *pd = get_next_level(pdpt, pdpt_idx, false, 0);
  if(!pd)
    return 0;

  const u64 *pt = get_next_level(pd, pd_idx, false, 0);
  if(!pt)
    return 0;

  if(!(pt[pt_idx] & VMM_PRESENT))
    return 0;

  return (pt[pt_idx] & PAGE_FRAME_MASK) | (virt & PAGE_OFFSET_MASK);
}

/**
 * @brief Get the full page table entry for a virtual address (debug).
 *
 * Returns the complete PTE value including flags, useful for debugging
 * page table issues.
 *
 * @param virt Virtual address.
 * @return Complete PTE value, or 0 if page tables don't exist.
 */
// cppcheck-suppress unusedFunction
u64 vmm_get_pte(u64 virt)
{
  u64 cr3;
  __asm__ volatile("mov %%cr3, %0" : "=r"(cr3));
  u64 *pml4 = (u64 *)phys_to_virt(cr3 & PAGE_FRAME_MASK);

  u64  pml4_idx = (virt >> 39) & PAGE_TABLE_INDEX_MASK;
  u64  pdpt_idx = (virt >> 30) & PAGE_TABLE_INDEX_MASK;
  u64  pd_idx   = (virt >> 21) & PAGE_TABLE_INDEX_MASK;
  u64  pt_idx   = (virt >> 12) & PAGE_TABLE_INDEX_MASK;

  u64 *pdpt = get_next_level(pml4, pml4_idx, false, 0);
  if(!pdpt)
    return 0;

  u64 *pd = get_next_level(pdpt, pdpt_idx, false, 0);
  if(!pd)
    return 0;

  const u64 *pt = get_next_level(pd, pd_idx, false, 0);
  if(!pt)
    return 0;

  return pt[pt_idx];
}

/**
 * @brief Switch to a different page table.
 *
 * Loads the specified PML4 into CR3, switching to a different address space.
 *
 * @param pml4_phys Physical address of the PML4 to switch to.
 */
void vmm_switch(u64 pml4_phys)
{
  __asm__ volatile("mov %0, %%cr3" : : "r"(pml4_phys) : "memory");
}

/**
 * @brief Get the higher-half direct map offset.
 *
 * Returns the offset used to convert physical addresses to virtual addresses
 * in the higher-half direct map region.
 *
 * @return HHDM offset.
 */
u64 vmm_get_hhdm(void)
{
  return hhdm;
}

/**
 * @brief Create a new address space (PML4) for a process.
 *
 * Allocates a new PML4 table, clears user-space entries (0-255),
 * and shares kernel-space entries (256-511) from the kernel PML4.
 *
 * @return Physical address of new PML4, or 0 on failure.
 */
u64 vmm_create_address_space(void)
{
  void *pml4_phys = pmm_alloc();
  if(!pml4_phys)
    return 0;

  u64 *new_pml4 = (u64 *)phys_to_virt((u64)pml4_phys);

  /* Clear user-space entries (0-255) */
  /* Clear user-space entries (0-255) */
  kzero(new_pml4, 256 * sizeof(u64));

  /* Share kernel-space entries (256-511) from kernel PML4 */
  for(int i = 256; i < 512; i++) {
    new_pml4[i] = kernel_pml4[i];
  }

  return (u64)pml4_phys;
}

/**
 * @brief Map a page in a specific address space.
 *
 * Maps a virtual page to a physical page in the address space specified by
 * pml4_phys. Creates intermediate page table levels as needed.
 *
 * @param pml4_phys Physical address of target PML4.
 * @param virt Virtual address to map.
 * @param phys Physical address to map to.
 * @param flags Page flags (VMM_PRESENT, VMM_WRITE, VMM_USER, etc.).
 */
void vmm_map_in(u64 pml4_phys, u64 virt, u64 phys, u64 flags)
{
  u64 *pml4 = (u64 *)phys_to_virt(pml4_phys);

  u64  pml4_idx = (virt >> 39) & PAGE_TABLE_INDEX_MASK;
  u64  pdpt_idx = (virt >> 30) & PAGE_TABLE_INDEX_MASK;
  u64  pd_idx   = (virt >> 21) & PAGE_TABLE_INDEX_MASK;
  u64  pt_idx   = (virt >> 12) & PAGE_TABLE_INDEX_MASK;

  u64 *pdpt = get_next_level(pml4, pml4_idx, true, flags);
  if(!pdpt)
    return;

  u64 *pd = get_next_level(pdpt, pdpt_idx, true, flags);
  if(!pd)
    return;

  u64 *pt = get_next_level(pd, pd_idx, true, flags);
  if(!pt)
    return;

  pt[pt_idx] = (phys & PAGE_FRAME_MASK) | flags | VMM_PRESENT;
}

/**
 * @brief Get current PML4 physical address from CR3.
 * @return Physical address of current PML4 (with flags masked out).
 */
u64 vmm_get_current_pml4(void)
{
  u64 cr3;
  __asm__ volatile("mov %%cr3, %0" : "=r"(cr3));
  return cr3 & PAGE_FRAME_MASK;
}

/**
 * @brief Get physical address for virtual address in a specific address space.
 *
 * Walks the page tables of the specified address space to find the physical
 * address mapped to the given virtual address.
 *
 * @param pml4_phys Physical address of PML4 to search.
 * @param virt Virtual address to resolve.
 * @return Physical address, or 0 if not mapped or not present.
 */
// cppcheck-suppress unusedFunction
u64 vmm_get_phys_in(u64 pml4_phys, u64 virt)
{
  u64 *pml4 = (u64 *)phys_to_virt(pml4_phys);

  u64  pml4_idx = (virt >> 39) & PAGE_TABLE_INDEX_MASK;
  u64  pdpt_idx = (virt >> 30) & PAGE_TABLE_INDEX_MASK;
  u64  pd_idx   = (virt >> 21) & PAGE_TABLE_INDEX_MASK;
  u64  pt_idx   = (virt >> 12) & PAGE_TABLE_INDEX_MASK;

  u64 *pdpt = get_next_level(pml4, pml4_idx, false, 0);
  if(!pdpt)
    return 0;

  u64 *pd = get_next_level(pdpt, pdpt_idx, false, 0);
  if(!pd)
    return 0;

  const u64 *pt = get_next_level(pd, pd_idx, false, 0);
  if(!pt)
    return 0;

  if(!(pt[pt_idx] & VMM_PRESENT))
    return 0;

  return pt[pt_idx] & PAGE_FRAME_MASK;
}

/**
 * @brief Clone an address space for fork().
 *
 * Creates a new PML4, shares kernel-half entries (256..511) with the source,
 * and eagerly duplicates every user-half leaf: each PT/PD/PDPT level is
 * walked and a fresh physical page is allocated for each present leaf,
 * with its content @c kmemcpy'd from the source. Walk levels themselves
 * are also freshly allocated by @c vmm_map_in.
 *
 * NOT a copy-on-write implementation — every page is materialised at fork
 * time. Adding COW would mark both sides read-only and fault-trap the
 * first write; today's userland tolerates the eager cost.
 *
 * @param src_pml4_phys Physical address of source PML4 to clone.
 * @return Physical address of new PML4, or 0 on failure.
 */
u64 vmm_clone_address_space(u64 src_pml4_phys)
{
  /* Create new address space with kernel mappings */
  u64 dst_pml4_phys = vmm_create_address_space();
  if(!dst_pml4_phys)
    return 0;

  const u64 *src_pml4 = (const u64 *)phys_to_virt(src_pml4_phys);

  /* Walk user-space entries (0-255) and copy mappings */
  for(int pml4_idx = 0; pml4_idx < 256; pml4_idx++) {
    if(!(src_pml4[pml4_idx] & VMM_PRESENT))
      continue;

    const u64 *src_pdpt =
        (const u64 *)phys_to_virt(src_pml4[pml4_idx] & PAGE_FRAME_MASK);

    for(int pdpt_idx = 0; pdpt_idx < 512; pdpt_idx++) {
      if(!(src_pdpt[pdpt_idx] & VMM_PRESENT))
        continue;

      const u64 *src_pd =
          (const u64 *)phys_to_virt(src_pdpt[pdpt_idx] & PAGE_FRAME_MASK);

      for(int pd_idx = 0; pd_idx < 512; pd_idx++) {
        if(!(src_pd[pd_idx] & VMM_PRESENT))
          continue;

        const u64 *src_pt =
            (const u64 *)phys_to_virt(src_pd[pd_idx] & PAGE_FRAME_MASK);

        for(int pt_idx = 0; pt_idx < 512; pt_idx++) {
          if(!(src_pt[pt_idx] & VMM_PRESENT))
            continue;

          u64 src_phys = src_pt[pt_idx] & PAGE_FRAME_MASK;
          u64 flags    = src_pt[pt_idx] & PAGE_OFFSET_MASK;

          /* Calculate virtual address */
          u64 virt = ((u64)pml4_idx << 39) | ((u64)pdpt_idx << 30) |
                     ((u64)pd_idx << 21) | ((u64)pt_idx << 12);

          /* Allocate new physical page */
          void *dst_page = pmm_alloc();
          if(!dst_page) {
            /* Out of memory - should cleanup but simplified for now */
            return 0;
          }

          /* Copy page contents */
          const void *src_data = (const void *)phys_to_virt(src_phys);
          void       *dst_data = (void *)phys_to_virt((u64)dst_page);
          kmemcpy(dst_data, src_data, PAGE_SIZE);

          /* Map in destination address space */
          vmm_map_in(dst_pml4_phys, virt, (u64)dst_page, flags);
        }
      }
    }
  }

  return dst_pml4_phys;
}

/**
 * @brief Walk the user half (PML4 entries 0..255) of @p pml4_phys and free
 *        every leaf page plus every PT/PD/PDPT level on the way up.
 *
 * @param pml4_phys     PML4 physical address to recurse into.
 * @param zero_entries  When @c true, also zeroes the PML4 entries after the
 *                      walk. Required by @c vmm_clear_user_mappings (the
 *                      PML4 stays in use); skipped by
 *                      @c vmm_destroy_user_mappings (the PML4 page itself
 *                      is freed afterwards, so its contents do not matter).
 */
static void user_mappings_walk_and_free(u64 pml4_phys, bool zero_entries)
{
  u64 *pml4 = (u64 *)phys_to_virt(pml4_phys);

  for(int pml4_idx = 0; pml4_idx < 256; pml4_idx++) {
    if(!(pml4[pml4_idx] & VMM_PRESENT))
      continue;

    const u64 *pdpt =
        (const u64 *)phys_to_virt(pml4[pml4_idx] & PAGE_FRAME_MASK);

    for(int pdpt_idx = 0; pdpt_idx < 512; pdpt_idx++) {
      if(!(pdpt[pdpt_idx] & VMM_PRESENT))
        continue;

      const u64 *pd =
          (const u64 *)phys_to_virt(pdpt[pdpt_idx] & PAGE_FRAME_MASK);

      for(int pd_idx = 0; pd_idx < 512; pd_idx++) {
        if(!(pd[pd_idx] & VMM_PRESENT))
          continue;

        const u64 *pt = (const u64 *)phys_to_virt(pd[pd_idx] & PAGE_FRAME_MASK);

        for(int pt_idx = 0; pt_idx < 512; pt_idx++) {
          if(!(pt[pt_idx] & VMM_PRESENT))
            continue;
          pmm_free((void *)(pt[pt_idx] & PAGE_FRAME_MASK));
        }
        pmm_free((void *)(pd[pd_idx] & PAGE_FRAME_MASK));
      }
      pmm_free((void *)(pdpt[pdpt_idx] & PAGE_FRAME_MASK));
    }
    pmm_free((void *)(pml4[pml4_idx] & PAGE_FRAME_MASK));

    if(zero_entries)
      pml4[pml4_idx] = 0;
  }
}

void vmm_destroy_user_mappings(u64 pml4_phys)
{
  user_mappings_walk_and_free(pml4_phys, false);
  pmm_free((void *)pml4_phys);
}

void vmm_clear_user_mappings(u64 pml4_phys)
{
  user_mappings_walk_and_free(pml4_phys, true);
  /* Flush TLB so the now-cleared user mappings don't linger in cache. */
  __asm__ volatile("mov %0, %%cr3" : : "r"(pml4_phys) : "memory");
}

/**
 * @brief Check if a pointer is in user space.
 * @param ptr Pointer to check.
 * @return true if address is in user space range.
 */
bool vmm_is_user_ptr(const void *ptr)
{
  return (u64)ptr < USER_SPACE_END;
}

/**
 * @brief Check if a memory range is entirely in user space.
 * @param ptr Start of range.
 * @param size Size of range.
 * @return true if range is valid and in user space.
 */
bool vmm_is_user_range(const void *ptr, u64 size)
{
  u64 start = (u64)ptr;
  u64 end   = start + size;

  /* Check for overflow */
  if(end < start)
    return false;

  return end <= USER_SPACE_END;
}
