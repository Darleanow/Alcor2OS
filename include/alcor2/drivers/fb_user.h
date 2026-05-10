/**
 * @file include/alcor2/drivers/fb_user.h
 * @brief Export Limine framebuffer to user processes (shared scanout buffer).
 */

#ifndef ALCOR2_FB_USER_H
#define ALCOR2_FB_USER_H

#include <alcor2/alcor_fb.h>
#include <alcor2/limine.h>
#include <alcor2/types.h>

/**
 * @brief Capture framebuffer geometry and physical span from Limine.
 * @param fb Bootloader framebuffer descriptor.
 * @param memmap Limine memory map (for @ref LIMINE_MEMMAP_FRAMEBUFFER).
 * @param hhdm_off HHDM offset; used if the memmap has no framebuffer entry.
 */
void fb_user_boot_init(
    struct limine_framebuffer *fb, struct limine_memmap_response *memmap,
    u64 hhdm_off
);

bool fb_user_ready(void);

/** @brief Fill @p out for @ref SYS_ALCOR_FB_INFO. */
void fb_user_fill_info(alcor_fb_info_t *out);

/**
 * @brief True if @p phys_page is a 4 KiB frame inside the framebuffer region.
 *
 * Used by @c sys_munmap so device RAM is never passed to @c pmm_free.
 */
bool fb_user_phys_page_is_framebuffer(u64 phys_page);

/** @brief Page-aligned physical base of the mappable span. */
u64 fb_user_phys_base(void);

/** @brief Length in bytes of the mappable span (multiple of page size). */
u64 fb_user_map_size(void);

/** @brief Offset in bytes from @ref fb_user_phys_base to the first pixel. */
u64 fb_user_mmap_pixel_offset(void);

#endif
