/**
 * Userspace framebuffer: FreeType + HarfBuzz + mmap (Fira Code ligatures, etc.).
 *
 * Default font on guest: /bin/FiraCode-Regular.ttf (SIL OFL). Any TTF/OTF works.
 */
#include <alcor2/alcor_fb_user.h>

#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <ft2build.h>
#include FT_FREETYPE_H
#include <hb-ft.h>
#include <hb.h>

static int load_file(const char *path, uint8_t **out, size_t *osz)
{
  int fd = open(path, O_RDONLY);
  if(fd < 0)
    return -1;
  off_t sz = lseek(fd, 0, SEEK_END);
  if(sz <= 0 || sz > (off_t)(16 * 1024 * 1024)) {
    close(fd);
    return -1;
  }
  lseek(fd, 0, SEEK_SET);
  auto *buf = static_cast<uint8_t *>(malloc((size_t)sz));
  if(!buf) {
    close(fd);
    return -1;
  }
  ssize_t n = read(fd, buf, (size_t)sz);
  close(fd);
  if(n != sz) {
    free(buf);
    return -1;
  }
  *out = buf;
  *osz = (size_t)sz;
  return 0;
}

static void put_px(
    uint8_t *fb, const alcor_fb_info_t *inf, int x, int y, uint32_t rgb,
    int a
)
{
  if(x < 0 || y < 0 || (uint32_t)x >= inf->width || (uint32_t)y >= inf->height)
    return;
  if(inf->bpp != 32 || inf->pitch < 4)
    return;
  size_t    o   = (size_t)y * inf->pitch + (size_t)x * 4;
  uint8_t   nr  = (uint8_t)((rgb >> 16) & 0xffu);
  uint8_t   ng  = (uint8_t)((rgb >> 8) & 0xffu);
  uint8_t   nb  = (uint8_t)(rgb & 0xffu);
  uint8_t  *p   = fb + o;
  uint32_t  aa  = (uint32_t)a;
  if(aa >= 255u) {
    p[0] = nb;
    p[1] = ng;
    p[2] = nr;
    p[3] = 0xff;
    return;
  }
  uint32_t b0 = p[0], g0 = p[1], r0 = p[2];
  p[0]        = (uint8_t)((nb * aa + b0 * (255u - aa)) / 255u);
  p[1]        = (uint8_t)((ng * aa + g0 * (255u - aa)) / 255u);
  p[2]        = (uint8_t)((nr * aa + r0 * (255u - aa)) / 255u);
  p[3]        = 0xff;
}

static void fill_bg(uint8_t *fb, const alcor_fb_info_t *inf, uint32_t rgb)
{
  for(uint32_t y = 0; y < inf->height; y++)
    for(uint32_t x = 0; x < inf->width; x++)
      put_px(fb, inf, (int)x, (int)y, rgb, 255);
}

static void blit_gray(
    uint8_t *fb, const alcor_fb_info_t *inf, int x0, int y0,
    const FT_Bitmap *bm, uint32_t fg
)
{
  for(unsigned yy = 0; yy < bm->rows; yy++) {
    for(unsigned xx = 0; xx < bm->width; xx++) {
      uint8_t a = bm->buffer[yy * bm->pitch + xx];
      if(a)
        put_px(fb, inf, x0 + (int)xx, y0 + (int)yy, fg, (int)a);
    }
  }
}

