/**
 * @file src/kernel/drivers/fb_console/sgr.c
 * @brief Select Graphic Rendition (SGR) handling — @c CSI @c m parsing.
 *
 * SGR is the largest @c CSI subset by parameter count: bold/italic/underline
 * attrs, 8/16/256/truecolour fg+bg, and their resets. Lives in its own file
 * because @ref csi_sgr fans out to four kinds of param consumer (reset, attr,
 * basic colour, extended colour) and each is non-trivial on its own.
 */

#include <alcor2/types.h>
#include <kernel/drivers/fb_console/internal.h>

/**
 * @brief Reset @c cur_fg / @c cur_bg / @c cur_attr to the boot defaults.
 *
 * Used by both the @c CSI @c 0 @c m code and the empty-param shortcut in
 * @ref csi_sgr (xterm treats an empty list as a full reset).
 */
static void sgr_reset(void)
{
  fb_ctx.cur_fg   = fb_ctx.default_fg;
  fb_ctx.cur_bg   = fb_ctx.default_bg;
  fb_ctx.cur_attr = 0;
}

/**
 * @brief Update @c cur_attr from a single-param attribute code (bold,
 *        italic, …, or the corresponding @c 21/22/23/… reset).
 *
 * @param p  SGR code already known to be in the attribute range.
 * @return @c true if @p p matched an attribute code; @c false if the caller
 *         should fall through to colour handling.
 */
static bool sgr_apply_attr(int p)
{
  switch(p) {
  case SGR_BOLD:
    fb_ctx.cur_attr |= (u8)FB_ATTR_BOLD;
    return true;
  case SGR_ITALIC:
    fb_ctx.cur_attr |= (u8)FB_ATTR_ITALIC;
    return true;
  case SGR_UNDERLINE:
    fb_ctx.cur_attr |= (u8)FB_ATTR_UNDERLINE;
    return true;
  case SGR_BLINK:
    fb_ctx.cur_attr |= (u8)FB_ATTR_BLINK;
    return true;
  case SGR_REVERSE:
    fb_ctx.cur_attr |= (u8)FB_ATTR_REVERSE;
    return true;
  case SGR_NO_BOLD:
    fb_ctx.cur_attr &= (u8) ~(u8)FB_ATTR_BOLD;
    return true;
  case SGR_NO_ITALIC:
    fb_ctx.cur_attr &= (u8) ~(u8)FB_ATTR_ITALIC;
    return true;
  case SGR_NO_UNDERLINE:
    fb_ctx.cur_attr &= (u8) ~(u8)FB_ATTR_UNDERLINE;
    return true;
  case SGR_NO_BLINK:
    fb_ctx.cur_attr &= (u8) ~(u8)FB_ATTR_BLINK;
    return true;
  case SGR_NO_REVERSE:
    fb_ctx.cur_attr &= (u8) ~(u8)FB_ATTR_REVERSE;
    return true;
  default:
    return false;
  }
}

/**
 * @brief Update @c cur_fg / @c cur_bg from a single-param colour code
 *        (default, 30..37, 40..47, 90..97, 100..107).
 *
 * @param p  SGR code.
 * @return @c true if @p p matched a basic colour code; @c false otherwise.
 */
static bool sgr_apply_basic_color(int p)
{
  if(p == SGR_FG_DEFAULT) {
    fb_ctx.cur_fg = fb_ctx.default_fg;
    return true;
  }
  if(p == SGR_BG_DEFAULT) {
    fb_ctx.cur_bg = fb_ctx.default_bg;
    return true;
  }
  if(p >= SGR_FG_BASE && p <= SGR_FG_END) {
    fb_ctx.cur_fg = ansi16_fg[p - SGR_FG_BASE];
    return true;
  }
  if(p >= SGR_FG_BRIGHT_BASE && p <= SGR_FG_BRIGHT_END) {
    fb_ctx.cur_fg = ansi16_fg_bright[p - SGR_FG_BRIGHT_BASE];
    return true;
  }
  if(p >= SGR_BG_BASE && p <= SGR_BG_END) {
    fb_ctx.cur_bg = ansi16_bg[p - SGR_BG_BASE];
    return true;
  }
  if(p >= SGR_BG_BRIGHT_BASE && p <= SGR_BG_BRIGHT_END) {
    fb_ctx.cur_bg = ansi16_bg[p - SGR_BG_BRIGHT_BASE];
    return true;
  }
  return false;
}

/**
 * @brief Handle the multi-param extended colour forms @c 38;5;N, @c 38;2;R;G;B,
 *        @c 48;5;N, @c 48;2;R;G;B.
 *
 * Advances the parameter cursor past the consumed subform so the caller can
 * resume its loop without double-counting.
 *
 * @param pv  Parameter vector.
 * @param np  Number of valid entries in @p pv.
 * @param pi  Pointer to the cursor; updated to the last consumed index.
 * @return @c true if an extended form was consumed; @c false if the params
 *         at @p pi don't form a complete subform (caller drops the code).
 */
static bool sgr_apply_extended_color(const int *pv, int np, int *pi)
{
  int p = pv[*pi];
  if(p != SGR_FG_EXTENDED && p != SGR_BG_EXTENDED)
    return false;
  if(*pi + 2 >= np)
    return false;
  int form = pv[*pi + 1];
  u32 colour;
  if(form == SGR_EXT_FORM_256) {
    colour = ansi256_to_rgb((unsigned)pv[*pi + 2]);
    *pi += 2;
  } else if(form == SGR_EXT_FORM_TRUECOLOR && *pi + 4 < np) {
    u32 r  = (u32)pv[*pi + 2] & BYTE_MASK;
    u32 g  = (u32)pv[*pi + 3] & BYTE_MASK;
    u32 b  = (u32)pv[*pi + 4] & BYTE_MASK;
    colour = (r << BGRA_RED_SHIFT) | (g << BGRA_GREEN_SHIFT) | b;
    *pi += 4;
  } else {
    return false;
  }
  if(p == SGR_FG_EXTENDED)
    fb_ctx.cur_fg = colour;
  else
    fb_ctx.cur_bg = colour;
  return true;
}

/** @brief Maximum SGR params consumed in one @c CSI @c m sequence. xterm caps
 * at 16; 32 gives headroom for pathological RGB stacks without growing the
 * kernel stack noticeably. */
#define SGR_MAX_PARAMS 32

void csi_sgr(void)
{
  int pv[SGR_MAX_PARAMS];
  int np = csi_params(pv, SGR_MAX_PARAMS);
  if(np == 0) {
    sgr_reset();
    return;
  }
  for(int pi = 0; pi < np; pi++) {
    int p = pv[pi];
    if(p == SGR_RESET) {
      sgr_reset();
      continue;
    }
    if(sgr_apply_attr(p))
      continue;
    if(sgr_apply_basic_color(p))
      continue;
    (void)sgr_apply_extended_color(pv, np, &pi);
  }
}
