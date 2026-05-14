/**
 * @file src/kernel/input/kbd_layout.c
 * @brief PS/2 set 1 scancodes to bytes: US or FR AZERTY (+ AltGr), CSI arrow
 * keys, and UTF-8 emission for Latin-1 codepoints.
 *
 * AZERTY produces accents (é, à, ç, è, ù, î, …) and the §/°/¨/£/¤/µ family.
 * Those codepoints live in U+0080..U+00FF, so the lookup tables store the
 * raw Latin-1 byte; ::emit_user_cp transcodes anything above 0x7f into its
 * 2-byte UTF-8 encoding before pushing it to the read queue. Apps and the
 * shell see the exact same byte stream a real UTF-8 terminal would feed
 * them, which keeps the FB tty decoder happy and stops ncurses' keyname()
 * from reporting "M-i" for é.
 */

#include <alcor2/arch/cpu.h>
#include <alcor2/drivers/console.h>
#include <alcor2/drivers/keyboard.h>
#include <alcor2/kbd.h>
#include <alcor2/kstdlib.h>
#include <alcor2/ktermios.h>
#include <alcor2/proc/proc.h>

#define LAT_mu      '\xb5'
#define LAT_deg     '\xb0'
#define LAT_sect    '\xa7'
#define LAT_ucce    '\xe7'
#define LAT_ucee    '\xe8'
#define LAT_uca0    '\xe0'
#define LAT_uce9    '\xe9'
#define LAT_ucf9    '\xf9'
#define LAT_diae    '\xa8'
#define LAT_poun    '\xa3'
#define LAT_curr    '\xa4'

#define OUT_PEND_SZ 256
static unsigned char out_pend_buf[OUT_PEND_SZ];
static u32           out_pend_w;
static u32           out_pend_r;

static bool          out_pend_take(unsigned char *c)
{
  if(out_pend_r == out_pend_w)
    return false;
  *c         = out_pend_buf[out_pend_r];
  out_pend_r = (out_pend_r + 1) % OUT_PEND_SZ;
  return true;
}

static bool out_pend_push(unsigned char b)
{
  u32 next = (out_pend_w + 1) % OUT_PEND_SZ;
  if(next == out_pend_r)
    return false;
  out_pend_buf[out_pend_w] = b;
  out_pend_w               = next;
  return true;
}

static void pend_csi(char tail)
{
  out_pend_push(0x1b);
  out_pend_push('[');
  out_pend_push((unsigned char)tail);
}

/**
 * @brief Deliver one user-visible codepoint to the keyboard byte stream.
 *
 * ASCII bytes (< 0x80) flow straight into @p out so the common case stays a
 * single-byte read. Latin-1 supplement codepoints (0x80..0xff, the AZERTY
 * é/à/ç/è/ù/î and the dead-key punctuation) are transcoded to their 2-byte
 * UTF-8 encoding and pushed to @c out_pend, matching what every modern
 * terminal feeds to its TTY. The caller (@c kbd_pop_byte) drains the queue
 * before re-entering @c process_raw_ctx, so the second UTF-8 byte surfaces
 * on the very next read.
 *
 * Dry-run mode reports readability without touching state.
 *
 * @return @c true when @p out was set or at least one byte is now queued.
 */
static bool emit_user_cp(unsigned char cp, unsigned char *out, bool dry)
{
  if(dry)
    return true;
  if(cp < 0x80u) {
    *out = cp;
    return true;
  }
  /* Latin-1 → UTF-8: 0xC0|(cp>>6), 0x80|(cp&0x3F).  No need for the 3/4-byte
   * cases — the layout tables only hold codepoints up to 0xff. */
  out_pend_push((unsigned char)(0xc0u | (cp >> 6u)));
  out_pend_push((unsigned char)(0x80u | (cp & 0x3fu)));
  return false;
}

