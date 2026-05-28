/**
 * @file src/kernel/drivers/fb_console_internal.h
 * @brief Shared state, types, and hot-path primitives for the fb_console
 * module split.
 *
 * Not part of any public/UAPI surface — purely the contract between the
 * @c fb_console_*.c source files. Public consumers go through
 * @c <alcor2/drivers/fb_console.h> instead. Splitting the original 1.8 KLOC
 * monolith required a single source of truth for the shared @ref fb_ctx
 * singleton and the hot blitting helpers; this header is that source.
 */

#ifndef ALCOR2_KERNEL_DRIVERS_FB_CONSOLE_INTERNAL_H
#define ALCOR2_KERNEL_DRIVERS_FB_CONSOLE_INTERNAL_H

#include <alcor2/arch/pit.h>
#include <alcor2/fb_console_ioctl.h>
#include <alcor2/types.h>
#include <stdbool.h>
#include <stddef.h>

/* --- Constants ----------------------------------------------------------- */

/** @brief Cursor blink half-period in PIT ticks (~2 Hz). Derived from
 * @c PIT_TICK_HZ so the visible rate is independent of the chosen PIT
 * frequency (the timer can be reprogrammed at runtime). */
#define FB_BLINK_PERIOD_TICKS (PIT_TICK_HZ / 2u)

/** @brief Built-in CP437 glyph width in pixels. Used for the bitmap-font
 * fallback path when no userspace atlas has been registered. */
#define FONT_W 8

/** @brief Built-in CP437 glyph height in pixels (see @ref FONT_W). */
#define FONT_H 16

/** @brief Pixel padding around the cell grid once an atlas takes over.
 * Zero at boot so the bitmap path fills the screen edge to edge; bumped to
 * this once a higher-res atlas is loaded, giving glyphs breathing room. */
#define FB_CONSOLE_MARGIN 20

/** @brief Lines of history retained in the scrollback ring. 500 was sized to
 * comfortably hold the output of @c make verbose without truncation, at the
 * cost of @c SCROLLBACK_ROWS * cols * sizeof(fb_cell_t) of heap. */
#define SCROLLBACK_ROWS 500

/** @brief Bytes held in the keyboard input ring. Sized to absorb a paste-or-
 * typing burst that arrives while userspace is mid-write; a smaller ring would
 * drop bytes during the gap. */
#define INPUT_RING 256

/** @brief Sentinel returned by @ref atlas_lookup when no glyph maps the
 * codepoint. Sized to never collide with a real atlas index (max 16384). */
#define ATLAS_NO_GLYPH 0xFFFFFFFFu

/* The ATLAS_*_MAX caps below bound damage from a buggy or malicious userspace
 * atlas. They are intentionally generous (no real font needs anything near
 * the cap); the goal is "stop a runaway allocation", not enforce font policy.
 * Updating them requires re-checking that the products do not overflow size_t
 * on the call paths in fb_console_set_atlas. */

/** @brief Max cell side in pixels (square cap). Picked because no shipped
 * monospaced font goes past 64 px and bigger means a typo. */
#define ATLAS_CELL_DIM_MAX 64u

/** @brief Max distinct glyph slots in the atlas. Sized for the BMP plane plus
 * bold and italic variants with room to spare. */
#define ATLAS_N_GLYPHS_MAX 16384u

/** @brief Max codepoints in the @c cp_map lookup table. Caps the kmalloc the
 * kernel makes to mirror @c cp_map_user. */
#define ATLAS_N_CP_MAX 0x4000u

/** @brief Max byte size of the atlas pixel buffer (16 MiB). Picked so a
 * 64×64 RGBA atlas with 16384 glyphs (= 256 MiB) is rejected — well over the
 * 1 GiB kernel heap we ship with. */
#define ATLAS_PIXELS_BYTES_MAX (16u * 1024u * 1024u)

/** @brief Max bytes-per-pixel the renderer is willing to handle. 4 covers
 * RGBA; 1 covers FreeType grayscale. Anything else is rejected at submission.
 */
#define ATLAS_MAX_BYTES_PER_PIXEL 4u

/* --- SGR attribute bits --------------------------------------------------- */
/* One byte covers every SGR feature we render (blink/bold/italic/underline/
 * reverse), so attr fits in a u8 inside the cell. Keeping the cell small
 * matters: the grid is one contiguous kmalloc allocation, so bytes saved per
 * cell ripple across the whole heap budget. */

