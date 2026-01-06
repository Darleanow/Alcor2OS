/**
 * @file include/alcor2/elf.h
 * @brief ELF64 format definitions and loader.
 *
 * Structures and constants for parsing/loading ELF64 executables.
 * Reference: System V ABI AMD64 Architecture Processor Supplement.
 */

#ifndef ALCOR2_ELF_H
#define ALCOR2_ELF_H

#include <alcor2/types.h>

/** @brief ELF magic number (\x7FELF in little-endian). */
#define ELF_MAGIC 0x464C457F

/** @name e_ident array indices
 * @{ */
#define EI_MAG0       0
#define EI_MAG1       1
#define EI_MAG2       2
#define EI_MAG3       3
#define EI_CLASS      4
#define EI_DATA       5
#define EI_VERSION    6
#define EI_OSABI      7
#define EI_ABIVERSION 8
#define EI_PAD        9
#define EI_NIDENT     16
/** @} */

/** @name EI_CLASS values
 * @{ */
#define ELFCLASS32 1
#define ELFCLASS64 2
/** @} */

/** @name EI_DATA values
 * @{ */
#define ELFDATA2LSB 1
#define ELFDATA2MSB 2
/** @} */

/** @name e_type values
 * @{ */
#define ET_NONE 0
#define ET_REL  1
#define ET_EXEC 2
#define ET_DYN  3
#define ET_CORE 4
/** @} */

/** @name e_machine values
 * @{ */
#define EM_X86_64 62
/** @} */

/** @name Program header types
 * @{ */
#define PT_NULL    0
#define PT_LOAD    1
#define PT_DYNAMIC 2
#define PT_INTERP  3
#define PT_NOTE    4
#define PT_SHLIB   5
#define PT_PHDR    6
#define PT_TLS     7
/** @} */

/** @name Program header flags
 * @{ */
#define PF_X 0x1
#define PF_W 0x2
#define PF_R 0x4
/** @} */

/**
 * @brief ELF64 file header.
 */
typedef struct
{
  u8  e_ident[EI_NIDENT]; /**< ELF identification. */
  u16 e_type;             /**< Object file type. */
  u16 e_machine;          /**< Machine type. */
  u32 e_version;          /**< Object file version. */
  u64 e_entry;            /**< Entry point address. */
  u64 e_phoff;            /**< Program header offset. */
  u64 e_shoff;            /**< Section header offset. */
  u32 e_flags;            /**< Processor-specific flags. */
  u16 e_ehsize;           /**< ELF header size. */
  u16 e_phentsize;        /**< Program header entry size. */
  u16 e_phnum;            /**< Number of program headers. */
  u16 e_shentsize;        /**< Section header entry size. */
  u16 e_shnum;            /**< Number of section headers. */
  u16 e_shstrndx;         /**< Section name string table index. */
} __attribute__((packed)) Elf64_Ehdr;

/**
 * @brief ELF64 program header.
 */
typedef struct
{
  u32 p_type;   /**< Segment type. */
  u32 p_flags;  /**< Segment flags. */
  u64 p_offset; /**< Offset in file. */
  u64 p_vaddr;  /**< Virtual address in memory. */
  u64 p_paddr;  /**< Physical address (unused on x86_64). */
  u64 p_filesz; /**< Size in file. */
  u64 p_memsz;  /**< Size in memory. */
  u64 p_align;  /**< Alignment. */
} __attribute__((packed)) Elf64_Phdr;

/**
 * @brief ELF64 section header.
 */
typedef struct
{
  u32 sh_name;      /**< Section name (string table index). */
  u32 sh_type;      /**< Section type. */
  u64 sh_flags;     /**< Section flags. */
  u64 sh_addr;      /**< Virtual address. */
  u64 sh_offset;    /**< Offset in file. */
  u64 sh_size;      /**< Section size. */
  u32 sh_link;      /**< Link to another section. */
  u32 sh_info;      /**< Additional info. */
  u64 sh_addralign; /**< Alignment. */
  u64 sh_entsize;   /**< Entry size if table. */
} __attribute__((packed)) Elf64_Shdr;

/**
 * @brief Loaded ELF information.
 */
typedef struct
{
  u64 entry; /**< Entry point address. */
  u64 base;  /**< Load base address. */
  u64 end;   /**< End of loaded segments. */
} elf_info_t;

/**
 * @brief Load an ELF64 executable into user memory.
 * @param data Pointer to ELF file data.
 * @param size Size of ELF file.
 * @param info Output: loaded ELF information.
 * @return 0 on success, negative on error.
 */
int elf_load(const void *data, u64 size, elf_info_t *info);

/**
 * @brief Validate an ELF64 header.
 * @param ehdr Pointer to ELF header.
 * @return true if valid ELF64 executable.
 */
bool elf_validate(const Elf64_Ehdr *ehdr);

#endif