static const unsigned char us_pl[128] = {
    0,   0x1b, '1',  '2', '3',  '4', '5', '6', '7', '8', '9', '0', '-',
    '=', '\b', '\t', 'q', 'w',  'e', 'r', 't', 'y', 'u', 'i', 'o', 'p',
    '[', ']',  '\n', 0,   'a',  's', 'd', 'f', 'g', 'h', 'j', 'k', 'l',
    ';', '\'', '`',  0,   '\\', 'z', 'x', 'c', 'v', 'b', 'n', 'm', ',',
    '.', '/',  0,    '*', 0,    ' ', 0,   0,   0,   0,   0,   0,   0,
    0,   0,    0,    0,   0,    0,   '7', '8', '9', '-', '4', '5', '6',
    '+', '1',  '2',  '3', '0',  '.', 0,   0,   0,   0,   0,   0,   0,
    0,   0,    0,    0,   0,    0,   0,   0,   0,   0,   0,   0,   0,
    0,   0,    0,    0,   0,    0,   0,   0,   0,   0,   0,   0,   0,
    0,   0,    0,    0,   0,    0,   0,   0,   0,   0,   0
};

static const unsigned char us_sh[128] = {
    0,   0x1b, '!',  '@', '#', '$', '%', '^', '&', '*', '(', ')', '_',
    '+', '\b', '\t', 'Q', 'W', 'E', 'R', 'T', 'Y', 'U', 'I', 'O', 'P',
    '{', '}',  '\n', 0,   'A', 'S', 'D', 'F', 'G', 'H', 'J', 'K', 'L',
    ':', '"',  '~',  0,   '|', 'Z', 'X', 'C', 'V', 'B', 'N', 'M', '<',
    '>', '?',  0,    '*', 0,   ' ', 0,   0,   0,   0,   0,   0,   0,
    0,   0,    0,    0,   0,   0,   '7', '8', '9', '-', '4', '5', '6',
    '+', '1',  '2',  '3', '0', '.', 0,   0,   0,   0,   0,   0,   0,
    0,   0,    0,    0,   0,   0,   0,   0,   0,   0,   0,   0,   0,
    0,   0,    0,    0,   0,   0,   0,   0,   0,   0,   0,   0,   0,
    0,   0,    0,    0,   0,   0,   0,   0,   0,   0,   0
};

static unsigned char       fr_pl[128];
static unsigned char       fr_sh[128];
static bool                fr_ready;

static const unsigned char fr_alt[128] = {
    [0x03] = '~', [0x04] = '#', [0x05] = '{',  [0x06] = '[',
    [0x07] = '|', [0x08] = '`', [0x09] = '\\', [0x0a] = '^',
    [0x0b] = '@', [0x0c] = ']', [0x0d] = '}',  [0x1b] = LAT_curr,
};

