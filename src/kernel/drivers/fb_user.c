/**
 * @file src/kernel/drivers/fb_user.c
 * @brief Framebuffer physical range + geometry for user mmap.
 */

#include <alcor2/drivers/fb_user.h>
#include <alcor2/kstdlib.h>
#include <alcor2/mm/pmm.h>

#define PAGE_MASK_LOCAL (PAGE_SIZE - 1ULL)

static struct
{
  int valid;
  u64 phys_base;    /**< Page-aligned start. */
  u64 map_size;     /**< Page multiple covering the active framebuffer. */
  u64 pixel_offset; /**< Byte offset from @a phys_base to first pixel. */
  u64 width;
  u64 height;
  u64 pitch;
  u16 bpp;
} g_fb;

static u64 page_align_up_u64(u64 x)
{
  return (x + PAGE_MASK_LOCAL) & ~PAGE_MASK_LOCAL;
}

void fb_user_boot_init(
    struct limine_framebuffer *lfb, struct limine_memmap_response *memmap,
    u64 hhdm_off
)
{
  kzero(&g_fb, sizeof(g_fb));
  if(!lfb || lfb->pitch == 0 || lfb->height == 0)
    return;

  u64 fb_phys = 0;

  if(memmap) {
    for(u64 i = 0; i < memmap->entry_count; i++) {
      struct limine_memmap_entry *e = memmap->entries[i];
      if(e->type == LIMINE_MEMMAP_FRAMEBUFFER) {
        fb_phys = e->base;
        break;
      }
    }
  }

  if(fb_phys == 0 && lfb->address) {
    u64 va = (u64)lfb->address;
    if(va >= hhdm_off)
      fb_phys = va - hhdm_off;
    else
      fb_phys = va;
  }

  if(fb_phys == 0)
    return;

  u64 active = lfb->pitch * lfb->height;
  if(active == 0)
    return;

  u64 map_lo = fb_phys & ~(u64)PAGE_MASK_LOCAL;
  u64 rel0   = fb_phys - map_lo;
  u64 map_sz = page_align_up_u64(rel0 + active);

  g_fb.phys_base    = map_lo;
  g_fb.map_size     = map_sz;
  g_fb.pixel_offset = rel0;
  g_fb.width        = lfb->width;
  g_fb.height       = lfb->height;
  g_fb.pitch        = lfb->pitch;
  g_fb.bpp          = lfb->bpp;
  g_fb.valid        = 1;
}

bool fb_user_ready(void)
{
  return g_fb.valid != 0;
}

void fb_user_fill_info(alcor_fb_info_t *out)
{
  if(!out)
    return;
  kzero(out, sizeof(*out));
  if(!g_fb.valid)
    return;
  if(g_fb.width > 0xffffffffu || g_fb.height > 0xffffffffu ||
     g_fb.pitch > 0xffffffffu)
    return;
  out->width    = (u32)g_fb.width;
  out->height   = (u32)g_fb.height;
  out->pitch    = (u32)g_fb.pitch;
  out->bpp      = g_fb.bpp;
  out->byte_len = g_fb.pitch * g_fb.height;
  out->map_size = g_fb.map_size;
}

bool fb_user_phys_page_is_framebuffer(u64 phys_page)
{
  if(!g_fb.valid)
    return false;
  u64 hi = g_fb.phys_base + g_fb.map_size;
  return phys_page >= g_fb.phys_base && phys_page < hi;
}

u64 fb_user_phys_base(void)
{
  return g_fb.valid ? g_fb.phys_base : 0;
}

u64 fb_user_map_size(void)
{
  return g_fb.valid ? g_fb.map_size : 0;
}

u64 fb_user_mmap_pixel_offset(void)
{
  return g_fb.valid ? g_fb.pixel_offset : 0;
}