static void shape_draw_line(
    uint8_t *fb, alcor_fb_info_t *inf, FT_Face face, hb_font_t *hbfont,
    unsigned pixel_h, int pen_x, int baseline_y, const char *utf8,
    uint32_t fg
)
{
  if(FT_Set_Pixel_Sizes(face, 0, pixel_h) != 0)
    return;
  hb_ft_font_changed(hbfont);

  hb_buffer_t *buf = hb_buffer_create();
  hb_buffer_add_utf8(buf, utf8, -1, 0, -1);
  hb_buffer_guess_segment_properties(buf);
  hb_shape(hbfont, buf, nullptr, 0);

  unsigned              len  = 0;
  hb_glyph_info_t      *info = hb_buffer_get_glyph_infos(buf, &len);
  hb_glyph_position_t  *pos  = hb_buffer_get_glyph_positions(buf, &len);

  int  x = pen_x;
  int  y = baseline_y;

  for(unsigned i = 0; i < len; i++) {
    x += pos[i].x_offset >> 6;
    y -= pos[i].y_offset >> 6;

    hb_codepoint_t gid = info[i].codepoint;
    if(FT_Load_Glyph(face, gid, FT_LOAD_DEFAULT) != 0 ||
       FT_Render_Glyph(face->glyph, FT_RENDER_MODE_NORMAL) != 0) {
      x += pos[i].x_advance >> 6;
      y -= pos[i].y_advance >> 6;
      continue;
    }

    FT_GlyphSlot slot = face->glyph;
    int          bx   = x + slot->bitmap_left;
    int          by   = y - slot->bitmap_top;
    blit_gray(fb, inf, bx, by, &slot->bitmap, fg);

    x += pos[i].x_advance >> 6;
    y -= pos[i].y_advance >> 6;
  }

  hb_buffer_destroy(buf);
}

int main(int argc, char **argv)
{
  alcor_fb_info_t inf;
  if(alcor_fb_info(&inf) != 0) {
    const char msg[] = "font-demo: fb_info failed\n";
    write(2, msg, sizeof(msg) - 1);
    return 1;
  }

  uint8_t *fb = (uint8_t *)alcor_fb_mmap();
  if(!fb || fb == (uint8_t *)-1) {
    const char msg[] = "font-demo: fb_mmap syscall failed\n";
    write(2, msg, sizeof(msg) - 1);
    return 1;
  }
  if(inf.bpp != 32) {
    const char msg[] = "font-demo: need 32bpp framebuffer\n";
    write(2, msg, sizeof(msg) - 1);
    return 1;
  }

  const char *font_path =
      (argv[1] && argv[1][0]) ? argv[1] : "/bin/FiraCode-Regular.ttf";

  uint8_t *font_data = nullptr;
  size_t   font_len  = 0;
  if(load_file(font_path, &font_data, &font_len) != 0) {
    const char msg[] =
        "font-demo: open font (default /bin/FiraCode-Regular.ttf, or pass path)\n";
    write(2, msg, sizeof(msg) - 1);
    return 1;
  }

  FT_Library ftlib = nullptr;
  FT_Face    face  = nullptr;
  if(FT_Init_FreeType(&ftlib) != 0 ||
     FT_New_Memory_Face(ftlib, font_data, (FT_Long)font_len, 0, &face) != 0) {
    const char msg[] = "font-demo: FreeType init failed\n";
    write(2, msg, sizeof(msg) - 1);
    free(font_data);
    return 1;
  }

  hb_font_t *hbfont = hb_ft_font_create_referenced(face);
  if(!hbfont) {
    const char msg[] = "font-demo: hb_ft_font_create_referenced failed\n";
    write(2, msg, sizeof(msg) - 1);
    FT_Done_Face(face);
    FT_Done_FreeType(ftlib);
    free(font_data);
    return 1;
  }

  fill_bg(fb, &inf, 0x001a2430u);

  unsigned pxy = inf.height > 480 ? 32u : 22u;
  int      y0  = (int)(inf.height / 5);

  /* Ligatures: => -> != are shaped as one glyph when the font provides them. */
  const char *line1 =
      "Fira Code + HarfBuzz + FreeType — =>  ->  !=  **  flamb"
      "\xc3\xa9"
      "";

  const char *line2 =
      (argc > 2 && argv[2] && argv[2][0]) ? argv[2]
                                          : "Alcor2 userspace draws the framebuffer.";

  shape_draw_line(
      fb, &inf, face, hbfont, pxy, 24, y0 + (int)pxy * 5 / 4, line1,
      0x00a8e8ffu
  );
  shape_draw_line(
      fb, &inf, face, hbfont, pxy * 3 / 4u, 24,
      y0 + (int)pxy * 5 / 4 + (int)pxy + 16, line2, 0x0088c8ffu
  );

  hb_font_destroy(hbfont);
  FT_Done_Face(face);
  FT_Done_FreeType(ftlib);
  free(font_data);

  const char done[] = "font-demo: done.\n";
  write(2, done, sizeof(done) - 1);
  return 0;
}