static void fr_tables_init(void)
{
  if(fr_ready)
    return;
  kmemcpy(fr_pl, us_pl, sizeof(fr_pl));
  kmemcpy(fr_sh, us_sh, sizeof(fr_sh));

  /* Digit row vs US numbers row */
  fr_pl[0x02] = '&';
  fr_sh[0x02] = '1';
  fr_pl[0x03] = LAT_uce9;
  fr_sh[0x03] = '2';
  fr_pl[0x04] = '"';
  fr_sh[0x04] = '3';
  fr_pl[0x05] = '\'';
  fr_sh[0x05] = '4';
  fr_pl[0x06] = '(';
  fr_sh[0x06] = '5';
  fr_pl[0x07] = '-';
  fr_sh[0x07] = '6';
  fr_pl[0x08] = LAT_ucee;
  fr_sh[0x08] = '7';
  fr_pl[0x09] = '_';
  fr_sh[0x09] = '8';
  fr_pl[0x0a] = LAT_ucce;
  fr_sh[0x0a] = '9';
  fr_pl[0x0b] = LAT_uca0;
  fr_sh[0x0b] = '0';
  fr_pl[0x0c] = ')';
  fr_sh[0x0c] = LAT_deg;
  fr_pl[0x0d] = '=';
  fr_sh[0x0d] = '+';

  /* Letter rows (AZERTY) */
  fr_pl[0x10] = 'a';
  fr_sh[0x10] = 'A';
  fr_pl[0x11] = 'z';
  fr_sh[0x11] = 'Z';
  fr_pl[0x12] = 'e';
  fr_sh[0x12] = 'E';
  fr_pl[0x13] = 'r';
  fr_sh[0x13] = 'R';
  fr_pl[0x14] = 't';
  fr_sh[0x14] = 'T';
  fr_pl[0x15] = 'y';
  fr_sh[0x15] = 'Y';
  fr_pl[0x16] = 'u';
  fr_sh[0x16] = 'U';
  fr_pl[0x17] = 'i';
  fr_sh[0x17] = 'I';
  fr_pl[0x18] = 'o';
  fr_sh[0x18] = 'O';
  fr_pl[0x19] = 'p';
  fr_sh[0x19] = 'P';
  fr_pl[0x1a] = '^';
  fr_sh[0x1a] = LAT_diae;
  fr_pl[0x1b] = '$';
  fr_sh[0x1b] = LAT_poun;

  /* Home row continuation */
  fr_pl[0x1e] = 'q';
  fr_sh[0x1e] = 'Q';
  fr_pl[0x1f] = 's';
  fr_sh[0x1f] = 'S';
  fr_pl[0x20] = 'd';
  fr_sh[0x20] = 'D';
  fr_pl[0x21] = 'f';
  fr_sh[0x21] = 'F';
  fr_pl[0x22] = 'g';
  fr_sh[0x22] = 'G';
  fr_pl[0x23] = 'h';
  fr_sh[0x23] = 'H';
  fr_pl[0x24] = 'j';
  fr_sh[0x24] = 'J';
  fr_pl[0x25] = 'k';
  fr_sh[0x25] = 'K';
  fr_pl[0x26] = 'l';
  fr_sh[0x26] = 'L';
  fr_pl[0x27] = 'm';
  fr_sh[0x27] = 'M';
  fr_pl[0x28] = LAT_ucf9;
  fr_sh[0x28] = '%';
  fr_pl[0x29] = '*';
  fr_sh[0x29] = LAT_mu;

  /* Bottom alpha row + punctuation; ISO `<` next to left Shift */
  fr_pl[0x2c] = 'w';
  fr_sh[0x2c] = 'W';
  fr_pl[0x2d] = 'x';
  fr_sh[0x2d] = 'X';
  fr_pl[0x2e] = 'c';
  fr_sh[0x2e] = 'C';
  fr_pl[0x2f] = 'v';
  fr_sh[0x2f] = 'V';
  fr_pl[0x30] = 'b';
  fr_sh[0x30] = 'B';
  fr_pl[0x31] = 'n';
  fr_sh[0x31] = 'N';
  fr_pl[0x32] = ',';
  fr_sh[0x32] = '?';
  fr_pl[0x33] = ';';
  fr_sh[0x33] = '.';
  fr_pl[0x34] = ':';
  fr_sh[0x34] = '/';
  fr_pl[0x35] = '!';
  fr_sh[0x35] = LAT_sect;
  fr_pl[0x56] = '<';
  fr_sh[0x56] = '>';

  fr_ready = true;
}

static const unsigned char *pick_pl(kbd_layout_t id)
{
  if(id == KBD_LAYOUT_FR)
    fr_tables_init();
  return id == KBD_LAYOUT_FR ? fr_pl : us_pl;
}

static const unsigned char *pick_sh(kbd_layout_t id)
{
  if(id == KBD_LAYOUT_FR)
    fr_tables_init();
  return id == KBD_LAYOUT_FR ? fr_sh : us_sh;
}

static bool fr_caps_scan(u8 key)
{
  if((key >= 0x10 && key <= 0x19) || (key >= 0x1e && key <= 0x28) ||
     (key >= 0x2c && key <= 0x31))
    return true;
  return false;
}

static bool us_caps_scan(u8 key)
{
  unsigned char p = us_pl[key];
  return p >= 'a' && p <= 'z';
}

#define KBD_RAW_PEEK_MAX 256

static kbd_layout_t layout = KBD_LAYOUT_US;

