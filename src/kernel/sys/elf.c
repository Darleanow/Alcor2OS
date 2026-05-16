/**
 * @file src/kernel/sys/elf.c
 * @brief ELF64 executable loader: validation, PT_LOAD mapping, file or memory
 * copy, user entry.
 *
 * Uses `vmm_map_range_alloc` for anonymous page runs and `vfs_read` for
 * `elf_load_fd` (no kernel buffer sized to the whole file).
 */

#include <alcor2/drivers/console.h>
#include <alcor2/fs/vfs.h>
#include <alcor2/kstdlib.h>
#include <alcor2/mm/heap.h>
#include <alcor2/mm/memory_layout.h>
#include <alcor2/mm/pmm.h>
#include <alcor2/mm/vmm.h>
#include <alcor2/proc/elf.h>

static void elf_info_init(const Elf64_Ehdr *ehdr, elf_info_t *info)
{
  info->entry = ehdr->e_entry;
  info->base  = ELF_BASE_SENTINEL;
  info->end   = 0;
  info->phdr  = 0;
  info->phent = ehdr->e_phentsize;
  info->phnum = ehdr->e_phnum;
}

static void elf_info_track_segment(
    const Elf64_Ehdr *ehdr, const Elf64_Phdr *phdr, elf_info_t *info
)
{
  u64 vaddr = phdr->p_vaddr;
  u64 memsz = phdr->p_memsz;

  if(vaddr < info->base) {
    info->base = vaddr;
    if(phdr->p_offset == 0)
      info->phdr = vaddr + ehdr->e_phoff;
  }
  if(vaddr + memsz > info->end)
    info->end = vaddr + memsz;
}

static int elf_map_segment_pages(u64 vaddr, u64 memsz)
{
  u64 page_start = vaddr & ~PAGE_OFFSET_MASK;
  u64 page_end   = (vaddr + memsz + PAGE_OFFSET_MASK) & ~PAGE_OFFSET_MASK;
  u64 count      = (page_end - page_start) >> 12;

  if(!vmm_map_range_alloc(page_start, count, VMM_WRITE | VMM_USER)) {
    console_print("[ELF] Out of memory\n");
    return -1;
  }
  return 0;
}

static void elf_copy_to_mapped(u64 dst_vaddr, const u8 *src, u64 size)
{
  u64 remaining = size;
  while(remaining > 0) {
    u64 dst_phys = vmm_get_phys(dst_vaddr);
    u8 *dst      = (u8 *)(dst_phys + vmm_get_hhdm());

    u64 page_offset   = dst_vaddr & PAGE_OFFSET_MASK;
    u64 bytes_in_page = PAGE_SIZE - page_offset;
    u64 to_copy       = remaining < bytes_in_page ? remaining : bytes_in_page;

    kmemcpy(dst, src, to_copy);
    src += to_copy;
    dst_vaddr += to_copy;
    remaining -= to_copy;
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

  elf_info_init(ehdr, info);

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

    elf_info_track_segment(ehdr, phdr, info);

    if(elf_map_segment_pages(vaddr, memsz) < 0)
      return -1;

    if(filesz > 0)
      elf_copy_to_mapped(vaddr, (const u8 *)data + offset, filesz);
  }

  if(info->base == ELF_BASE_SENTINEL) {
    console_print("[ELF] No loadable segments\n");
    return -1;
  }

  return 0;
}

/**
 * @brief Load an ELF64 executable streaming directly from a file descriptor.
 *
 * Reads the ELF and program headers into small stack/heap buffers, then
 * streams each PT_LOAD segment directly into HHDM-mapped physical pages.
 * Kernel memory overhead is O(1) regardless of binary size — no full-file
 * buffer is allocated.
 *
 * @param fd   Open VFS file descriptor.
 * @param info Output structure for entry point and memory range.
 * @return 0 on success, -1 on failure.
 */
int elf_load_fd(i64 fd, elf_info_t *info)
{
  /* Read and validate ELF header. */
  Elf64_Ehdr ehdr;
  vfs_seek(fd, 0, SEEK_SET);
  if(vfs_read(fd, &ehdr, sizeof(Elf64_Ehdr)) != (i64)sizeof(Elf64_Ehdr)) {
    console_print("[ELF] Cannot read ELF header\n");
    return -1;
  }
  if(!elf_validate(&ehdr))
    return -1;
  if(ehdr.e_phnum == 0 || ehdr.e_phoff == 0) {
    console_print("[ELF] No program headers\n");
    return -1;
  }

  /* Read program headers (typically a few KB at most). */
  u64         phdrs_size = (u64)ehdr.e_phnum * sizeof(Elf64_Phdr);
  Elf64_Phdr *phdrs      = (Elf64_Phdr *)kmalloc(phdrs_size);
  if(!phdrs)
    return -1;

  vfs_seek(fd, (i64)ehdr.e_phoff, SEEK_SET);
  if(vfs_read(fd, phdrs, phdrs_size) != (i64)phdrs_size) {
    console_print("[ELF] Cannot read program headers\n");
    kfree(phdrs);
    return -1;
  }

  elf_info_init(&ehdr, info);

  /* Load each PT_LOAD segment. */
  for(u16 i = 0; i < ehdr.e_phnum; i++) {
    const Elf64_Phdr *phdr = &phdrs[i];
    if(phdr->p_type != PT_LOAD || phdr->p_memsz == 0)
      continue;

    u64 vaddr  = phdr->p_vaddr;
    u64 memsz  = phdr->p_memsz;
    u64 filesz = phdr->p_filesz;

    elf_info_track_segment(&ehdr, phdr, info);

    if(elf_map_segment_pages(vaddr, memsz) < 0) {
      kfree(phdrs);
      return -1;
    }

    /* Stream segment data in 64 KB chunks.
     * Reading 64 KB at a time (16 ext2 blocks per call) reduces the number
     * of VFS/ext2/ATA round-trips from ~21 700 to ~850 for an 87 MB binary,
     * yielding a 25× reduction in per-call overhead. */
    if(filesz > 0) {
#define ELF_READ_CHUNK (64UL * 1024)
      u8 *read_buf = (u8 *)kmalloc(ELF_READ_CHUNK);
      if(!read_buf) {
        kfree(phdrs);
        return -1;
      }

      vfs_seek(fd, (i64)phdr->p_offset, SEEK_SET);

      u64 remaining = filesz;
      u64 dst_vaddr = vaddr;

      while(remaining > 0) {
        u64 chunk = remaining < ELF_READ_CHUNK ? remaining : ELF_READ_CHUNK;
        i64 n     = vfs_read(fd, read_buf, chunk);
        if(n <= 0)
          break;

        elf_copy_to_mapped(dst_vaddr, read_buf, (u64)n);
        dst_vaddr += (u64)n;
        remaining -= (u64)n;
      }

      kfree(read_buf);
#undef ELF_READ_CHUNK
    }
  }

  kfree(phdrs);

  if(info->base == ELF_BASE_SENTINEL) {
    console_print("[ELF] No loadable segments (fd)\n");
    return -1;
  }
  return 0;
}
