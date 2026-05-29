/**
 * @file src/kernel/drivers/fb_console/internal.h
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
#include <alcor2/types.h>
#include <stdbool.h>
#include <stddef.h>
#include <uapi/alcor2/fb_console_ioctl.h>

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

/* The SGR_* values below are the wire-format numbers a terminal emits after
 * @c CSI; mirrored verbatim from ECMA-48 / xterm so an unmodified TUI program
 * sees the same behaviour as it would on a real terminal. Defined here so
 * the parser in @ref ansi.c references named symbols instead of bare ints. */

/** @brief @c CSI @c 0 @c m — reset all SGR state to defaults. */
#define SGR_RESET 0

/** @brief @c CSI @c 1 @c m — set bold. */
#define SGR_BOLD 1

/** @brief @c CSI @c 3 @c m — set italic. */
#define SGR_ITALIC 3

/** @brief @c CSI @c 4 @c m — set underline. */
#define SGR_UNDERLINE 4

/** @brief @c CSI @c 5 @c m — set blink. */
#define SGR_BLINK 5

/** @brief @c CSI @c 7 @c m — set reverse video. */
#define SGR_REVERSE 7

/** @brief @c CSI @c 22 @c m — clear bold. */
#define SGR_NO_BOLD 22

/** @brief @c CSI @c 23 @c m — clear italic. */
#define SGR_NO_ITALIC 23

/** @brief @c CSI @c 24 @c m — clear underline. */
#define SGR_NO_UNDERLINE 24

/** @brief @c CSI @c 25 @c m — clear blink. */
#define SGR_NO_BLINK 25

/** @brief @c CSI @c 27 @c m — clear reverse. */
#define SGR_NO_REVERSE 27

/** @brief First foreground colour code; @c CSI @c 30..37 @c m index
 * @ref ansi16_fg through @c (p - SGR_FG_BASE). */
#define SGR_FG_BASE 30

/** @brief Last foreground colour code in the 30..37 range. */
#define SGR_FG_END 37

/** @brief @c CSI @c 38 @c m — extended foreground (256-colour or truecolour
 * sub-form follows). */
#define SGR_FG_EXTENDED 38

/** @brief @c CSI @c 39 @c m — reset foreground to the default colour. */
#define SGR_FG_DEFAULT 39

/** @brief First background colour code; @c CSI @c 40..47 @c m index
 * @ref ansi16_bg through @c (p - SGR_BG_BASE). */
#define SGR_BG_BASE 40

/** @brief Last background colour code in the 40..47 range. */
#define SGR_BG_END 47

/** @brief @c CSI @c 48 @c m — extended background (256-colour or truecolour
 * sub-form follows). */
#define SGR_BG_EXTENDED 48

/** @brief @c CSI @c 49 @c m — reset background to the default colour. */
#define SGR_BG_DEFAULT 49

/** @brief First bright foreground colour code; @c CSI @c 90..97 @c m. */
#define SGR_FG_BRIGHT_BASE 90

/** @brief Last bright foreground colour code in the 90..97 range. */
#define SGR_FG_BRIGHT_END 97

/** @brief First bright background colour code; @c CSI @c 100..107 @c m. */
#define SGR_BG_BRIGHT_BASE 100

/** @brief Last bright background colour code in the 100..107 range. */
#define SGR_BG_BRIGHT_END 107

/** @brief Sub-form selector after SGR 38/48 selecting truecolour:
 * @c CSI @c 38;2;R;G;B @c m. */
#define SGR_EXT_FORM_TRUECOLOR 2

/** @brief Sub-form selector after SGR 38/48 selecting 256-colour palette:
 * @c CSI @c 38;5;N @c m. */
#define SGR_EXT_FORM_256 5

/** @brief DEC private mode 1 (DECCKM) — when set, cursor keys send SS3
 * sequences (@c \\EOA) instead of CSI (@c \\E[A). ncurses' keypad() toggles
 * this via the terminfo smkx string. */
#define DEC_PM_APP_CURSOR_KEYS 1

/** @brief DEC private mode 25 — cursor visibility. */
#define DEC_PM_CURSOR_VISIBLE 25

/** @brief Alpha byte set to opaque in a 0xAARRGGBB packed pixel. The kernel
 * only emits opaque writes — this masks every result so a downstream
 * compositor that respects alpha doesn't see ghosts. */
#define BGRA_OPAQUE_ALPHA 0xFF000000u

