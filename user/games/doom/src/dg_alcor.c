/**
 * @file src/dg_alcor.c
 * @brief doomgeneric platform backend for Alcor2OS.
 *
 * Implements the five DG_* hooks. The framebuffer is 32bpp 0xAARRGGBB, matching
 * DG_ScreenBuffer, so a pixel blit is one OR with 0xFF000000 (no swizzle).
 * stdin runs raw with the FR layout (ZQSD → z/q/s/d); key-up uses the kernel's
 * \x00<ch> release sentinels, so a key is held until its precise break code.
 */

#include "d_event.h"
#include "doomgeneric.h"
#include "doomkeys.h"

#include <alcor2/alcor_fb_user.h>
#include <alcor2/arch/pit.h>
#include <alcor2/drivers/mouse.h>
#include <alcor2/fb_console_ioctl.h>

#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <stdint.h>
#include <string.h>
#include <sys/ioctl.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>

/** @brief Maximum number of simultaneously held keys tracked. */
#define MAX_HELD 8

/** @brief Capacity of the event ring buffer (must be a power of two). */
#define EVT_QUEUE_CAP 32u

/**
 * @brief Upper bound on the framebuffer upscale factor.
 *
 * A 320×200 source gains no visible detail past ~5× (1600×1000), while the
 * per-frame write volume grows quadratically — an unclamped 10× at 4K is
 * 25 MB/frame and saturates the bus even with write-combining.  This also
 * bounds the DG_DrawFrame row buffer, so it must not be raised without
 * resizing that buffer.
 */
#define MAX_SCALE 5u

/*
 * Keyboard layout ioctl — mirrors include/alcor2/kbd.h values so this file
 * does not depend on kernel headers.
 */

#define ALCOR2_IOC_KBD_SET_LAYOUT                                              \
  ((1u << 30) | (0x4bu << 8) | 1u | (sizeof(uint32_t) << 16))

/* Enable \x00<ch> release events from the keyboard driver.  When active, each
 * key RELEASE emits this two-byte sentinel so we can drop keys from the held
 * table immediately rather than waiting for a repeat timeout.  This fixes
 * diagonal movement (Z+D) where PS/2 stops repeating the first key once a
 * second key is pressed. */
#define ALCOR2_IOC_KBD_RELEASE_EVENTS                                          \
  ((1u << 30) | (0x4bu << 8) | 2u | (sizeof(uint32_t) << 16))

#define KBD_LAYOUT_US 0u
#define KBD_LAYOUT_FR 1u

/*
 * Key-binding globals (defined in doomgeneric/m_controls.c).
 * Set before doomgeneric_Create so the game uses ZQSD / AZERTY bindings
 * from the very first tick.  M_LoadDefaults calls M_BindVariable (which
 * just registers pointers) and LoadDefaultCollection is a no-op without
 * ORIGCODE, so these values are never overwritten at runtime.
 */

extern int key_right, key_left, key_up, key_down;
extern int key_strafeleft, key_straferight;
extern int key_fire, key_use, key_strafe, key_speed;
extern int key_menu_up, key_menu_down, key_menu_left, key_menu_right;

/* Mouse button indices (defined in m_controls.c). */
extern int  mousebfire, mousebstrafe, mousebforward, mousebuse;

static void apply_alcor_key_bindings(void)
{
  /* FR (AZERTY) layout, mouse-centric:
   *   Z/S   — forward / backward
   *   Q/D   — strafe left / right  (mouse handles turning)
   *   ←/→   — turn left / right
   *   A     — fire (keyboard)
   *   E     — use / open
   *   Space — use (secondary)
   *   Shift — run
   */
  key_up    = 'z';
  key_down  = 's';
  key_left  = KEY_LEFTARROW;
  key_right = KEY_RIGHTARROW;

  key_strafeleft  = 'q';
  key_straferight = 'd';
  key_fire        = 'a';
  key_use         = 'e';
  key_speed       = KEY_RSHIFT;
  key_strafe      = KEY_RALT;

  /* Mouse buttons: left=fire (default), right=use, middle=nothing. */
  mousebfire    = 0;
  mousebstrafe  = -1;
  mousebuse     = 1;
  mousebforward = -1;

  /* Menu navigation follows movement keys. */
  key_menu_up    = 'z';
  key_menu_down  = 's';
  key_menu_left  = KEY_LEFTARROW;
  key_menu_right = KEY_RIGHTARROW;
}

