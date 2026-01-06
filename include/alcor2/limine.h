/**
 * @file include/alcor2/limine.h
 * @brief Limine bootloader protocol definitions.
 *
 * Structures and macros for interacting with the Limine bootloader.
 * Provides framebuffer, memory map, HHDM offset, and module loading.
 */

#ifndef ALCOR2_LIMINE_H
#define ALCOR2_LIMINE_H

#include <alcor2/types.h>

/** @name Limine magic values
 * @{ */
#define LIMINE_MAGIC_0 0xc7b1dd30df4c8b88
#define LIMINE_MAGIC_1 0x0a82e883a194f07b
/** @} */

/** @name Request IDs
 * @{ */
#define LIMINE_FRAMEBUFFER_REQUEST_ID                                      \
  {LIMINE_MAGIC_0, LIMINE_MAGIC_1, 0x9d5827dcd881dd75, 0xa3148604f6fab11b}

#define LIMINE_MEMMAP_REQUEST_ID                                           \
  {LIMINE_MAGIC_0, LIMINE_MAGIC_1, 0x67cf3d9d378a806f, 0xe304acdfc50c3c62}

#define LIMINE_HHDM_REQUEST_ID                                             \
  {LIMINE_MAGIC_0, LIMINE_MAGIC_1, 0x48dcf1cb8ad2b852, 0x63984e959a98244b}

#define LIMINE_MODULE_REQUEST_ID                                           \
  {LIMINE_MAGIC_0, LIMINE_MAGIC_1, 0x3e7e279702be32af, 0xca1c4f3bd1280cee}
/** @} */

/** @name Memory map entry types
 * @{ */
#define LIMINE_MEMMAP_USABLE                 0
#define LIMINE_MEMMAP_RESERVED               1
#define LIMINE_MEMMAP_ACPI_RECLAIMABLE       2
#define LIMINE_MEMMAP_ACPI_NVS               3
#define LIMINE_MEMMAP_BAD_MEMORY             4
#define LIMINE_MEMMAP_BOOTLOADER_RECLAIMABLE 5
#define LIMINE_MEMMAP_KERNEL_AND_MODULES     6
#define LIMINE_MEMMAP_FRAMEBUFFER            7
/** @} */

/**
 * @brief Declare base revision for Limine protocol.
 * @param n Revision number.
 */
#define LIMINE_BASE_REVISION(n)                                      \
  USED                  SECTION(                                     \
      ".limine_requests"                            \
  ) static volatile u64 limine_base_revision[3] = { \
      0xf9562b2d5c95a6c8, 0x6a7b384944536bdc, (n)   \
  };

/** @brief Check if base revision was accepted. */
#define LIMINE_BASE_REVISION_OK (limine_base_revision[2] == 0)

/** @brief Start of request section. */
#define LIMINE_REQUESTS_START                     \
  USED                    SECTION(                \
      ".limine_requests_start" \
  ) static volatile void *limine_req_start = (void *)0xf9562b2d5c95a6c8;

/** @brief End of request section. */
#define LIMINE_REQUESTS_END                                                   \
  USED SECTION(".limine_requests_end") static volatile void *limine_req_end = \
      (void *)0xadc0e0531bb10d03;

/**
 * @brief Video mode descriptor.
 */
struct limine_video_mode
{
  u64 pitch;
  u64 width;
  u64 height;
  u16 bpp;
  u8  memory_model;
  u8  red_mask_size;
  u8  red_mask_shift;
  u8  green_mask_size;
  u8  green_mask_shift;
  u8  blue_mask_size;
  u8  blue_mask_shift;
};

/**
 * @brief Framebuffer descriptor.
 */
struct limine_framebuffer
{
  void                      *address;
  u64                        width;
  u64                        height;
  u64                        pitch;
  u16                        bpp;
  u8                         memory_model;
  u8                         red_mask_size;
  u8                         red_mask_shift;
  u8                         green_mask_size;
  u8                         green_mask_shift;
  u8                         blue_mask_size;
  u8                         blue_mask_shift;
  u8                         unused[7];
  u64                        edid_size;
  void                      *edid;
  u64                        mode_count;
  struct limine_video_mode **modes;
};

/**
 * @brief Framebuffer response from bootloader.
 */
struct limine_framebuffer_response
{
  u64                         revision;
  u64                         framebuffer_count;
  struct limine_framebuffer **framebuffers;
};

/**
 * @brief Framebuffer request structure.
 */
struct limine_framebuffer_request
{
  u64                                 id[4];
  u64                                 revision;
  struct limine_framebuffer_response *response;
};

/**
 * @brief Memory map entry.
 */
struct limine_memmap_entry
{
  u64 base;
  u64 length;
  u64 type;
};

/**
 * @brief Memory map response from bootloader.
 */
struct limine_memmap_response
{
  u64                          revision;
  u64                          entry_count;
  struct limine_memmap_entry **entries;
};

/**
 * @brief Memory map request structure.
 */
struct limine_memmap_request
{
  u64                            id[4];
  u64                            revision;
  struct limine_memmap_response *response;
};

/**
 * @brief Higher-half direct map response.
 */
struct limine_hhdm_response
{
  u64 revision;
  u64 offset;
};

/**
 * @brief Higher-half direct map request.
 */
struct limine_hhdm_request
{
  u64                          id[4];
  u64                          revision;
  struct limine_hhdm_response *response;
};

/**
 * @brief Limine file descriptor (module or kernel file).
 */
struct limine_file
{
  u64   revision;
  void *address;
  u64   size;
  char *path;
  char *cmdline;
  u32   media_type;
  u32   unused;
  u64   tftp_ip;
  u32   tftp_port;
  u32   partition_index;
  u32   mbr_disk_id;
  u8    gpt_disk_uuid[16];
  u8    gpt_part_uuid[16];
  u8    part_uuid[16];
};

/**
 * @brief Module response from bootloader.
 */
struct limine_module_response
{
  u64                  revision;
  u64                  module_count;
  struct limine_file **modules;
};

/**
 * @brief Module request structure.
 */
struct limine_module_request
{
  u64                            id[4];
  u64                            revision;
  struct limine_module_response *response;
  u64                            internal_module_count;
  struct limine_file           **internal_modules;
};

#endif
