/**
 * @file src/kernel/drivers/fb_console/atlas.c
 * @brief Glyph atlas lookup + meta validation.
 *
 * Hot side: codepoint → glyph index resolution called per dirty cell during
 * @ref blit_cell_data and @ref flush_batch. Cold side: @ref atlas_meta_is_sane
 * gates a userspace-submitted descriptor before the rest of the kernel trusts
 * its sizes and indices.
 */

#include <alcor2/drivers/fb_console.h>
#include <alcor2/types.h>
#include <kernel/drivers/fb_console/internal.h>

u32 atlas_lookup(u32 cp)
{
  if(!fb_ctx.atlas_active)
    return ATLAS_NO_GLYPH;
  if(cp < fb_ctx.atlas_n_cp) {
    u32 idx = fb_ctx.atlas_cp_map[cp];
    if(idx < fb_ctx.atlas_n_glyphs)
      return idx;
  }
  return fb_ctx.atlas_fallback;
}

u32 atlas_lookup_attr(u32 cp, u16 attr)
{
  u32 idx = atlas_lookup(cp);
  if(idx == ATLAS_NO_GLYPH)
    return ATLAS_NO_GLYPH;
  if((attr & FB_ATTR_BOLD) && fb_ctx.atlas_bold_base > 0u) {
    u32 bi = idx + fb_ctx.atlas_bold_base;
    if(bi < fb_ctx.atlas_n_glyphs)
      return bi;
  } else if((attr & FB_ATTR_ITALIC) && fb_ctx.atlas_italic_base > 0u) {
    u32 ii = idx + fb_ctx.atlas_italic_base;
    if(ii < fb_ctx.atlas_n_glyphs)
      return ii;
  }
  return idx;
}

bool atlas_meta_is_sane(const fb_console_atlas_t *meta)
{
  /* Caps are generous — they bound damage from a buggy shim, not enforce
   * policy. */
  if(meta->cell_w == 0u || meta->cell_h == 0u ||
     meta->cell_w > ATLAS_CELL_DIM_MAX || meta->cell_h > ATLAS_CELL_DIM_MAX)
    return false;
  if(meta->n_glyphs == 0u || meta->n_glyphs > ATLAS_N_GLYPHS_MAX)
    return false;
  if(meta->n_cp == 0u || meta->n_cp > ATLAS_N_CP_MAX)
    return false;
  if(meta->pixels_size == 0u || meta->pixels_size > ATLAS_PIXELS_BYTES_MAX)
    return false;
  /* Stride: one row of pixels must fit inside cell_w * max-bytes-per-pixel.
   * Cap bypp at 4 (the widest fb format we render); 1-byte alpha is typical. */
  if(meta->stride_bytes == 0u ||
     meta->stride_bytes > ATLAS_MAX_BYTES_PER_PIXEL * meta->cell_w)
    return false;
  return true;
}