/* Framebuffer state */

static uint32_t       *s_fb32; /* mapped framebuffer (u32 pixels)   */
static alcor_fb_info_t s_fbinfo;
static uint32_t        s_pitch32; /* framebuffer pitch in u32 units    */
static uint32_t        s_scale;   /* Doom pixels → native pixels       */
static uint32_t        s_off_x;   /* left margin to centre the image   */
static uint32_t        s_off_y;   /* top margin to centre the image    */

/* Keyboard state */

static struct termios s_saved_tio;
static int            s_raw_mode_active;
static int            s_layout_changed;
static int            s_mouse_fd = -1;

struct held_entry
{
  unsigned char dk; /* Doom key value; 0 = free slot */
};

static struct held_entry s_held[MAX_HELD];

struct key_event
{
  unsigned char dk;
  int           pressed;
};

static struct key_event s_evq[EVT_QUEUE_CAP];
static uint32_t         s_evq_r; /* read index  */
static uint32_t         s_evq_w; /* write index */

/* Event queue helpers */

static int evq_push(unsigned char dk, int pressed)
{
  uint32_t next = (s_evq_w + 1u) & (EVT_QUEUE_CAP - 1u);
  if(next == s_evq_r)
    return 0; /* queue full — drop */
  s_evq[s_evq_w].dk      = dk;
  s_evq[s_evq_w].pressed = pressed;
  s_evq_w                = next;
  return 1;
}

static int evq_pop(unsigned char *dk, int *pressed)
{
  if(s_evq_r == s_evq_w)
    return 0;
  *dk      = s_evq[s_evq_r].dk;
  *pressed = s_evq[s_evq_r].pressed;
  s_evq_r  = (s_evq_r + 1u) & (EVT_QUEUE_CAP - 1u);
  return 1;
}

/* Held-key tracking. The kernel emits a precise \x00<ch> break code on every
 * key-up (ALCOR2_IOC_KBD_RELEASE_EVENTS), so a key is held from its make code
 * until its break code — no repeat-timeout heuristic, which would otherwise
 * kill a held key once PS/2 stops repeating it (it only repeats the last key,
 * breaking Z then D). */

/** First make code for a key pushes a press; later repeats are ignored. */
static void hold_press(unsigned char dk)
{
  for(int i = 0; i < MAX_HELD; i++)
    if(s_held[i].dk == dk)
      return; /* already held; PS/2 repeat, nothing to do */

  for(int i = 0; i < MAX_HELD; i++) {
    if(s_held[i].dk == 0) {
      s_held[i].dk = dk;
      evq_push(dk, 1);
      return;
    }
  }
  /* All slots occupied (>8 keys held simultaneously) — drop. */
}

/** Break code: clear the held key and push a release. */
static void hold_release(unsigned char dk)
{
  for(int i = 0; i < MAX_HELD; i++) {
    if(s_held[i].dk == dk) {
      evq_push(dk, 0);
      s_held[i].dk = 0;
      return;
    }
  }
}

/* Key translation */

/**
 * @brief Map a raw input byte to a Doom key.
 *
 * Ctrl+letter combinations (0x01-0x1A, excluding CR and LF) are mapped to
 * KEY_RCTRL so any Ctrl chord acts as the "fire" button.
 * Uppercase letters are normalised to lowercase (Doom is case-insensitive).
 */
static unsigned char translate_byte(unsigned char b)
{
  if(b >= 0x01u && b <= 0x1Au && b != 0x0Au && b != 0x0Du)
    return KEY_RCTRL;
  if(b >= 'A' && b <= 'Z')
    return (unsigned char)(b + 32);
  return b;
}

/**
 * @brief Parse a raw input buffer and push key events into the queue.
 *
 * Handles:
 *   - ANSI CSI arrow/function sequences: \x1b [ A/B/C/D and \x1b [ <n> ~
 *   - SS3 arrow sequences (DECCKM mode): \x1b O A/B/C/D
 *   - SS3 function keys F1-F4:           \x1b O P/Q/R/S
 *   - Lone ESC:                          \x1b with no recognised continuation
 *   - CR / LF:                           mapped to KEY_ENTER
 *   - All other bytes:                   passed through translate_byte()
 */
