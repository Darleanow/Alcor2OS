#include <alcor2/console.h>
#include <fcntl.h>
#include <ft2build.h>
#include <shell/atlas.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <unistd.h>
#include FT_FREETYPE_H
#include FT_SYNTHESIS_H

/* Cell pixel size + Fira pixel height. Matches the old userspace fb_tty's
 * look: a 12-px-wide, 22-px-tall cell with Fira rasterised at 20 px. */
#define CELL_W  16
#define CELL_H  30
#define FIRA_PX 26

/* Codepoint ranges to rasterise. Tuples (start, end_inclusive). */
static const struct
{
  uint32_t start, end;
} kRanges[] = {
    {0x0020u, 0x007Eu}, /* ASCII printable */
    {0x00A0u, 0x00FFu}, /* Latin-1 supplement */
    {0x2500u, 0x257Fu}, /* Box drawing */
    {0x2580u, 0x259Fu}, /* Block elements */
    {0x2190u, 0x21FFu}, /* Arrows */
};

#define CP_MAP_SIZE 0x2600u

/** Edge flags for the procedurally-drawn box-drawing glyphs below. */
enum
{
  BX_N = 1,
  BX_S = 2,
  BX_E = 4,
  BX_W = 8
};

/* ── Rounded-corner arc renderer ────────────────────────────────────────
 *
 * U+256D ╭  U+256E ╮  U+256F ╯  U+2570 ╰
 *
 * These are NOT in the procedural light-box subset (0x2500-0x253F), so
 * FreeType was used before — its glyph strokes sit at different vertical
 * midpoints than our procedural ─/│, causing visible gaps.
 *
 * We draw them ourselves: straight arms to each connecting edge plus a
 * quarter-circle arc (Bresenham integers, no SSE) that bridges them
 * seamlessly.
 * ─────────────────────────────────────────────────────────────────────── */
static int is_arc_corner(uint32_t cp)
{
  return cp >= 0x256Du && cp <= 0x2570u;
}

static void rasterise_arc_corner(uint32_t idx, uint32_t cp, uint8_t *pixels)
{
#define SET_PX(X, Y)                                                           \
  do {                                                                         \
    int _x = (X), _y = (Y);                                                    \
    if(_x >= 0 && _x < CELL_W && _y >= 0 && _y < CELL_H)                       \
      pixels                                                                   \
          [(idx * (size_t)CELL_H + (size_t)_y) * (size_t)CELL_W +              \
           (size_t)_x] = 0xffu;                                                \
  } while(0)

  int cx  = CELL_W / 2;
  int cy  = CELL_H / 2;
  int cx1 = cx - 1;
  int cy1 = cy - 1;
  int r   = CELL_W / 4;

  /* arc_cx/cy: centre of the imaginary circle (r px from cell centre
   * toward the interior).  dx/dy: sign factors mapping Bresenham octant
   * to the correct screen quadrant. */
  int arc_cx, arc_cy, dx, dy;
  int hx0, hx1; /* horizontal arm column range */
  int vy0, vy1; /* vertical   arm row    range */

  switch(cp) {
  case 0x256Du: /* ╭  right + down */
    arc_cx = cx + r;
    arc_cy = cy + r;
    dx     = -1;
    dy     = -1;
    hx0    = cx + r;
    hx1    = CELL_W - 1;
    vy0    = cy + r;
    vy1    = CELL_H - 1;
    break;
  case 0x256Eu: /* ╮  left  + down */
    arc_cx = cx - r;
    arc_cy = cy + r;
    dx     = 1;
    dy     = -1;
    hx0    = 0;
    hx1    = cx - r;
    vy0    = cy + r;
    vy1    = CELL_H - 1;
    break;
  case 0x256Fu: /* ╯  left  + up   */
    arc_cx = cx - r;
    arc_cy = cy - r;
    dx     = 1;
    dy     = 1;
    hx0    = 0;
    hx1    = cx - r;
    vy0    = 0;
    vy1    = cy - r;
    break;
  case 0x2570u: /* ╰  right + up   */
    arc_cx = cx + r;
    arc_cy = cy - r;
    dx     = -1;
    dy     = 1;
    hx0    = cx + r;
    hx1    = CELL_W - 1;
    vy0    = 0;
    vy1    = cy - r;
    break;
  default:
    return;
  }

  for(int x = hx0; x <= hx1; x++) {
    SET_PX(x, cy);
    SET_PX(x, cy1);
  }
  for(int y = vy0; y <= vy1; y++) {
    SET_PX(cx, y);
    SET_PX(cx1, y);
  }

  SET_PX(arc_cx, cy1);
  SET_PX(cx1, arc_cy);

  /* Bresenham arc: each point drawn as a 2×2 block toward (X-1, Y-1)
   * so the arc stays 2px thick and joins the 2px arms without a center
   * block that would make the elbow look square. */
  int ax = r, ay = 0, err = 1 - r;
  while(ax >= ay) {
    int X0 = arc_cx + dx * ax, Y0 = arc_cy + dy * ay;
    int X1 = arc_cx + dx * ay, Y1 = arc_cy + dy * ax;
    SET_PX(X0, Y0);
    SET_PX(X0 - 1, Y0);
    SET_PX(X0, Y0 - 1);
    SET_PX(X0 - 1, Y0 - 1);
    SET_PX(X1, Y1);
    SET_PX(X1 - 1, Y1);
    SET_PX(X1, Y1 - 1);
    SET_PX(X1 - 1, Y1 - 1);
    ay++;
    if(err < 0) {
      err += 2 * ay + 1;
    } else {
      ax--;
      err += 2 * ay - 2 * ax + 1;
    }
  }
#undef SET_PX
}

