/**
 * @file include/alcor2/mm/vmm.h
 * @brief Virtual memory manager (paging).
 *
 * x86_64 page table management for kernel and per-process address spaces.
 */

#ifndef ALCOR2_VMM_H
#define ALCOR2_VMM_H

#include <alcor2/types.h>

/** @name Page table entry flags
 * @{ */
#define VMM_PRESENT (1ULL << 0)
#define VMM_WRITE   (1ULL << 1)
#define VMM_USER    (1ULL << 2)
#define VMM_NX      (1ULL << 63)
/** @} */

/** @brief Kernel higher-half base address. */
#define KERNEL_BASE 0xFFFFFFFF80000000ULL

/**
 * @brief Initialize the virtual memory manager.
 * @param hhdm_offset Higher-half direct map offset.
 */
void vmm_init(u64 hhdm_offset);

/**
 * @brief Map a virtual page to a physical page.
 * @param virt Virtual address.
 * @param phys Physical address.
 * @param flags Page flags (VMM_PRESENT, VMM_WRITE, etc.).
 */
void vmm_map(u64 virt, u64 phys, u64 flags);

/**
 * @brief Allocate and map a range of consecutive virtual pages.
 *
 * More efficient than calling vmm_map() in a loop: reads CR3 once and caches
 * intermediate page table levels across pages. Within the same 2 MB region
 * only one PT lookup is needed for up to 512 pages.
 * Skips already-mapped pages (safe for overlapping ELF segments).
 *
 * @param virt_start First virtual address (page-aligned).
 * @param count      Number of 4 KiB pages.
 * @param flags      Page flags (VMM_WRITE, VMM_USER, …); VMM_PRESENT is added.
 * @return true on success, false if physical memory is exhausted.
 */
bool vmm_map_range_alloc(u64 virt_start, u64 count, u64 flags);

/**
 * @brief Unmap a virtual page.
 * @param virt Virtual address.
 */
void vmm_unmap(u64 virt);

/**
 * @brief Get physical address for virtual address.
 * @param virt Virtual address.
 * @return Physical address or 0 if not mapped.
 */
u64 vmm_get_phys(u64 virt);

/**
 * @brief Get the full page table entry for a virtual address (debug).
 * @param virt Virtual address.
 * @return PTE value.
 */
u64 vmm_get_pte(u64 virt);

/**
 * @brief Switch to a different page table.
 * @param pml4_phys Physical address of PML4.
 */
void vmm_switch(u64 pml4_phys);

/**
 * @brief Get the higher-half direct map offset.
 * @return HHDM offset.
 */
u64 vmm_get_hhdm(void);

/**
 * @brief Create a new address space (new PML4).
 * @return Physical address of the new PML4.
 */
u64 vmm_create_address_space(void);

/**
 * @brief Map a page in a specific address space.
 * @param pml4_phys Physical address of PML4.
 * @param virt Virtual address.
 * @param phys Physical address.
 * @param flags Page flags.
 */
void vmm_map_in(u64 pml4_phys, u64 virt, u64 phys, u64 flags);

/**
 * @brief Get physical address in a specific address space.
 * @param pml4_phys Physical address of PML4.
 * @param virt Virtual address.
 * @return Physical address or 0 if not mapped.
 */
u64 vmm_get_phys_in(u64 pml4_phys, u64 virt);

/**
 * @brief Clone user mappings for fork.
 * @param src_pml4 Source PML4 physical address.
 * @return New PML4 physical address with cloned mappings.
 */
u64 vmm_clone_address_space(u64 src_pml4_phys);

/**
 * @brief Destroy all user mappings in an address space.
 * @param pml4_phys Physical address of PML4.
 */
void vmm_destroy_user_mappings(u64 pml4_phys);

/**
 * @brief Free every user-space mapping in an address space (pages, PT, PD,
 * PDPT) and zero the user-space PML4 entries, but keep the PML4 page itself
 * intact.
 *
 * Used by @c execve to wipe the calling process's old image while preserving
 * the address space so the new image can be loaded into the same @c cr3.
 *
 * @param pml4_phys Physical address of PML4.
 */
void vmm_clear_user_mappings(u64 pml4_phys);

/**
 * @brief Get the current CR3 (PML4) value.
 * @return Current PML4 physical address.
 */
u64 vmm_get_current_pml4(void);

/**
 * @brief Convert physical address to virtual (HHDM).
 * @param phys Physical address.
 * @return Virtual address in HHDM region.
 */
static inline void *phys_to_virt(u64 phys)
{
  return (void *)(phys + vmm_get_hhdm());
}

/**
 * @brief Convert virtual address to physical (HHDM).
 * @param virt Virtual address in HHDM region.
 * @return Physical address.
 */
// cppcheck-suppress unusedFunction
static inline u64 virt_to_phys(void *virt)
{
  return (u64)virt - vmm_get_hhdm();
}

/**
 * @brief Check if pointer is in user space.
 * @param ptr Pointer to check.
 * @return true if in user address range.
 */
bool vmm_is_user_ptr(const void *ptr);

/**
 * @brief Check if range is in user space.
 * @param ptr  Start of range.
 * @param size Size in bytes.
 * @return true if entire range is in user address space.
 */
bool vmm_is_user_range(const void *ptr, u64 size);

#endif