static void parse_input(const uint8_t *buf, int n)
{
  for(int i = 0; i < n;) {
    unsigned char dk;

    /* \x00<ch> — key-release sentinel emitted by the kernel when
     * ALCOR2_IOC_KBD_RELEASE_EVENTS is enabled.  Process immediately without
     * going through hold_press. */
    if(buf[i] == 0x00u) {
      if(i + 1 < n) {
        dk = translate_byte(buf[i + 1]);
        if(dk != 0)
          hold_release(dk);
        i += 2;
      } else
        i++; /* lone \x00 — skip */
      continue;
    }

    if(buf[i] == 0x1Bu) {
      if(i + 2 < n && buf[i + 1] == '[') {
        /* CSI sequence: \x1b [ <suffix> */
        switch(buf[i + 2]) {
        case 'A':
          dk = KEY_UPARROW;
          i += 3;
          break;
        case 'B':
          dk = KEY_DOWNARROW;
          i += 3;
          break;
        case 'C':
          dk = KEY_RIGHTARROW;
          i += 3;
          break;
        case 'D':
          dk = KEY_LEFTARROW;
          i += 3;
          break;

        /* VT220 function key: \x1b [ <n> ~ (n = 11-15 = F1-F5, 17-21 = F6-F10)
         */
        default: {
          int code = 0, j = i + 2;
          while(j < n && buf[j] >= '0' && buf[j] <= '9')
            code = code * 10 + (buf[j++] - '0');
          if(j < n && buf[j] == '~') {
            j++;
            int fk = -1;
            if(code >= 11 && code <= 15)
              fk = code - 11;
            else if(code >= 17 && code <= 21)
              fk = code - 12;
            dk = (fk >= 0) ? (unsigned char)(KEY_F1 + fk) : 0;
            i  = j;
          } else {
            i++; /* unrecognised — consume ESC and retry */
            continue;
          }
          break;
        }
        }
      } else if(i + 2 < n && buf[i + 1] == 'O') {
        /* SS3 sequence: \x1b O <suffix> — emitted by the kernel in DECCKM
         * mode (app_cursor_keys=true) and for F1-F4 unconditionally. */
        switch(buf[i + 2]) {
        case 'A':
          dk = KEY_UPARROW;
          i += 3;
          break;
        case 'B':
          dk = KEY_DOWNARROW;
          i += 3;
          break;
        case 'C':
          dk = KEY_RIGHTARROW;
          i += 3;
          break;
        case 'D':
          dk = KEY_LEFTARROW;
          i += 3;
          break;
        case 'P':
          dk = KEY_F1;
          i += 3;
          break;
        case 'Q':
          dk = (unsigned char)(KEY_F1 + 1);
          i += 3;
          break;
        case 'R':
          dk = (unsigned char)(KEY_F1 + 2);
          i += 3;
          break;
        case 'S':
          dk = (unsigned char)(KEY_F1 + 3);
          i += 3;
          break;
        default:
          dk = KEY_ESCAPE;
          i++;
          break;
        }
      } else {
        dk = KEY_ESCAPE;
        i++;
      }
    } else if(buf[i] == 0x0Du || buf[i] == 0x0Au) {
      dk = KEY_ENTER;
      i++;
    } else {
      dk = translate_byte(buf[i]);
      i++;
    }

    if(dk != 0)
      hold_press(dk);
  }
}

/* Terminal + keyboard cleanup */

static void restore_terminal(void)
{
  /* Hand the framebuffer back; fb_console repaints the whole screen (margins
   * included) with the console theme, clearing the game image. */
  ioctl(STDOUT_FILENO, FB_CONSOLE_RECLAIM, 0);

  /* Drop the elevated timer rate. */
  uint32_t fast_off = 0u;
  ioctl(STDIN_FILENO, ALCOR2_IOC_TIMER_FAST, &fast_off);

  /* Disable release events before restoring layout so canonical programs don't
   * see spurious \x00<ch> sequences. */
  uint32_t rel = 0u;
  ioctl(STDIN_FILENO, ALCOR2_IOC_KBD_RELEASE_EVENTS, &rel);

  if(s_layout_changed) {
    uint32_t us = KBD_LAYOUT_US;
    ioctl(STDIN_FILENO, ALCOR2_IOC_KBD_SET_LAYOUT, &us);
  }
  if(s_raw_mode_active)
    tcsetattr(STDIN_FILENO, TCSANOW, &s_saved_tio);
  if(s_mouse_fd >= 0) {
    uint32_t rel_off = 0u;
    ioctl(s_mouse_fd, ALCOR2_IOC_MOUSE_SET_RELATIVE, &rel_off);
    close(s_mouse_fd);
    s_mouse_fd = -1;
  }
}