/** @brief SGR 5: blinking text — gated by the global blink phase in tick. */
#define FB_ATTR_BLINK (1u << 0)

/** @brief SGR 1: bold — picks the bold sub-atlas when one is registered. */
#define FB_ATTR_BOLD (1u << 1)

/** @brief SGR 3: italic — picks the italic sub-atlas when one is registered. */
#define FB_ATTR_ITALIC (1u << 2)

/** @brief SGR 4: underline — drawn as a 2 px bar under the glyph row. */
#define FB_ATTR_UNDERLINE (1u << 3)

/** @brief SGR 7: reverse video — swaps fg/bg at blit time, not in the cell. */
#define FB_ATTR_REVERSE (1u << 4)

/* --- Types --------------------------------------------------------------- */

/**
 * @brief One cell of the text grid.
 *
 * Kept compact so the kmalloc'd grid stays small at high rows*cols. The
 * @c dirty bit is in-band so @ref flush_batch can scan the row range without
 * touching a separate bitmap.
 */
typedef struct
{
  u32 cp;    /**< Unicode codepoint at this cell. */
  u32 fg;    /**< RGB foreground. */
  u32 bg;    /**< RGB background. */
  u16 attr;  /**< SGR attribute bits (FB_ATTR_*). */
  u16 dirty; /**< Batch-blit dirty flag: 1 = needs blit at @ref flush_batch. */
} fb_cell_t;

/**
 * @brief Per-driver runtime state for the framebuffer console.
 *
 * Was an anonymous static struct in the monolithic @c fb_console.c; lifted
 * to a named typedef + @c extern singleton when the module was split so the
 * sibling source files could share it without poking into a private symbol.
 * Stays a single instance — there is only one framebuffer.
 */
typedef struct
{
  /* Framebuffer */
  volatile u8 *base;
  u64          width, height, pitch;
  u8           bytes_pp;

  /* Cell grid (kmalloc'd at init). */
  fb_cell_t *cells;
  int        rows, cols; /* in cells */
  int        cx, cy;     /* cursor in cell coords */

  /* Cell pixel dimensions. Defaults to the compiled-in CP437 bitmap size;
   * an atlas submission can replace them with its own cell_w / cell_h. */
  int cell_w, cell_h;

  /* Grid offset in pixels from the framebuffer origin (top-left padding).
   * Zero at boot so the bitmap path fills the screen; bumped to
   * FB_CONSOLE_MARGIN once an atlas takes over. */
  int margin_x, margin_y;

  /* Default colors (used for SGR resets). */
  u32 default_fg, default_bg;
  u32 cur_fg, cur_bg;
  u8  cur_attr;      /* current SGR attribute bits (FB_ATTR_*) */
  u8  cell_blink_on; /* cell blink display phase: 1=visible, 0=hidden */

  /* UTF-8 decoder state. */
  u32 utf8_partial;
  u8  utf8_rem;

  /* ANSI escape-sequence state machine.
   *   0: NORMAL — bytes feed straight through UTF-8 → cell
   *   1: ESC    — saw 0x1b, waiting for the next byte
   *   2: CSI    — inside `ESC [`, accumulating params into esc_buf
   *   3: G0SET  — inside `ESC (`, waiting for the charset designator */
  u8   esc_state;
  u8   esc_len;
  char esc_buf[64];
  u8   g0_acs;  /* 1 once `ESC ( 0` has selected DEC Special Graphics. */
  u32  last_cp; /* last emitted codepoint, replayed by CSI REP (`b`). */

  /* Saved cursor for ESC 7/8 + CSI s/u. */
  int saved_cx, saved_cy;

  /* Cursor blink: counts down one PIT tick at a time; reloaded with
   * FB_BLINK_PERIOD_TICKS so the ~2 Hz rate holds regardless of PIT_TICK_HZ. */
  u16 blink_ticks;
  u8  blink_on;
  u8  cursor_visible;

  /* DECCKM: when set, cursor keys send SS3 (\EOA) instead of CSI (\E[A).
   * ncurses' keypad(TRUE) toggles this via the terminfo smkx string. */
  bool app_cursor_keys;

  /* fb yielded to a userspace mmap-er (e.g. doom). */
  bool yielded;

  /* Batch-blit mode: when true, put_cp_at_cursor/erase_rect mark cell
   * dirty rather than blitting immediately. flush_batch() drains them.
   * batch_r0..batch_r1 track the inclusive dirty row range so flush_batch
   * can skip the full 80×25 scan when only a few rows changed. */
  bool in_batch;
  int  batch_r0, batch_r1;

  /* Input ring (keyboard → reader). */
  u8           in_buf[INPUT_RING];
  unsigned int in_head, in_tail;

  /* Userspace-submitted glyph atlas; bitmap font is the fallback. */
  bool atlas_active;
  u8  *atlas_pixels; /* kernel buffer copy. */
  u32 *atlas_cp_map; /* kernel buffer copy: u32[atlas_n_cp]. */
  u32  atlas_cell_w, atlas_cell_h;
  u32  atlas_stride;
  u32  atlas_bpp;
  u32  atlas_n_glyphs;
  u32  atlas_n_cp;
  u32  atlas_fallback;
  u32  atlas_bold_base;   /* first bold glyph slot; 0 = no bold atlas   */
  u32  atlas_italic_base; /* first italic glyph slot; 0 = no italic atlas */
} fb_console_ctx_t;

