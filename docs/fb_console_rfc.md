# Kernel framebuffer console — design notes (RFC)

Branch: `feat/kernel/fb_console`

## Goal

Move the framebuffer terminal from `user/apps/shell/platform/fb_tty.c`
(~1567 lines, FreeType + HarfBuzz, full ANSI/CSI) into a kernel device
exposed as `/dev/console`. Userspace gets simple `write(1, ...)` semantics.

## Architecture: kernel text grid + userspace glyph atlas

- **Kernel** owns the framebuffer, the text cell grid, the cursor, blink
  state, and an ANSI/CSI parser. Bytes written to `/dev/console` are
  parsed and rendered.
- **Glyph rasterization stays in userspace.** Shell at startup loads Fira
  via FreeType, rasterizes every supported codepoint into a flat 2D atlas
  (e.g. 256×N glyphs at 12×24 px each), and submits the atlas through
  `ioctl(fd, FB_CONSOLE_SET_ATLAS, &meta)`. The kernel mmaps the buffer
  read-only and blits glyph cells from atlas → framebuffer.
- **No FreeType in kernel.** No HarfBuzz either, which means **no Fira
  ligatures** in the rendering path. Accepted trade-off for a much
  simpler kernel surface.

## Atlas layout

```c
typedef struct {
  u64 pixels_user;     /* userspace VA of the glyph atlas */
  u32 pixels_size;     /* total bytes in the atlas */
  u32 cell_w, cell_h;  /* glyph cell dimensions in pixels */
  u32 stride_bytes;    /* bytes per row of one glyph cell */
  u32 bpp;             /* bits per pixel — match framebuffer */
  u32 n_glyphs;        /* total glyph slots */
  u64 cp_map_user;     /* userspace VA of u32[n_cp]: codepoint → glyph_idx */
  u32 n_cp;            /* size of cp_map (covers 0..n_cp-1 codepoints) */
  u32 fallback_idx;    /* glyph for unmapped codepoints */
} fb_console_atlas_t;
```

Kernel calls vmm to validate + page-map the userspace pages, holds the
mapping for the atlas lifetime.

## Surface

### Boot

Kernel initializes the framebuffer console early (replacing today's
`console.c` boot-logger code path). Starts with the compiled-in CP437
bitmap font from `font_bitmap.h`. Boot messages render through it.

### Runtime — switching to Fira

Shell startup:
1. open `/dev/console` (inherited from init, fd 0/1/2).
2. Rasterize Fira → atlas (this is a tiny userspace helper, replaces the
   bulk of `fb_tty.c`).
3. `ioctl(FB_CONSOLE_SET_ATLAS, &meta)`.

From then on, every `write(1, ...)` renders Fira glyphs. libvega just
calls `write(1, ...)`; no host-iface dance, no `vega_host_ops_t`.

### Graphics-mode apps (doom)

`ioctl(FB_CONSOLE_YIELD)` releases the framebuffer:
- Kernel pauses console rendering.
- App `mmap`s the fb directly and draws raw pixels.
- `ioctl(FB_CONSOLE_RECLAIM)` on exit: kernel repaints the grid.

## Scope of this PR

1. **`src/kernel/drivers/fb_console.c` + `include/alcor2/drivers/fb_console.h`**: new module — text grid, ANSI/CSI state machine, bitmap glyph blitter using `font_bitmap.h`.
2. **VFS `/dev/console`**: character device routing `read` to keyboard, `write` to `fb_console_write`, `ioctl` for atlas + yield + reclaim.
3. **`src/kernel/main.c`**: replace `console_init` call site, mount `/dev/console`.
4. **`user/apps/shell/`**: replace `platform/fb_tty.c` (1567 lines) with `platform/atlas.c` (~150 lines: FreeType rasterize + ioctl submit). Shell's fd 0/1/2 = `/dev/console`, no fb intercept.
5. **`user/lib/vega`**: drop `puts` / `stdout_bytes` / `fb_tty_*` host hooks. libvega writes to fd 1. Host iface collapses to `is_builtin` / `run_builtin`.

Net delta: ~1500 lines added (kernel), ~1500 deleted (userspace fb_tty.c).

## Non-goals (later PRs)

- Multiple VTs / `/dev/tty1..N`.
- Fira ligatures (would need HarfBuzz in the renderer; revisit if/when
  we promote the atlas submitter to a dedicated `consoled` process).
- Per-process controlling tty refinements.

## Decisions (resolved)

- **UTF-8 parsing**: in kernel. `fb_console_write` keeps a small partial-byte
  buffer across calls.
- **Glyph indirection** (ncurses-friendly): hybrid.
  - ASCII (`0x00–0x7F`): direct array `glyph_idx[cp]`.
  - Non-ASCII: sorted `cp_map[]` of `{codepoint, glyph_idx}`, binary search.
    Naturally covers DEC line-drawing (`0x2500–0x257F`), block elements
    (`0x2580–0x259F`), arrows (`0x2190–0x21FF`), Latin-1 supplement
    (`0x00A0–0x00FF`).
  - Unmapped codepoints fall back to atlas's `fallback_idx` (e.g. `?`).
- **Blink timer**: piggyback on the existing PIT 100Hz IRQ. `fb_console_tick`
  decrements a counter from 50; at zero, flip phase + repaint cursor cell
  (~2Hz). Reset to "on" on any write or keystroke. Skipped while the fb is
  yielded.