/** Turn-speed multiplier applied to the summed per-tick X delta. */
#define MOUSE_TURN_GAIN 6

/**
 * Hard ceiling on the per-tick X delta, in raw mouse units before gain.
 * A grabbed mouse delivers clean deltas, but a single 35 Hz tick can bundle
 * several packets during a fast flick; this caps any residual spike (e.g. the
 * burst QEMU emits on grab acquisition) without throttling normal aiming.
 */
#define MOUSE_DX_CLAMP 200

/* Current and last-posted button bitmask. PS/2 only emits a packet on change,
 * so the held state is retained; the last-posted copy lets feed_mouse_events
 * skip redundant posts (see there) without dropping a press or release. */
static int s_mouse_buttons;
static int s_mouse_last_buttons;

/**
 * @brief Drain all pending mouse packets and post one ev_mouse to Doom.
 *
 * Called once per tick before doomgeneric_Tick(). All packets queued since the
 * last tick are coalesced into a single event: X deltas are summed (clamped),
 * and the button state is re-posted every tick so held buttons keep firing.
 * data3 (vertical) stays zero — classic Doom has no vertical look.
 */
static void feed_mouse_events(void)
{
  if(s_mouse_fd < 0)
    return;

  alcor2_mouse_event_t pkt;
  int                  accum_dx = 0;
  int                  got      = 0;

  while((int)read(s_mouse_fd, &pkt, sizeof(pkt)) == (int)sizeof(pkt)) {
    accum_dx += (int)pkt.dx;
    s_mouse_buttons = (int)pkt.buttons;
    got             = 1;
  }

  /* Post only when something actually happened (motion or a button change).
   * The game loop spins this many times per tic, so posting unconditionally
   * floods Doom's 64-slot event queue and evicts the real motion events,
   * which makes the view jerky and laggy. */
  if(!got && s_mouse_buttons == s_mouse_last_buttons)
    return;
  s_mouse_last_buttons = s_mouse_buttons;

  if(accum_dx > MOUSE_DX_CLAMP)
    accum_dx = MOUSE_DX_CLAMP;
  if(accum_dx < -MOUSE_DX_CLAMP)
    accum_dx = -MOUSE_DX_CLAMP;

  event_t ev;
  ev.type  = ev_mouse;
  ev.data1 = s_mouse_buttons;            /* bit 0=left, 1=right, 2=middle */
  ev.data2 = accum_dx * MOUSE_TURN_GAIN; /* +dx = right = Doom turn right */
  ev.data3 = 0;                          /* no vertical look in classic Doom */
  ev.data4 = 0;
  D_PostEvent(&ev);
}

static void sig_handler(int sig)
{
  restore_terminal();
  const char msg[] = "doom: terminated by signal\n";
  (void)write(STDERR_FILENO, msg, sizeof(msg) - 1);
  (void)signal(sig, SIG_DFL);
  (void)raise(sig);
}

/* doomgeneric hooks */