/** Light single-line box-drawing chars from U+2500..U+253F.
 *  Returns the edge bitmask, or -1 if @p cp isn't covered (FreeType handles
 * it). Drawing them ourselves guarantees adjacent cells join without TTF-edge
 * gaps. */
static int box_edges_for(uint32_t cp)
{
  switch(cp) {
  case 0x2500u:
    return BX_E | BX_W; /* ─ */
  case 0x2502u:
    return BX_N | BX_S; /* │ */
  case 0x250Cu:
    return BX_E | BX_S; /* ┌ */
  case 0x2510u:
    return BX_W | BX_S; /* ┐ */
  case 0x2514u:
    return BX_E | BX_N; /* └ */
  case 0x2518u:
    return BX_W | BX_N; /* ┘ */
  case 0x251Cu:
    return BX_N | BX_S | BX_E; /* ├ */
  case 0x2524u:
    return BX_N | BX_S | BX_W; /* ┤ */
  case 0x252Cu:
    return BX_E | BX_W | BX_S; /* ┬ */
  case 0x2534u:
    return BX_E | BX_W | BX_N; /* ┴ */
  case 0x253Cu:
    return BX_N | BX_S | BX_E | BX_W; /* ┼ */
  default:
    return -1;
  }
}

/** Paint a procedural box-drawing glyph: 2-px strokes from each connecting
 *  edge to the cell centre.
 *
 *  Stroke layout for CELL_W=12, CELL_H=22  (cx=6, cy=11):
 *    horizontal: rows cy-1..cy  (10,11) — centred on cy
 *    vertical:   cols cx-1..cx  ( 5, 6) — centred on cx
 *
 *  Every box-drawing character in this file uses the same row/col set,
 *  guaranteeing seamless joins between adjacent cells.                   */
