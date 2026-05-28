/**
 * @file src/kernel/drivers/fb_console/palette.c
 * @brief Catppuccin Mocha ANSI palette and xterm-256 resolution.
 *
 * The 16-colour tables and the @ref ansi256_to_rgb resolver live together
 * because they share an array index — @c ansi256_to_rgb falls through to
 * @ref ansi16_fg / @ref ansi16_fg_bright for indices 0..15 instead of
 * duplicating the values. Kept in-kernel rather than pushed to the spazer
 * graphics SDK because the boot-time console must paint before any userspace
 * lib is loaded.
 */

#include <alcor2/types.h>
#include <kernel/drivers/fb_console/internal.h>

/**
 * @brief Catppuccin Mocha 16-colour foreground palette.
 *
 * Indices are the ANSI SGR codes minus the @c 30 base — slot 0 is @c SGR @c 30
 * (black), slot 7 is @c SGR @c 37 (white).
 */
const u32 ansi16_fg[8] = {
    0x45475au, 0xf38ba8u, 0xa6e3a1u, 0xf9e2afu,
    0x89b4fau, 0xf5c2e7u, 0x94e2d5u, 0xbac2deu,
};

/**
 * @brief Catppuccin Mocha bright-foreground palette (@c SGR @c 90..@c 97).
 *
 * Slot 0 is the "bright black" surface tone, used as a slightly lighter
 * background tint by some TUIs; the other slots match the non-bright palette
 * except for magenta/cyan/white which are pushed brighter so reverse-video
 * stays legible.
 */
const u32 ansi16_fg_bright[8] = {
    0x585b70u, 0xf38ba8u, 0xa6e3a1u, 0xf9e2afu,
    0x89b4fau, 0xcba6f7u, 0x89dcebu, 0xa6adc8u,
};

/**
 * @brief Catppuccin Mocha 16-colour background palette (@c SGR @c 40..@c 47).
 *
 * Identical to @ref ansi16_fg because Catppuccin Mocha has no separate
 * background variants — the two arrays are kept distinct so a future palette
 * swap can tune the bg side without touching the fg renderer.
 */
const u32 ansi16_bg[8] = {
    0x45475au, 0xf38ba8u, 0xa6e3a1u, 0xf9e2afu,
    0x89b4fau, 0xf5c2e7u, 0x94e2d5u, 0xbac2deu,
};

/** @brief First palette slot of the 6x6x6 colour cube in xterm 256. */
#define XTERM256_CUBE_BASE 16u

/** @brief First palette slot of the 24-step greyscale ramp in xterm 256. */
#define XTERM256_GREY_BASE 232u

/** @brief Number of palette slots in one slice of the 6x6x6 cube
 * (one fixed red channel = 6 greens × 6 blues). */
#define XTERM256_CUBE_SLICE 36u

/** @brief Side length of the 6x6x6 colour cube along one axis. */
#define XTERM256_CUBE_SIDE 6u

/** @brief Cube axis value 1 (out of 0..5) in the 8-bit channel space — the
 * xterm cube uses {0, 95, 135, 175, 215, 255}; this is the first non-zero
 * stop. */
#define XTERM256_CUBE_STOP_1 55u

/** @brief Spacing between successive non-zero cube stops (135-95, 175-135,
 * etc.). */
#define XTERM256_CUBE_STEP 40u

/** @brief Brightness of greyscale slot 0 (palette index 232). */
#define XTERM256_GREY_BASE_LEVEL 8u

/** @brief Step between successive greyscale slots. */
#define XTERM256_GREY_STEP 10u

/**
 * @brief Pack three 0..255 channels into a 0xRRGGBB triplet.
 *
 * Tiny helper so every place that builds an RGB word uses the named shifts
 * rather than scattering @c << @c 16 / @c << @c 8 across the renderer.
 *
 * @param r  Red channel.
 * @param g  Green channel.
 * @param b  Blue channel.
 * @return Packed 0xRRGGBB.
 */
static inline u32 rgb_pack(u32 r, u32 g, u32 b)
{
  return (r << BGRA_RED_SHIFT) | (g << BGRA_GREEN_SHIFT) | b;
}

/**
 * @brief Look up one axis (red, green, or blue) of the xterm 6x6x6 cube.
 *
 * The xterm cube uses non-linear stops @c {0, 95, 135, 175, 215, 255}; modeled
 * here as "0 for stop 0, else @c XTERM256_CUBE_STOP_1 plus a step per axis
 * tick". Pulled out of @ref ansi256_to_rgb so the cube math reads once instead
 * of three times.
 *
 * @param axis  Axis position 0..5.
 * @return 8-bit channel value at that stop.
 */
static inline u32 xterm_cube_axis(unsigned axis)
{
  if(axis == 0u)
    return 0u;
  return XTERM256_CUBE_STOP_1 + XTERM256_CUBE_STEP * (axis - 1u);
}

/**
 * @brief Map an xterm 256-colour index to a 0xRRGGBB triplet.
 *
 * Three slots back-to-back in the palette: 0..15 = base + bright ANSI,
 * 16..231 = the 6x6x6 cube, 232..255 = the 24-step greyscale. Formulas
 * mirror the xterm definitions verbatim so a TUI program sees the same
 * colours it would on real xterm.
 *
 * @param idx  Palette index 0..255.
 * @return Packed RGB.
 */
u32 ansi256_to_rgb(unsigned idx)
{
  if(idx < SGR_FG_END - SGR_FG_BASE + 1u)
    return ansi16_fg[idx];
  if(idx < XTERM256_CUBE_BASE)
    return ansi16_fg_bright[idx - (SGR_FG_END - SGR_FG_BASE + 1u)];
  if(idx < XTERM256_GREY_BASE) {
    unsigned cube_idx = idx - XTERM256_CUBE_BASE;
    unsigned r_axis   = cube_idx / XTERM256_CUBE_SLICE;
    unsigned rem      = cube_idx % XTERM256_CUBE_SLICE;
    unsigned g_axis   = rem / XTERM256_CUBE_SIDE;
    unsigned b_axis   = rem % XTERM256_CUBE_SIDE;
    return rgb_pack(
        xterm_cube_axis(r_axis), xterm_cube_axis(g_axis),
        xterm_cube_axis(b_axis)
    );
  }
  u32 v = XTERM256_GREY_BASE_LEVEL +
          XTERM256_GREY_STEP * (idx - XTERM256_GREY_BASE);
  return rgb_pack(v, v, v);
}