void DG_Init(void)
{
  if(alcor_fb_info(&s_fbinfo) < 0) {
    const char msg[] = "doom: SYS_ALCOR_FB_INFO failed\n";
    (void)write(STDERR_FILENO, msg, sizeof(msg) - 1);
    _exit(1);
  }
  s_fb32 = alcor_fb_mmap();
  if(s_fb32 == (void *)-1) {
    const char msg[] = "doom: SYS_ALCOR_FB_MMAP failed\n";
    (void)write(STDERR_FILENO, msg, sizeof(msg) - 1);
    _exit(1);
  }
  s_pitch32 = s_fbinfo.pitch / 4u;

  /* Largest uniform scale that fits both axes, so the image fills as much of
   * the screen as possible without distorting Doom's 320×200 aspect ratio. */
  uint32_t sx = s_fbinfo.width / DOOMGENERIC_RESX;
  uint32_t sy = s_fbinfo.height / DOOMGENERIC_RESY;
  s_scale     = (sx < sy) ? sx : sy;
  if(s_scale < 1u)
    s_scale = 1u;
  if(s_scale > MAX_SCALE)
    s_scale = MAX_SCALE;

  /* Centre the scaled image in the framebuffer. */
  uint32_t out_w = DOOMGENERIC_RESX * s_scale;
  uint32_t out_h = DOOMGENERIC_RESY * s_scale;
  s_off_x = (s_fbinfo.width > out_w) ? (s_fbinfo.width - out_w) / 2u : 0u;
  s_off_y = (s_fbinfo.height > out_h) ? (s_fbinfo.height - out_h) / 2u : 0u;

  /* Stop the fb_console from painting its cursor and text over our pixels. */
  ioctl(STDOUT_FILENO, FB_CONSOLE_YIELD, 0);

  /* Raise the timer rate so DG_SleepMs(1) in the game loop has 1 ms (not 4 ms)
   * resolution. Released in restore_terminal / on exit. */
  uint32_t fast_on = 1u;
  ioctl(STDIN_FILENO, ALCOR2_IOC_TIMER_FAST, &fast_on);

  /* Switch stdin to raw, non-blocking mode. */
  struct termios raw;
  tcgetattr(STDIN_FILENO, &s_saved_tio);
  raw = s_saved_tio;
  raw.c_lflag &= (tcflag_t) ~(ICANON | ECHO);
  raw.c_cc[VMIN]  = 0;
  raw.c_cc[VTIME] = 0;
  tcsetattr(STDIN_FILENO, TCSANOW, &raw);
  s_raw_mode_active = 1;

  /* Switch to FR (AZERTY) layout: physical ZQSD → z/q/s/d. */
  uint32_t fr = KBD_LAYOUT_FR;
  if(ioctl(STDIN_FILENO, ALCOR2_IOC_KBD_SET_LAYOUT, &fr) == 0)
    s_layout_changed = 1;

  /* Enable key-release events: kernel emits \x00<ch> on break codes so
   * simultaneous keys (e.g. Z+D diagonal) are released precisely instead
   * of waiting for the repeat timeout. */
  uint32_t rel = 1u;
  ioctl(STDIN_FILENO, ALCOR2_IOC_KBD_RELEASE_EVENTS, &rel);

  /* Open the mouse device in non-blocking mode.  Failure is silently
   * tolerated — the game runs keyboard-only if the mouse is unavailable. */
  s_mouse_fd = open("/dev/mouse", O_RDONLY | O_NONBLOCK);
  if(s_mouse_fd >= 0) {
    /* Pin the cursor to the screen centre and let deltas flow freely. */
    uint32_t rel_on = 1u;
    ioctl(s_mouse_fd, ALCOR2_IOC_MOUSE_SET_RELATIVE, &rel_on);
  }

  (void)atexit(restore_terminal);
  (void)signal(SIGSEGV, sig_handler);
  (void)signal(SIGABRT, sig_handler);
  (void)signal(SIGTERM, sig_handler);
  (void)signal(SIGINT, sig_handler);
}

void DG_DrawFrame(void)
{
  const uint32_t *src   = DG_ScreenBuffer;
  const uint32_t  scale = s_scale;

  /* Base of the centred output region. */
  uint32_t *fb_base = s_fb32 + (size_t)s_off_y * s_pitch32 + s_off_x;

  /* Fast path for scale==1: blit with alpha fill, one row at a time. */
  if(scale == 1) {
    for(uint32_t sy = 0; sy < DOOMGENERIC_RESY; sy++) {
      const uint32_t *src_row = src + (size_t)sy * DOOMGENERIC_RESX;
      uint32_t       *dst     = fb_base + (size_t)sy * s_pitch32;
      for(uint32_t sx = 0; sx < DOOMGENERIC_RESX; sx++)
        dst[sx] = src_row[sx] | 0xFF000000u;
    }
    return;
  }

  /* Scaled path: build one scaled row in a stack buffer then memcpy it for
   * each duplicate scanline.  This replaces N scatter-writes per pixel with
   * one sequential fill + (scale-1) memcpys.  Sized for MAX_SCALE. */
  uint32_t row_buf[DOOMGENERIC_RESX * MAX_SCALE];
  size_t   row_bytes = (size_t)DOOMGENERIC_RESX * scale * sizeof(uint32_t);

  for(uint32_t sy = 0; sy < DOOMGENERIC_RESY; sy++) {
    const uint32_t *src_row = src + (size_t)sy * DOOMGENERIC_RESX;

    /* Expand one source row into row_buf. */
    uint32_t *out = row_buf;
    for(uint32_t sx = 0; sx < DOOMGENERIC_RESX; sx++) {
      uint32_t px = src_row[sx] | 0xFF000000u;
      for(uint32_t rx = 0; rx < scale; rx++)
        *out++ = px;
    }

    /* Copy the expanded row into each duplicate scanline. */
    uint32_t *dst0 = fb_base + (size_t)sy * scale * s_pitch32;
    /* cppcheck-suppress uninitvar ; row_buf was filled by the loop above
     * via *out++ but cppcheck doesn't track pointer-increment writes. */
    memcpy(dst0, row_buf, row_bytes);
    for(uint32_t ry = 1; ry < scale; ry++)
      memcpy(dst0 + (size_t)ry * s_pitch32, row_buf, row_bytes);
  }
}