typedef struct
{
  bool        pend_e0;
  bool        lalt_dn;
  bool        ralt_dn;
  key_state_t mod;
} kbd_ev_ctx_t;

static kbd_ev_ctx_t g_kbd = {0};

void                kbd_set_layout(kbd_layout_t lay)
{
  if((unsigned)lay >= KBD_LAYOUT_COUNT)
    lay = KBD_LAYOUT_US;
  layout     = lay;
  out_pend_w = out_pend_r = 0;
  g_kbd.pend_e0           = false;
  g_kbd.lalt_dn           = false;
  g_kbd.ralt_dn           = false;
}

kbd_layout_t kbd_get_layout(void)
{
  return layout;
}

/**
 * @param dry If true, do not push CSI or set @a *out; only report if read would
 *            produce a user-visible byte (for select(2) readability).
 * @return true when @a *out should be delivered (dry) or out_pend was fed.
 */
static bool
    process_raw_ctx(u8 raw, kbd_ev_ctx_t *s, unsigned char *out, bool dry)
{
  if(raw == 0xe0) {
    s->pend_e0 = true;
    return false;
  }

  if(s->pend_e0) {
    s->pend_e0    = false;
    bool released = (raw & KEY_RELEASE) != 0;
    u8   ext      = raw & (u8)~KEY_RELEASE;

    if(ext == 0x38) {
      s->ralt_dn = !released;
      s->mod.alt = (s->lalt_dn || s->ralt_dn);
      return false;
    }

    if(released)
      return false;

    switch(ext) {
    case 0x48:
      if(dry)
        return true;
      pend_csi('A');
      break;
    case 0x50:
      if(dry)
        return true;
      pend_csi('B');
      break;
    case 0x4b:
      if(dry)
        return true;
      pend_csi('D');
      break;
    case 0x4d:
      if(dry)
        return true;
      pend_csi('C');
      break;
    case 0x53:
      if(!dry)
        *out = 0x7f;
      return true;
    default:
      break;
    }
    return false;
  }

  bool released = (raw & KEY_RELEASE) != 0;
  u8   key      = raw & ~KEY_RELEASE;
  if(key >= 128)
    return false;

  switch(key) {
  case KEY_LSHIFT:
  case KEY_RSHIFT:
    s->mod.shift = !released;
    return false;
  case KEY_LCTRL:
    s->mod.ctrl = !released;
    return false;
  case KEY_LALT:
    s->lalt_dn = !released;
    s->mod.alt = (s->lalt_dn || s->ralt_dn);
    return false;
  case KEY_CAPSLOCK:
    if(!released)
      s->mod.capslock = !s->mod.capslock;
    return false;
  default:
    break;
  }

  if(released)
    return false;

  const unsigned char *pl = pick_pl(layout);
  const unsigned char *sh = pick_sh(layout);

  if(s->mod.ctrl) {
    unsigned char b = pl[key];
    if(b >= 'a' && b <= 'z') {
      if(!dry)
        *out = (unsigned char)(b - 'a' + 1);
      return true;
    }
    if(b >= 'A' && b <= 'Z') {
      if(!dry)
        *out = (unsigned char)(b - 'A' + 1);
      return true;
    }
    return false;
  }

  if(layout == KBD_LAYOUT_FR && (s->lalt_dn || s->ralt_dn) && fr_alt[key]) {
    return emit_user_cp(fr_alt[key], out, dry);
  }

  bool eff_shift = s->mod.shift;
  if(layout == KBD_LAYOUT_FR) {
    if(fr_caps_scan(key))
      eff_shift ^= s->mod.capslock;
  } else if(us_caps_scan(key)) {
    eff_shift ^= s->mod.capslock;
  }

  unsigned char c = eff_shift ? sh[key] : pl[key];
  if(!c)
    return false;
  return emit_user_cp(c, out, dry);
}

static bool kbd_peek_would_emit(const u8 *buf, u32 n, kbd_ev_ctx_t st)
{
  unsigned char dummy;
  for(u32 i = 0; i < n; i++) {
    if(process_raw_ctx(buf[i], &st, &dummy, true))
      return true;
  }
  return false;
}