/** @brief 8-bit channel mask — extract one BGRA component. Named because
 * @c & 255 reads as a magic number in dense colour code. */
#define BYTE_MASK 0xFFu

/** @brief Bit offset of the red channel in a 0xRRGGBB or 0xAARRGGBB pixel.
 * Named so every @c >> @c 16 in the colour-unpack paths reads as "extract red"
 * instead of a bare shift. */
#define BGRA_RED_SHIFT 16u

/** @brief Bit offset of the green channel in a packed RGB(A) pixel. Same role
 * as @ref BGRA_RED_SHIFT for the middle byte. */
#define BGRA_GREEN_SHIFT 8u

/** @brief Byte index of the alpha channel inside an atlas RGBA pixel.
 * Used by the per-pixel blend path so the @c [3] subscript reads as "alpha
 * from RGBA" rather than a magic offset. */
#define ATLAS_RGBA_ALPHA_BYTE 3u

/** @brief Byte index used as alpha when the atlas pixel format is single-byte
 * grayscale (FreeType coverage map). The byte itself is the coverage. */
#define ATLAS_GRAY_ALPHA_BYTE 0u

/** @brief Tab stops every 8 cells. ANSI/xterm default; matches what userspace
 * terminfo expects, so editors and shells align as designed. */
#define TAB_WIDTH 8

/** @brief Bit mask whose AND-NOT with the cursor column snaps to the previous
 * tab stop. Derived from @ref TAB_WIDTH so the two stay in sync. */
#define TAB_SNAP_MASK (TAB_WIDTH - 1)

/* The FB_BPP_* values are the bits-per-pixel modes Limine can report;
 * FB_BYTES_PER_PIXEL_* is the corresponding stride coefficient. Centralised
 * here so the fast-path predicate (@c bytes_pp == @c FB_BYTES_PER_PIXEL_32)
 * reads as a named check across every blit module instead of a bare 4. */

/** @brief 32-bit pixel: 0xAARRGGBB; the renderer's fast path. */
#define FB_BPP_32 32

/** @brief 24-bit pixel: 0xRRGGBB packed in 3 bytes. */
#define FB_BPP_24 24

/** @brief 16-bit pixel: RGB565 packed. */
#define FB_BPP_16 16

/** @brief Bytes per pixel at @ref FB_BPP_32. */
#define FB_BYTES_PER_PIXEL_32 4u

/** @brief Bytes per pixel at @ref FB_BPP_24. */
#define FB_BYTES_PER_PIXEL_24 3u

/** @brief Bytes per pixel at @ref FB_BPP_16. */
#define FB_BYTES_PER_PIXEL_16 2u

/** @brief Bits in a byte. Named so atlas-bpp → bytes-up math (@c (bpp + @c
 * BITS_PER_BYTE @c - @c 1) @c / @c BITS_PER_BYTE) doesn't read as "magic 8". */
#define BITS_PER_BYTE 8u

/** @brief Pixel thickness of the SGR underline bar drawn below the glyph row.
 * Picked to be readable across font sizes from 8 px CP437 to 20 px Fira. */
#define UNDERLINE_THICKNESS_PX 2u

/** @brief Fully-opaque alpha value (max byte). Marker for "skip the multiply,
 * just use the foreground" in the alpha-blend fast path. */
#define ALPHA_OPAQUE 255u

/** @brief Rounding bias for fixed-point alpha-blend division: adding 128
 * before the shift-by-8 converts truncation to round-to-nearest, avoiding
 * one-quantum darkening of mid-alpha pixels. */
#define ALPHA_ROUND_BIAS 128u

/* The default fg/bg the console boots into. Picked from Catppuccin Mocha so
 * boot output matches the userspace theme and there's no jarring re-paint
 * once a userspace atlas takes over. */

/** @brief Catppuccin Mocha "Text" — default foreground colour. */
#define CATPPUCCIN_MOCHA_TEXT 0xcdd6f4u

/** @brief Catppuccin Mocha "Base" — default background colour. */
#define CATPPUCCIN_MOCHA_BASE 0x1e1e2eu

/** @brief Number of margin sides (left+right or top+bottom) — derived from
 * how @ref FB_CONSOLE_MARGIN is applied symmetrically on both edges. Named
 * so the reflow arithmetic in @ref fb_console_set_atlas doesn't have a bare
 * 2 doing layout work. */