void DG_SleepMs(uint32_t ms)
{
  struct timespec ts = {
      .tv_sec  = (time_t)(ms / 1000u),
      .tv_nsec = (long)((ms % 1000u) * 1000000L),
  };
  nanosleep(&ts, NULL);
}

uint32_t DG_GetTicksMs(void)
{
  struct timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return (uint32_t)((uint64_t)ts.tv_sec * 1000u +
                    (uint64_t)ts.tv_nsec / 1000000u);
}

int DG_GetKey(int *pressed, unsigned char *key)
{
  struct pollfd pfd = {.fd = STDIN_FILENO, .events = POLLIN};
  if(poll(&pfd, 1, 0) > 0) {
    uint8_t buf[16];
    int     n = (int)read(STDIN_FILENO, buf, sizeof(buf));
    if(n > 0)
      parse_input(buf, n);
  }

  return evq_pop(key, pressed);
}

void DG_SetWindowTitle(const char *title)
{
  (void)title; /* no window manager in a bare framebuffer context */
}

/* Entry point */

/* WAD candidates searched in order when the user does not pass -iwad. */
static const char *s_wad_candidates[] = {
    "/games/doom/freedoom1.wad", /* Freedoom phase 1 — free IWAD replacement  */
    "/games/doom/freedoom2.wad", /* Freedoom phase 2 */
    "/games/doom/doom1.wad", /* id shareware                               */
    "/games/doom/doom.wad",  /* id registered / Ultimate Doom              */
    "/games/doom/doom2.wad",     "/games/doom/plutonia.wad",
    "/games/doom/tnt.wad",       NULL,
};

static const char *find_default_wad(void)
{
  for(int i = 0; s_wad_candidates[i]; i++)
    if(access(s_wad_candidates[i], R_OK) == 0)
      return s_wad_candidates[i];
  return s_wad_candidates[0]; /* not found — let doom print the error */
}

int main(int argc, char **argv)
{
  apply_alcor_key_bindings();

  /* Inject -iwad if the caller did not supply one, probing /games/doom/ so the
   * game launches from any working directory. */
  int has_iwad = 0;
  for(int i = 1; i < argc; i++)
    if(!strcmp(argv[i], "-iwad"))
      has_iwad = 1;

  char *patched_argv[68];
  int   patched_argc = 0;
  for(int i = 0; i < argc; i++)
    patched_argv[patched_argc++] = argv[i];

  if(!has_iwad && patched_argc + 2 < 68) {
    patched_argv[patched_argc++] = (char *)"-iwad";
    patched_argv[patched_argc++] = (char *)find_default_wad();
  }
  patched_argv[patched_argc] = NULL;

  /* doomgeneric_Create initialises the engine and runs one tick; the real loop
   * runs below. */
  doomgeneric_Create(patched_argc, patched_argv);

  /* Send Doom's periodic printf to /dev/null: otherwise it repaints the console
   * over our framebuffer. */
  int devnull = open("/dev/null", O_WRONLY);
  if(devnull >= 0) {
    dup2(devnull, STDOUT_FILENO);
    dup2(devnull, STDERR_FILENO);
    close(devnull);
  }

  for(;;) {
    feed_mouse_events();
    doomgeneric_Tick();
  }
  return 0;
}