/**
 * Pop one translated byte from the keyboard path. When @a block is false,
 * drains the raw queue once without sleeping.
 */
static bool kbd_pop_byte(unsigned char *out, bool block)
{
  for(;;) {
    unsigned char pb;
    if(out_pend_take(&pb)) {
      *out = pb;
      return true;
    }
    if(!block) {
      while(keyboard_raw_available()) {
        u8 raw = keyboard_raw_pop();
        if(process_raw_ctx(raw, &g_kbd, out, false))
          return true;
      }
      return false;
    }
    while(!keyboard_raw_available()) {
      cpu_enable_interrupts();
      __asm__ volatile("hlt");
      cpu_disable_interrupts();
      /* Yield to other processes (e.g. pipe relay parent) that became READY
       * while we were waiting. proc_switch marks us READY so we will be
       * rescheduled once the other process yields or blocks. */
      proc_schedule();
    }
    u8 raw = keyboard_raw_pop();
    if(process_raw_ctx(raw, &g_kbd, out, false))
      return true;
  }
}

static void tty_echo_byte(unsigned char c, bool echo_on)
{
  if(!echo_on)
    return;
  if(c == '\n' || c == '\r') {
    console_putchar('\r');
    console_putchar('\n');
  } else if(c == '\t') {
    console_putchar('\t');
  } else if(c >= 0x20 && c < 0x7f) {
    console_putchar((char)c);
  }
}

static void tty_echo_erase(bool echo_on)
{
  if(!echo_on)
    return;
  console_putchar('\b');
  console_putchar(' ');
  console_putchar('\b');
}

/**
 * @brief Trim one UTF-8 codepoint from the tail of @p buf.
 *
 * The kbd line discipline appends incoming bytes one at a time, so a
 * Latin-1 character like é lands as the pair 0xC3 0xA9. Plain backspace
 * would erase a single byte and leave a dangling lead, corrupting the
 * line. Walk backwards through any continuation bytes (10xxxxxx) and
 * then over the lead byte itself.
 *
 * @return Updated length after the trim (0 when @p len was already 0).
 */
static u32 utf8_trim_one(const char *buf, u32 len)
{
  while(len > 0 && ((unsigned char)buf[len - 1] & 0xc0u) == 0x80u)
    len--;
  if(len > 0)
    len--;
  return len;
}

static u64 kbd_deliver_ready(proc_t *p, char *buf, u64 count)
{
  u64 to_copy = count;
  if(to_copy > p->kbd_ready_len)
    to_copy = p->kbd_ready_len;
  if(to_copy == 0)
    return 0;
  kmemcpy(buf, p->kbd_ready, to_copy);
  if(to_copy < p->kbd_ready_len) {
    u32 rest = p->kbd_ready_len - (u32)to_copy;
    kmemcpy(
        p->kbd_ready, p->kbd_ready + to_copy, rest
    ); /* src > dst, no overlap */
    p->kbd_ready_len = rest;
  } else
    p->kbd_ready_len = 0;
  return to_copy;
}

