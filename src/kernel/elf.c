/**
 * @file src/kernel/elf.c
 * @brief ELF64 executable loader.
 */

#include <alcor2/console.h>
#include <alcor2/elf.h>
#include <alcor2/pmm.h>
#include <alcor2/vmm.h>
#include <alcor2/memory_layout.h>

/**
 * @brief Copy memory region.
 * @param dst Destination buffer.
 * @param src Source buffer.
 * @param size Number of bytes to copy.
 */
static void memcpy(void *dst, const void *src, u64 size)
{
  u8       *d = (u8 *)dst;
  const u8 *s = (const u8 *)src;
  for(u64 i = 0; i < size; i++) {
    d[i] = s[i];
  }
}

/**
 * @brief Set memory to a specific value.
 * @param dst Destination buffer.
 * @param val Value to set (byte).
 * @param size Number of bytes to set.
 */
static void memset(void *dst, u8 val, u64 size)
{
  u8 *d = (u8 *)dst;
  for(u64 i = 0; i < size; i++) {
    d[i] = val;
  }
}

/**
 * @brief Validate ELF64 header.
 * 
 * Checks ELF magic number, class (64-bit), endianness (little-endian),
 * file type (executable or dynamic), and machine architecture (x86_64).
 * 
 * @param ehdr Pointer to ELF header.
 * @return true if valid, false otherwise.
 */
bool elf_validate(const Elf64_Ehdr *ehdr)
{
  /* Check magic number */
  if(ehdr->e_ident[EI_MAG0] != 0x7F || ehdr->e_ident[EI_MAG1] != 'E' ||
     ehdr->e_ident[EI_MAG2] != 'L' || ehdr->e_ident[EI_MAG3] != 'F') {
    console_print("[ELF] Invalid magic\n");
    return false;
  }

  /* Check 64-bit */
  if(ehdr->e_ident[EI_CLASS] != ELFCLASS64) {
    console_print("[ELF] Not 64-bit\n");
    return false;
  }

  /* Check little-endian */
  if(ehdr->e_ident[EI_DATA] != ELFDATA2LSB) {
    console_print("[ELF] Not little-endian\n");
    return false;
  }

  /* Check executable */
  if(ehdr->e_type != ET_EXEC && ehdr->e_type != ET_DYN) {
    console_print("[ELF] Not executable\n");
    return false;
  }

  /* Check x86_64 */
  if(ehdr->e_machine != EM_X86_64) {
    console_print("[ELF] Not x86_64\n");
    return false;
  }

  return true;
}

/**
 * @brief Load an ELF64 executable into memory.
 * 
 * Validates the ELF header, iterates through program headers, allocates
 * and maps physical pages for PT_LOAD segments, copies segment data,
 * and fills the elf_info_t structure with entry point and memory range.
 * 
 * Must be called while in the target address space.
 * 
 * @param data Pointer to ELF file data.
 * @param size Size of ELF file in bytes.
 * @param info Output structure for entry point and memory range.
 * @return 0 on success, -1 on failure.
 */
int elf_load(const void *data, u64 size, elf_info_t *info)
{
  const Elf64_Ehdr *ehdr = (const Elf64_Ehdr *)data;

  /* Validate header */
  if(size < sizeof(Elf64_Ehdr)) {
    console_print("[ELF] File too small\n");
    return -1;
  }

  if(!elf_validate(ehdr)) {
    return -1;
  }

  /* Get program headers */
  if(ehdr->e_phoff == 0 || ehdr->e_phnum == 0) {
    console_print("[ELF] No program headers\n");
    return -1;
  }

  const Elf64_Phdr *phdrs = (const Elf64_Phdr *)((u8 *)data + ehdr->e_phoff);

  info->entry = ehdr->e_entry;
  info->base  = ELF_BASE_SENTINEL;
  info->end   = 0;

  /* Load each PT_LOAD segment */
  for(u16 i = 0; i < ehdr->e_phnum; i++) {
    const Elf64_Phdr *phdr = &phdrs[i];

    if(phdr->p_type != PT_LOAD) {
      continue;
    }

    if(phdr->p_memsz == 0) {
      continue;
    }

    u64 vaddr  = phdr->p_vaddr;
    u64 memsz  = phdr->p_memsz;
    u64 filesz = phdr->p_filesz;
    u64 offset = phdr->p_offset;

    /* Update base/end */
    if(vaddr < info->base) {
      info->base = vaddr;
    }
    if(vaddr + memsz > info->end) {
      info->end = vaddr + memsz;
    }

    /* Allocate and map pages for this segment */
    u64 page_start = vaddr & ~PAGE_OFFSET_MASK;
    u64 page_end   = (vaddr + memsz + PAGE_OFFSET_MASK) & ~PAGE_OFFSET_MASK;
    u64 num_pages  = (page_end - page_start) / PAGE_SIZE;

    for(u64 p = 0; p < num_pages; p++) {
      u64 page_vaddr = page_start + p * PAGE_SIZE;

      /* Check if already mapped */
      if(vmm_get_phys(page_vaddr) != 0) {
        continue;
      }

      /* Allocate physical page */
      void *phys = pmm_alloc();
      if(!phys) {
        console_print("[ELF] Out of memory\n");
        return -1;
      }

      /* Map as user RWX (simplified - could use p_flags) */
      u64 flags = VMM_PRESENT | VMM_WRITE | VMM_USER;
      vmm_map(page_vaddr, (u64)phys, flags);

      /* Zero the page */
      void *page_ptr = (void *)((u64)phys + vmm_get_hhdm());
      memset(page_ptr, 0, PAGE_SIZE);
    }

    /* Copy segment data - must handle page boundaries! */
    if(filesz > 0) {
      const u8 *src       = (const u8 *)data + offset;
      u64       remaining = filesz;
      u64       dst_vaddr = vaddr;

      while(remaining > 0) {
        /* Get physical address for current page */
        u64 dst_phys = vmm_get_phys(dst_vaddr);
        u8 *dst      = (u8 *)(dst_phys + vmm_get_hhdm());

        /* Calculate how much we can copy in this page */
        u64 page_offset   = dst_vaddr & PAGE_OFFSET_MASK;
        u64 bytes_in_page = PAGE_SIZE - page_offset;
        u64 to_copy = remaining < bytes_in_page ? remaining : bytes_in_page;

        memcpy(dst, src, to_copy);

        src += to_copy;
        dst_vaddr += to_copy;
        remaining -= to_copy;
      }
    }

    console_printf(
        "[ELF] Loaded segment at 0x%x (%d bytes)\n", (unsigned)vaddr, (int)memsz
    );
  }

  if(info->base == ELF_BASE_SENTINEL) {
    console_print("[ELF] No loadable segments\n");
    return -1;
  }

  console_printf("[ELF] Entry point: 0x%x\n", (unsigned)info->entry);

  return 0;
}