static void rasterise_box(uint32_t idx, int edges, uint8_t *pixels)
{
  int cx  = CELL_W / 2; /* 6  */
  int cy  = CELL_H / 2; /* 11 */
  int cx1 = cx - 1;     /* 5  — second vertical column   */
  int cy1 = cy - 1;     /* 10 — second horizontal row    */

  if(edges & BX_W)
    for(int x = 0; x <= cx; x++) {
      pixels[(idx * CELL_H + cy) * CELL_W + x]  = 0xff;
      pixels[(idx * CELL_H + cy1) * CELL_W + x] = 0xff;
    }
  if(edges & BX_E)
    for(int x = cx; x < CELL_W; x++) {
      pixels[(idx * CELL_H + cy) * CELL_W + x]  = 0xff;
      pixels[(idx * CELL_H + cy1) * CELL_W + x] = 0xff;
    }
  if(edges & BX_N)
    for(int y = 0; y <= cy; y++) {
      pixels[(idx * CELL_H + y) * CELL_W + cx]  = 0xff;
      pixels[(idx * CELL_H + y) * CELL_W + cx1] = 0xff;
    }
  if(edges & BX_S)
    for(int y = cy; y < CELL_H; y++) {
      pixels[(idx * CELL_H + y) * CELL_W + cx]  = 0xff;
      pixels[(idx * CELL_H + y) * CELL_W + cx1] = 0xff;
    }
}

/** Copy the currently-loaded FreeType glyph into atlas slot @p idx,
 *  baseline-aligned to @p baseline_y and clipped to the cell box. */
static void rasterise_into_slot(
    FT_GlyphSlot s, uint32_t idx, int baseline_y, uint8_t *pixels
)
{
  int x_off = (int)s->bitmap_left;
  int y_off = baseline_y - (int)s->bitmap_top;
  if(x_off < 0)
    x_off = 0;
  for(int by = 0; by < (int)s->bitmap.rows; by++) {
    int dy = y_off + by;
    if(dy < 0 || dy >= CELL_H)
      continue;
    for(int bx = 0; bx < (int)s->bitmap.width; bx++) {
      int dx = x_off + bx;
      if(dx < 0 || dx >= CELL_W)
        continue;
      pixels[(idx * CELL_H + dy) * CELL_W + dx] =
          s->bitmap.buffer[by * s->bitmap.pitch + bx];
    }
  }
}

static int load_font_file(const char *path, uint8_t **out, size_t *osz)
{
  int fd = open(path, O_RDONLY);
  if(fd < 0)
    return -1;
  struct stat st;
  if(fstat(fd, &st) < 0 || st.st_size <= 0) {
    close(fd);
    return -1;
  }
  size_t   sz  = (size_t)st.st_size;
  uint8_t *buf = (uint8_t *)malloc(sz);
  if(!buf) {
    close(fd);
    return -1;
  }
  size_t got = 0;
  while(got < sz) {
    ssize_t n = read(fd, buf + got, sz - got);
    if(n <= 0) {
      free(buf);
      close(fd);
      return -1;
    }
    got += (size_t)n;
  }
  close(fd);
  *out = buf;
  *osz = sz;
  return 0;
}