u64 kbd_read_for_process(proc_t *p, char *buf, u64 count)
{
  u64 filled = 0;

  if(!p || count == 0)
    return 0;

  k_termios_t  *t       = &p->termios;
  u32           lflag   = t->c_lflag;
  bool          icanon  = (lflag & KTERM_ICANON) != 0;
  bool          echo_on = (lflag & KTERM_ECHO) != 0;
  u8            vmin    = t->c_cc[KTERM_VMIN];
  u8            vtime   = t->c_cc[KTERM_VTIME];
  unsigned char verase  = t->c_cc[KTERM_VERASE];
  unsigned char vkill   = t->c_cc[KTERM_VKILL];
  unsigned char veof    = t->c_cc[KTERM_VEOF];

  if(icanon) {
    u64 d = kbd_deliver_ready(p, buf, count);
    if(d)
      return d;

    for(;;) {
      unsigned char c;
      kbd_pop_byte(&c, true);

      if(c == verase || c == '\b') {
        if(p->kbd_edit_len > 0) {
          p->kbd_edit_len = utf8_trim_one(p->kbd_edit, p->kbd_edit_len);
          tty_echo_erase(echo_on);
        }
        continue;
      }

      if(c == vkill) {
        while(p->kbd_edit_len > 0) {
          p->kbd_edit_len = utf8_trim_one(p->kbd_edit, p->kbd_edit_len);
          tty_echo_erase(echo_on);
        }
        continue;
      }

      if(c == '\r' || c == '\n') {
        tty_echo_byte(c, echo_on);
        if(p->kbd_edit_len >= PROC_KBD_LINE_CAP)
          p->kbd_edit_len = PROC_KBD_LINE_CAP - 1;
        kmemcpy(p->kbd_ready, p->kbd_edit, p->kbd_edit_len);
        p->kbd_ready_len              = p->kbd_edit_len + 1;
        p->kbd_ready[p->kbd_edit_len] = '\n';
        p->kbd_edit_len               = 0;
        return kbd_deliver_ready(p, buf, count);
      }

      if(c == veof) {
        if(p->kbd_edit_len == 0)
          return 0;
        kmemcpy(p->kbd_ready, p->kbd_edit, p->kbd_edit_len);
        p->kbd_ready_len = p->kbd_edit_len;
        p->kbd_edit_len  = 0;
        return kbd_deliver_ready(p, buf, count);
      }

      if(p->kbd_edit_len < PROC_KBD_LINE_CAP - 1u) {
        p->kbd_edit[p->kbd_edit_len++] = (char)c;
        tty_echo_byte(c, echo_on);
      }
    }
  }

  /* Non-canonical */
  if(vmin == 0 && vtime == 0) {
    while(filled < count && kbd_pop_byte((unsigned char *)&buf[filled], false))
      filled++;
    return filled;
  }

  if(vmin == 0) {
    unsigned char oc;
    kbd_pop_byte(&oc, true);
    buf[0] = (char)oc;
    return 1;
  }

  u64 need = (u64)vmin;
  if(need > count)
    need = count;

  while(filled < need) {
    unsigned char oc;
    kbd_pop_byte(&oc, true);
    buf[filled++] = (char)oc;
  }
  while(filled < count) {
    unsigned char oc;
    if(!kbd_pop_byte(&oc, false))
      break;
    buf[filled++] = (char)oc;
  }
  return filled;
}

bool kbd_select_read_ready(const proc_t *p)
{
  if(!p)
    return kbd_raw_pending();
  /* ICANON: line complete is the ideal condition, but our discipline runs
   * inside read() rather than at IRQ time.  Fall through to kbd_raw_pending()
   * so select() does not deadlock when the ready buffer is still empty. */
  if((p->termios.c_lflag & KTERM_ICANON) != 0)
    return p->kbd_ready_len > 0 || kbd_raw_pending();
  return kbd_raw_pending();
}

u64 kbd_read_translated(char *buf, u64 count)
{
  proc_t *p = proc_current();
  if(p)
    return kbd_read_for_process(p, buf, count);

  static proc_t boot_stub;
  static int    boot_inited;
  if(!boot_inited) {
    ktermios_init_default(&boot_stub.termios);
    boot_stub.kbd_edit_len  = 0;
    boot_stub.kbd_ready_len = 0;
    boot_inited             = 1;
  }
  return kbd_read_for_process(&boot_stub, buf, count);
}

/**
 * @brief True when fd 0 read(2) can return without blocking for a new IRQ
 *        (pending CSI/user byte), not merely when key-up scancodes sit in the
 *        raw queue — otherwise select(2) would wake and read would block.
 */
bool kbd_raw_pending(void)
{
  if(out_pend_r != out_pend_w)
    return true;
  if(!keyboard_raw_available())
    return false;
  u8  peek[KBD_RAW_PEEK_MAX];
  u32 n = keyboard_raw_peek(peek, (u32)sizeof(peek));
  if(n == 0)
    return false;
  return kbd_peek_would_emit(peek, n, g_kbd);
}