/** @brief The framebuffer console singleton. Defined in @c fb_console.c. */
extern fb_console_ctx_t fb_ctx;

/* --- Inline hot-path primitives ------------------------------------------ */

/**
 * @brief 32-bit splat using @c rep @c stosl. Inlined across every TU that
 * touches pixels because the call overhead dominates the work for cell-sized
 * fills (cell_w is usually 8-12 words).
 *
 * @param dst  Destination start; must be 4-byte aligned and writable.
 * @param val  32-bit pattern to splat.
 * @param n    Count of 32-bit words.
 */
static inline void fill32(volatile u32 *dst, u32 val, u32 n)
{
  __asm__ volatile("rep stosl" : "+D"(dst), "+c"(n) : "a"(val) : "memory");
}

/**
 * @brief Alpha-blend one row of an atlas glyph into the framebuffer.
 *
 * @c bypp==1 covers FreeType grayscale (alpha-only) atlases; @c bypp==4
 * covers RGBA atlases. Fast-paths for @c a==0 and @c a==255 skip the multiply
 * for fully-transparent / fully-opaque pixels — that covers the majority of
 * a typical glyph coverage map, so the branches pay for themselves.
 *
 * Inlined (not a normal function) because it runs once per row per dirty cell
 * in @ref flush_batch and @ref blit_cell_data; both are on the hot path.
 *
 * @param dst    Destination pixel row (32-bit BGRA).
 * @param src    Source glyph row from the atlas.
 * @param n      Pixels to blend.
 * @param bypp   Bytes per source pixel (1 = grayscale, 4 = RGBA).
 * @param fg_r   Foreground red channel.
 * @param fg_g   Foreground green channel.
 * @param fg_b   Foreground blue channel.
 * @param bg_r   Background red channel.
 * @param bg_g   Background green channel.
 * @param bg_b   Background blue channel.
 * @param fg_pk  Pre-packed 0xFF000000|fg, used for the @c a==255 fast path.
 * @param bg_pk  Pre-packed 0xFF000000|bg, used for the @c a==0 fast path.
 */
static inline void blend_glyph_row(
    volatile u32 *dst, const u8 *src, u32 n, u32 bypp, u32 fg_r, u32 fg_g,
    u32 fg_b, u32 bg_r, u32 bg_g, u32 bg_b, u32 fg_pk, u32 bg_pk
)
{
  if(!dst || !src)
    return;
  if(bypp == 1u) {
    for(u32 gx = 0; gx < n; gx++) {
      u32 a = src[gx];
      if(!a) {
        dst[gx] = bg_pk;
        continue;
      }
      if(a == 255u) {
        dst[gx] = fg_pk;
        continue;
      }
      u32 inv = 255u - a;
      dst[gx] = 0xFF000000u | ((fg_r * a + bg_r * inv + 128u) >> 8) << 16 |
                ((fg_g * a + bg_g * inv + 128u) >> 8) << 8 |
                (fg_b * a + bg_b * inv + 128u) >> 8;
    }
  } else {
    for(u32 gx = 0; gx < n; gx++) {
      const u8 *px = src + (size_t)gx * bypp;
      u32       a  = (bypp == 4u) ? (u32)px[3] : (u32)px[0];
      if(!a) {
        dst[gx] = bg_pk;
        continue;
      }
      if(a == 255u) {
        dst[gx] = fg_pk;
        continue;
      }
      u32 inv = 255u - a;
      dst[gx] = 0xFF000000u | ((fg_r * a + bg_r * inv + 128u) >> 8) << 16 |
                ((fg_g * a + bg_g * inv + 128u) >> 8) << 8 |
                (fg_b * a + bg_b * inv + 128u) >> 8;
    }
  }
}

