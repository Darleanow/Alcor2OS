/**
 * @file apps/shell/platform/atlas.c
 * @brief Rasterise a TTF into a flat CELL_W × CELL_H glyph atlas and submit
 * it to the kernel framebuffer console via @c FB_CONSOLE_SET_ATLAS.
 *
 * Covers ASCII printable, Latin-1 supplement, box-drawing, block elements,
 * and arrows — enough for ncurses-style TUIs. Unmapped codepoints fall back
 * to the kernel's CP437 bitmap.
 */

#include <alcor2/drivers/fb_console.h>
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

/* Cell pixel size + Fira pixel height. Matches the old userspace fb_tty's
 * look: a 12-px-wide, 22-px-tall cell with Fira rasterised at 20 px. */
#define CELL_W  12
#define CELL_H  22
#define FIRA_PX 20

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

  /* Count glyphs we'll rasterise. */
  uint32_t n_glyphs = 0;
  for(size_t r = 0; r < sizeof kRanges / sizeof kRanges[0]; r++)
    n_glyphs += kRanges[r].end - kRanges[r].start + 1u;
  n_glyphs += 1; /* slot 0 = fallback '?' */

  /* Allocate flat atlas (n_glyphs × CELL_W × CELL_H, 1 byte alpha per pixel).
   */
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
   * cp_map entries stay 0xFFFFFFFF and the kernel falls back to CP437. */
  for(size_t r = 0; r < sizeof kRanges / sizeof kRanges[0]; r++) {
    for(uint32_t cpi = kRanges[r].start; cpi <= kRanges[r].end; cpi++) {
      if(FT_Load_Char(face, cpi, FT_LOAD_RENDER) != 0)
        continue;
      uint32_t idx = next_idx++;
      rasterise_into_slot(face->glyph, idx, baseline, pixels);
      if(cpi < CP_MAP_SIZE)
        cp_map[cpi] = idx;
    }
  }

  FT_Done_Face(face);
  FT_Done_FreeType(lib);
  free(font_data);

  fb_console_atlas_t meta = {
      .pixels_user  = (uint64_t)(uintptr_t)pixels,
      .pixels_size  = (uint32_t)atlas_size,
      .cell_w       = CELL_W,
      .cell_h       = CELL_H,
      .stride_bytes = CELL_W, /* 1 byte alpha per pixel */
      .bpp          = 8,
      .n_glyphs     = n_glyphs,
      .cp_map_user  = (uint64_t)(uintptr_t)cp_map,
      .n_cp         = CP_MAP_SIZE,
      .fallback_idx = fallback_idx,
  };

  int rc = ioctl(STDOUT_FILENO, FB_CONSOLE_SET_ATLAS, &meta);
  /* The kernel copies pixels + cp_map into its own buffers; we can free
   * the userspace originals now. */
  free(pixels);
  free(cp_map);
  return (rc == 0) ? 0 : -1;
}