#define MARGIN_SIDES_COUNT 2u

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
      if(a == ALPHA_OPAQUE) {
        dst[gx] = fg_pk;
        continue;
      }
      u32 inv = ALPHA_OPAQUE - a;
      dst[gx] = BGRA_OPAQUE_ALPHA |
                (((fg_r * a + bg_r * inv + ALPHA_ROUND_BIAS) >> BITS_PER_BYTE)
                 << BGRA_RED_SHIFT) |
                (((fg_g * a + bg_g * inv + ALPHA_ROUND_BIAS) >> BITS_PER_BYTE)
                 << BGRA_GREEN_SHIFT) |
                ((fg_b * a + bg_b * inv + ALPHA_ROUND_BIAS) >> BITS_PER_BYTE);
    }
  } else {
    for(u32 gx = 0; gx < n; gx++) {
      const u8 *px = src + (size_t)gx * bypp;
      u32 a = (bypp == FB_BYTES_PER_PIXEL_32) ? (u32)px[ATLAS_RGBA_ALPHA_BYTE]
                                              : (u32)px[ATLAS_GRAY_ALPHA_BYTE];
      if(!a) {
        dst[gx] = bg_pk;
        continue;
      }
      if(a == ALPHA_OPAQUE) {
        dst[gx] = fg_pk;
        continue;
      }
      u32 inv = ALPHA_OPAQUE - a;
      dst[gx] = BGRA_OPAQUE_ALPHA |
                (((fg_r * a + bg_r * inv + ALPHA_ROUND_BIAS) >> BITS_PER_BYTE)
                 << BGRA_RED_SHIFT) |
                (((fg_g * a + bg_g * inv + ALPHA_ROUND_BIAS) >> BITS_PER_BYTE)
                 << BGRA_GREEN_SHIFT) |
                ((fg_b * a + bg_b * inv + ALPHA_ROUND_BIAS) >> BITS_PER_BYTE);
    }
  }
}

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

/**
 * @brief Re-blit the cell currently showing the inverted caret block,
 * restoring the glyph beneath it. Idempotent: no-op when nothing is painted.
 */
void caret_erase(void);

/**
 * @brief Paint the caret at the current @c fb_ctx.cx/cy by drawing the cell
 * with fg/bg swapped. Records the position so a later @ref caret_erase can
 * undo the inversion.
 */
void caret_paint(void);

/**
 * @brief Convenience: @ref caret_erase followed by @ref caret_paint. Used
 * after a write completes; cheap when the cursor hasn't moved (erase is
 * idempotent on the same cell).
 */
void caret_refresh(void);

/**
 * @brief Drop the @c last-drawn tracker without repainting.
 *
 * Used by paths that overwrite the cell themselves (full repaint, scroll,
 * fb yield/reclaim) — they need the caret machinery to forget the previous
 * position so the next @ref caret_paint records a fresh one.
 */
void caret_clear_drawn(void);

/**
 * @brief Mark the cell currently under the caret dirty for the next
 * @ref flush_batch and forget the tracker. Used by @ref fb_console_write_begin
 * so the batched repaint also wipes the inverted block — without this the
 * post-batch flush would leave the old caret visible until the next refresh.
 */
void caret_invalidate_in_batch(void);

/**
 * @brief Move the top row off the grid into the scrollback ring and shift
 * every remaining row up. Increments the pending-scroll counter so the next
 * flush copies pixels in a single move.
 */
void scroll_one(void);

/**
 * @brief Repaint the visible grid from the scrollback ring blended with the
 * live cells, based on the current scroll-up offset. Called when entering
 * scrollback mode or scrolling within it.
 */
void scrollback_repaint(void);

/**
 * @brief Drop back to the live view (scroll offset = 0) and mark every cell
 * dirty so the next @ref flush_batch repaints what the scrollback overlay
 * had been hiding.
 */
void scrollback_exit(void);

/**
 * @brief Drain the pending-scroll counter: copies pixels up in one MMIO blit
 * and marks the freshly exposed rows dirty for the next @ref flush_batch.
 * No-op when no scrolls are pending.
 */
void flush_pending_scroll(void);

/**
 * @brief (Re)allocate the scrollback ring for @p cols columns; existing
 * contents are dropped (cell coordinates do not survive a column reflow).
 *
 * @param cols  New column count.
 */
void scrollback_alloc_for(int cols);

/**
 * @brief Forget pending scrolls without flushing them. Used by paths that
 * are about to repaint the entire grid anyway (fb_console_reclaim,
 * fb_console_set_atlas) — flushing first would just waste a VRAM copy.
 */