/* --- Cross-file function decls ------------------------------------------- */

/* fb_console_pixel.c — raw framebuffer access */

/**
 * @brief Map a hardware @c bpp value (32/24/16) to bytes-per-pixel.
 *
 * Centralised so every code path that walks framebuffer rows agrees on the
 * pixel stride. Falls back to 4 on unknown bpp because that matches Limine's
 * default and is the only depth the atlas blit path supports.
 *
 * @param bpp  Bits per pixel reported by the bootloader/framebuffer request.
 * @return Bytes per pixel (1..4).
 */
u8 bytes_pp_from_bpp(u16 bpp);

/**
 * @brief Write one pixel of @p color at (@p x, @p y) honouring the
 * framebuffer's current @c bytes_pp.
 *
 * Out-of-bounds writes are silently dropped — callers (atlas glyph path,
 * mouse cursor halo) tile from coordinates that can lie slightly past the
 * grid edge, and clipping at this single chokepoint is cheaper than guarding
 * every loop.
 *
 * @param x      Pixel column, in framebuffer coordinates.
 * @param y      Pixel row, in framebuffer coordinates.
 * @param color  0xRRGGBB; alpha is forced to 0xFF for 32 bpp.
 */
void fb_put_pixel(u32 x, u32 y, u32 color);

/* fb_console_atlas.c — glyph cache lookup + meta validation */

/**
 * @brief Resolve @p cp to an atlas glyph slot.
 *
 * @param cp  Unicode codepoint to look up.
 * @return The glyph index, or @ref ATLAS_NO_GLYPH if no atlas is registered
 *         and the codepoint is not covered (caller falls back to bitmap).
 */
u32 atlas_lookup(u32 cp);

/**
 * @brief Like @ref atlas_lookup, but follow the bold/italic sub-atlas offset
 * when the requested attribute is set. Single chokepoint for SGR-aware glyph
 * resolution — keeps blit paths from each re-implementing the offset
 * arithmetic.
 *
 * @param cp    Unicode codepoint.
 * @param attr  Cell SGR bits; only @ref FB_ATTR_BOLD / @ref FB_ATTR_ITALIC
 *              are consulted here.
 * @return Glyph slot for the styled variant, or the plain slot if the variant
 *         is unavailable, or @ref ATLAS_NO_GLYPH.
 */
u32 atlas_lookup_attr(u32 cp, u16 attr);

/**
 * @brief Reject a userspace atlas descriptor whose sizes/bounds would
 * misbehave inside the renderer.
 *
 * Defensive gate against a malformed @c FB_CONSOLE_SET_ATLAS payload — the
 * kernel kmalloc's @c pixels_size and walks @c cp_map_user[0..n_cp-1] based
 * on these numbers, so they must be bounded before any allocation happens.
 *
 * @param meta  Descriptor copied from userspace.
 * @return @c true if all fields fit the @c ATLAS_*_MAX caps.
 */
bool atlas_meta_is_sane(const fb_console_atlas_t *meta);

/* fb_console_cell.c — cell-to-pixel rendering */

/**
 * @brief Render an arbitrary cell value at grid position (col, row).
 *
 * Taking the cell by pointer lets @ref scrollback_repaint pass a synthetic
 * cell that does not live in the live grid (e.g. a row pulled from the
 * scrollback ring) without copying it into @c fb_ctx.cells first.
 *
 * @param c    Cell content to render.
 * @param col  Grid column.
 * @param row  Grid row.
 */
void blit_cell_data(const fb_cell_t *c, int col, int row);

/**
 * @brief Render @c fb_ctx.cells[row * cols + col] — the common case when the
 * caller already owns the grid coordinates and just needs the cell repainted.
 *
 * @param col  Grid column.
 * @param row  Grid row.
 */
void blit_cell(int col, int row);

#endif /* ALCOR2_KERNEL_DRIVERS_FB_CONSOLE_INTERNAL_H */