int atlas_submit(const char *font_path)
{
  /* Load TTF into memory. */
  uint8_t *font_data;
  size_t   font_size;
  if(load_font_file(font_path, &font_data, &font_size) < 0)
    return -1;

  FT_Library lib;
  if(FT_Init_FreeType(&lib) != 0) {
    free(font_data);
    return -1;
  }
  FT_Face face;
  if(FT_New_Memory_Face(lib, font_data, (FT_Long)font_size, 0, &face) != 0) {
    FT_Done_FreeType(lib);
    free(font_data);
    return -1;
  }
  if(FT_Set_Pixel_Sizes(face, 0, FIRA_PX) != 0) {
    FT_Done_Face(face);
    FT_Done_FreeType(lib);
    free(font_data);
    return -1;
  }

  /* Count glyphs we'll rasterise (regular only; bold doubles it). */
  uint32_t n_regular = 0;
  for(size_t r = 0; r < sizeof kRanges / sizeof kRanges[0]; r++)
    n_regular += kRanges[r].end - kRanges[r].start + 1u;
  n_regular += 1; /* slot 0 = fallback '?' */

  uint32_t n_glyphs = n_regular * 3u; /* regular | bold | italic */

  /* Flat atlas: (n_glyphs × CELL_W × CELL_H), 1-byte alpha per pixel.
   * Slots [0, n_regular): regular.
   * Slots [n_regular, 2*n_regular): bold (FT_GlyphSlot_Embolden).
   * Slots [2*n_regular, 3*n_regular): italic (FT_GlyphSlot_Oblique). */
  size_t    atlas_size = (size_t)n_glyphs * (size_t)CELL_W * (size_t)CELL_H;
  uint8_t  *pixels     = (uint8_t *)calloc(atlas_size, 1);
  uint32_t *cp_map     = (uint32_t *)malloc(CP_MAP_SIZE * sizeof(uint32_t));
  if(!pixels || !cp_map) {
    free(pixels);
    free(cp_map);
    FT_Done_Face(face);
    FT_Done_FreeType(lib);
    free(font_data);
    return -1;
  }
  /* 0xFFFFFFFF = no glyph → kernel falls back to CP437 bitmap. */
  for(uint32_t i = 0; i < CP_MAP_SIZE; i++)
    cp_map[i] = 0xFFFFFFFFu;

  /* Slot 0 reserved for the fallback glyph ('?'). */
  uint32_t fallback_idx = 0;
  uint32_t next_idx     = 1;

  /* Baseline at ~13/16 of the cell height leaves room for descenders below
   * (Fira's descender at FIRA_PX ≈ 4–5 px). With CELL_H=22 → baseline=17,
   * which matches Fira's ascender at 20 px almost exactly. */
  const int baseline = (int)(CELL_H * 13 / 16);

  /* Slot 0: fallback glyph ('?'), used when a cell's codepoint isn't mapped. */
  if(FT_Load_Char(face, (uint32_t)'?', FT_LOAD_RENDER) == 0)
    rasterise_into_slot(face->glyph, fallback_idx, baseline, pixels);

  /* All requested ranges. Codepoints FreeType can't load are skipped: their
   * cp_map entries stay 0xFFFFFFFF and the kernel falls back to CP437.
   *
   * Light box-drawing chars (U+2500..U+253F subset) are drawn procedurally:
   * Fira's box glyphs don't span the full cell width, so adjacent cells
   * leave visible gaps. Painting them ourselves guarantees a continuous line.
   */
  for(size_t r = 0; r < sizeof kRanges / sizeof kRanges[0]; r++) {
    for(uint32_t cpi = kRanges[r].start; cpi <= kRanges[r].end; cpi++) {
      uint32_t idx;
      int      edges = box_edges_for(cpi);
      if(edges >= 0) {
        idx = next_idx++;
        rasterise_box(idx, edges, pixels);
      } else if(is_arc_corner(cpi)) {
        idx = next_idx++;
        rasterise_arc_corner(idx, cpi, pixels);
      } else {
        if(FT_Load_Char(face, cpi, FT_LOAD_RENDER) != 0)
          continue;
        idx = next_idx++;
        rasterise_into_slot(face->glyph, idx, baseline, pixels);
      }
      if(cpi < CP_MAP_SIZE)
        cp_map[cpi] = idx;
    }
  }

  /* Bold pass: slots n_regular..n_glyphs-1.
   * FreeType glyphs: re-render then apply FT_GlyphSlot_Embolden.
   * Procedural glyphs (box drawing / arc corners): memcpy — already thick. */
  size_t cell_bytes = (size_t)CELL_W * (size_t)CELL_H;

  if(FT_Load_Char(face, (uint32_t)'?', FT_LOAD_RENDER) == 0) {
    FT_GlyphSlot_Embolden(face->glyph);
    rasterise_into_slot(
        face->glyph, fallback_idx + n_regular, baseline, pixels
    );
  } else {
    memcpy(
        pixels + (fallback_idx + n_regular) * cell_bytes,
        pixels + fallback_idx * cell_bytes, cell_bytes
    );
  }

  for(size_t r = 0; r < sizeof kRanges / sizeof kRanges[0]; r++) {
    for(uint32_t cpi = kRanges[r].start; cpi <= kRanges[r].end; cpi++) {
      if(cpi >= CP_MAP_SIZE)
        continue;
      uint32_t reg_idx = cp_map[cpi];
      if(reg_idx == 0xFFFFFFFFu)
        continue;
      uint32_t bold_idx = reg_idx + n_regular;

      int      edges = box_edges_for(cpi);
      if(edges >= 0 || is_arc_corner(cpi)) {
        memcpy(
            pixels + bold_idx * cell_bytes, pixels + reg_idx * cell_bytes,
            cell_bytes
        );
      } else {
        if(FT_Load_Char(face, cpi, FT_LOAD_RENDER) == 0) {
          FT_GlyphSlot_Embolden(face->glyph);
          rasterise_into_slot(face->glyph, bold_idx, baseline, pixels);
        } else {
          memcpy(
              pixels + bold_idx * cell_bytes, pixels + reg_idx * cell_bytes,
              cell_bytes
          );
        }
      }
    }
  }

  /* Italic pass: slots 2*n_regular..3*n_regular-1.
   * FT_GlyphSlot_Oblique shears the rendered bitmap in-place. */
  uint32_t italic_base = 2u * n_regular;

  if(FT_Load_Char(face, (uint32_t)'?', FT_LOAD_RENDER) == 0) {
    FT_GlyphSlot_Oblique(face->glyph);
    rasterise_into_slot(
        face->glyph, fallback_idx + italic_base, baseline, pixels
    );
  } else {
    memcpy(
        pixels + (fallback_idx + italic_base) * cell_bytes,
        pixels + fallback_idx * cell_bytes, cell_bytes
    );
  }

  for(size_t r = 0; r < sizeof kRanges / sizeof kRanges[0]; r++) {
    for(uint32_t cpi = kRanges[r].start; cpi <= kRanges[r].end; cpi++) {
      if(cpi >= CP_MAP_SIZE)
        continue;
      uint32_t reg_idx = cp_map[cpi];
      if(reg_idx == 0xFFFFFFFFu)
        continue;
      uint32_t italic_idx = reg_idx + italic_base;

      int      edges = box_edges_for(cpi);
      if(edges >= 0 || is_arc_corner(cpi)) {
        memcpy(
            pixels + italic_idx * cell_bytes, pixels + reg_idx * cell_bytes,
            cell_bytes
        );
      } else {
        if(FT_Load_Char(face, cpi, FT_LOAD_RENDER) == 0) {
          FT_GlyphSlot_Oblique(face->glyph);
          rasterise_into_slot(face->glyph, italic_idx, baseline, pixels);
        } else {
          memcpy(
              pixels + italic_idx * cell_bytes, pixels + reg_idx * cell_bytes,
              cell_bytes
          );
        }
      }
    }
  }

  FT_Done_Face(face);
  FT_Done_FreeType(lib);
  free(font_data);

  alcor_console_atlas_t meta = {
      .pixels_user   = (uint64_t)(uintptr_t)pixels,
      .pixels_size   = (uint32_t)atlas_size,
      .cell_w        = CELL_W,
      .cell_h        = CELL_H,
      .stride_bytes  = CELL_W,
      .bpp           = 8,
      .n_glyphs      = n_glyphs,
      .cp_map_user   = (uint64_t)(uintptr_t)cp_map,
      .n_cp          = CP_MAP_SIZE,
      .fallback_idx  = fallback_idx,
      .bold_offset   = n_regular,
      .italic_offset = italic_base,
  };

  int rc = alcor_console_set_atlas(&meta);
  /* The kernel copies pixels + cp_map into its own buffers; we can free
   * the userspace originals now. */
  free(pixels);
  free(cp_map);
  return (rc == 0) ? 0 : -1;
}