void scrollback_drop_pending(void);

/**
 * @brief Erase the software mouse cursor if it is painted, so the scrollback
 * pixel-move does not drag a stale copy along. Called by
 * @ref flush_pending_scroll just before the kmemcpy.
 */
void mouse_cursor_invalidate_for_scroll(void);

/**
 * @brief Paint or move the software mouse cursor. Early-outs when the pointer
 * has not moved since the last call, so this is free at rest and as smooth as
 * the tick rate in motion. Called from the PIT tick path.
 */
void mouse_cursor_render(void);

/**
 * @brief Forget the painted-at position without erasing pixels. Used by paths
 * that are about to repaint the whole framebuffer themselves (yield, reclaim,
 * blink-driven re-blit) — the cursor's pixels will be overwritten anyway, so
 * the next @ref mouse_cursor_render must redraw rather than skip-as-unchanged.
 */
void mouse_cursor_drop(void);

/**
 * @brief Grow the per-row scratch used by @ref flush_batch to at least
 * @p cols entries. Called on grid init and whenever a SET_ATLAS reflow
 * widens the grid.
 *
 * @param cols  Minimum capacity in cells.
 */
void flush_ci_ensure(int cols);

/**
 * @brief Repaint every dirty cell in the [@c batch_r0, @c batch_r1] row range
 * in one VRAM pass, then clear their @c dirty bits.
 *
 * The fast 32-bpp path resolves atlas glyphs once per cell into the scratch,
 * then walks scanline-by-scanline; the slow path falls back to @ref blit_cell.
 */
void flush_batch(void);

/**
 * @brief Write one Unicode codepoint at the cell cursor and advance.
 *
 * Wraps at the right margin (scrolls if needed) and either marks the cell
 * dirty (batch mode) or blits it immediately. Skips identical-content writes
 * so editors that repaint unchanged lines don't pound VRAM.
 *
 * @param cp  Unicode codepoint to write.
 */
void put_cp_at_cursor(u32 cp);

/**
 * @brief Catppuccin Mocha 16-colour foreground palette. Indices match
 * @c SGR @c 30..37. Defined in @c palette.c; shared with @c sgr.c so the
 * SGR handler can resolve colour codes without re-declaring the table.
 */
extern const u32 ansi16_fg[8];

/** @brief Bright foreground palette (@c SGR @c 90..97). See @ref ansi16_fg. */
extern const u32 ansi16_fg_bright[8];

/** @brief Background palette (@c SGR @c 40..47). See @ref ansi16_fg. */
extern const u32 ansi16_bg[8];

/**
 * @brief Resolve an xterm 256-colour palette index to a 0xRRGGBB triplet.
 *
 * Used by the SGR extended-colour subform (@c CSI @c 38;5;N or @c 48;5;N).
 * Defined in @c palette.c.
 *
 * @param idx  Palette index 0..255.
 * @return Packed RGB.
 */
u32 ansi256_to_rgb(unsigned idx);

/**
 * @brief Parse up to @p maxn decimal parameters from @c esc_buf into @p pv.
 *
 * Defined in @c csi.c; @c sgr.c also consumes it for the @c CSI @c m param
 * list. Empty fields default to 0 so the parser stays xterm-compatible.
 *
 * @param pv    Destination buffer.
 * @param maxn  Capacity of @p pv.
 * @return Number of params parsed.
 */
int csi_params(int *pv, int maxn);

/**
 * @brief @c CSI @c m — Select Graphic Rendition. Defined in @c sgr.c; called
 * from @c csi.c's @ref handle_csi switch.
 */
void csi_sgr(void);

/**
 * @brief @c CSI dispatcher — final-byte switch for a buffered CSI sequence.
 * Defined in @c csi.c; called from @c ansi.c's state machine when @c esc_buf
 * is complete.
 */
void handle_csi(void);

/**
 * @brief Push one byte through the ANSI/CSI/charset state machine.
 *
 * Recognised sequences mutate @c fb_ctx.cur_* / @c fb_ctx.cx / cy directly;
 * bytes that survive the parser fall through to UTF-8 decoding and
 * @ref put_cp_at_cursor. Single entry point so @ref fb_console_write_raw
 * is a tight loop over this function.
 *
 * @param b  Input byte.
 */
void feed_byte(u8 b);

#endif /* ALCOR2_KERNEL_DRIVERS_FB_CONSOLE_INTERNAL_H */
