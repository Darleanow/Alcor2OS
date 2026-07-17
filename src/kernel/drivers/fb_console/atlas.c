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

/**
 * @brief Resolve a Unicode codepoint to its slot in the userspace atlas.
 *
 * Goes through @c atlas_fallback when the codepoint is out of range or maps
 * to no glyph, so renderers can always assume "something will be drawn" and
 * never have to special-case a missing glyph in the hot path. The @c idx
 * sanity check (@c < @c atlas_n_glyphs) guards against a corrupt cp_map
 * leaking through the @ref atlas_meta_is_sane gate.
 *
 * @param cp  Unicode codepoint.
 * @return Atlas glyph slot, or @ref ATLAS_NO_GLYPH when no atlas is active.
 */
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

/**
 * @brief Resolve to the bold or italic sub-atlas slot when SGR asks for one.
 *
 * Centralises the offset-add arithmetic so each blit path doesn't reinvent it
 * (or forget the bounds check). Bold wins over italic when both bits are set:
 * the bold variants are typically more readable, and the parser already
 * accepts that combination silently rather than erroring out.
 *
 * @param cp    Unicode codepoint.
 * @param attr  Cell SGR bits; only @ref FB_ATTR_BOLD and @ref FB_ATTR_ITALIC
 *              are consulted.
 * @return Styled slot when available, plain slot when not, @ref ATLAS_NO_GLYPH
 *         on miss.
 */
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

/**
 * @brief Reject a userspace atlas descriptor whose fields would misbehave
 * inside the renderer.
 *
 * The @c kmalloc and per-codepoint walks in @ref fb_console_set_atlas trust
 * every field of @p meta verbatim once this returns @c true, so this is the
 * one and only place where an attacker-controlled value gets bounded. Caps
 * are generous (no real font needs anything near them), the goal is to
 * stop a runaway allocation, not enforce font policy.
 *
 * @param meta  Descriptor copied from userspace.
 * @return @c true if every field fits the @c ATLAS_*_MAX caps and stride is
 *         within @c cell_w * max-bytes-per-pixel.
 */
bool atlas_meta_is_sane(const alcor_console_atlas_t *meta)
{
  if(meta->cell_w == 0u || meta->cell_h == 0u ||
     meta->cell_w > ATLAS_CELL_DIM_MAX || meta->cell_h > ATLAS_CELL_DIM_MAX)
    return false;
  if(meta->n_glyphs == 0u || meta->n_glyphs > ATLAS_N_GLYPHS_MAX)
    return false;
  if(meta->n_cp == 0u || meta->n_cp > ATLAS_N_CP_MAX)
    return false;
  if(meta->pixels_size == 0u || meta->pixels_size > ATLAS_PIXELS_BYTES_MAX)
    return false;
  if(meta->stride_bytes == 0u ||
     meta->stride_bytes > ATLAS_MAX_BYTES_PER_PIXEL * meta->cell_w)
    return false;
  return true;
}
