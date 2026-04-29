/**
 * @file src/kernel/input/kbd_layout.c
 * @brief PS/2 set 1 scancodes to bytes: US or FR AZERTY (+ AltGr) and CSI arrow keys.
 */

#include <alcor2/arch/cpu.h>
#include <alcor2/kbd.h>
#include <alcor2/kstdlib.h>
#include <alcor2/drivers/keyboard.h>

#define LAT_mu   '\xb5'
#define LAT_deg  '\xb0'
#define LAT_sect '\xa7'
#define LAT_ucce '\xe7'
#define LAT_ucee '\xe8'
#define LAT_uca0 '\xe0'
#define LAT_uce9 '\xe9'
#define LAT_ucf9 '\xf9'
#define LAT_diae '\xa8'
#define LAT_poun '\xa3'
#define LAT_curr '\xa4'

#define OUT_PEND_SZ 256
static unsigned char out_pend_buf[OUT_PEND_SZ];
static u32           out_pend_w;
static u32           out_pend_r;

static bool out_pend_take(unsigned char *c)
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
  out_pend_w                = next;
  return true;
}

static void pend_csi(char tail)
{
  out_pend_push(0x1b);
  out_pend_push('[');
  out_pend_push((unsigned char)tail);
}

static const unsigned char us_pl[128] = {
    0,   0,    '1',  '2', '3',  '4', '5', '6', '7', '8', '9', '0', '-',
    '=', '\b', '\t', 'q', 'w',  'e', 'r', 't', 'y', 'u', 'i', 'o', 'p',
    '[', ']',  '\n', 0,   'a',  's', 'd', 'f', 'g', 'h', 'j', 'k', 'l',
    ';', '\'', '`',  0,   '\\', 'z', 'x', 'c', 'v', 'b', 'n', 'm', ',',
    '.', '/',  0,    '*', 0,    ' ', 0,   0,   0,   0,   0,   0,   0,
    0,   0,    0,    0,   0,    0,   '7', '8', '9', '-', '4', '5', '6',
    '+', '1',  '2',  '3', '0',  '.', 0,   0,   0,   0,   0,   0,   0,
    0,   0,    0,    0,   0,    0,   0,   0,   0,   0,   0,   0,   0,
    0,   0,    0,    0,   0,    0,   0,   0,   0,   0,   0,   0,   0,
    0,   0,    0,    0,   0,    0,   0,   0,   0,   0,   0};

static const unsigned char us_sh[128] = {
    0,   0,    '!',  '@', '#', '$', '%', '^', '&', '*', '(', ')', '_',
    '+', '\b', '\t', 'Q', 'W', 'E', 'R', 'T', 'Y', 'U', 'I', 'O', 'P',
    '{', '}',  '\n', 0,   'A', 'S', 'D', 'F', 'G', 'H', 'J', 'K', 'L',
    ':', '"',  '~',  0,   '|', 'Z', 'X', 'C', 'V', 'B', 'N', 'M', '<',
    '>', '?',  0,    '*', 0,   ' ', 0,   0,   0,   0,   0,   0,   0,
    0,   0,    0,    0,   0,   0,   '7', '8', '9', '-', '4', '5', '6',
    '+', '1',  '2',  '3', '0', '.', 0,   0,   0,   0,   0,   0,   0,
    0,   0,    0,    0,   0,   0,   0,   0,   0,   0,   0,   0,   0,
    0,   0,    0,    0,   0,   0,   0,   0,   0,   0,   0,   0,   0,
    0,   0,    0,    0,   0,   0,   0,   0,   0,   0,   0};

static unsigned char fr_pl[128];
static unsigned char fr_sh[128];
static bool          fr_ready;

static const unsigned char fr_alt[128] = {
    [0x03] = '~',
    [0x04] = '#',
    [0x05] = '{',
    [0x06] = '[',
    [0x07] = '|',
    [0x08] = '`',
    [0x09] = '\\',
    [0x0a] = '^',
    [0x0b] = '@',
    [0x0c] = ']',
    [0x0d] = '}',
    [0x1b] = LAT_curr,
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
  fr_pl[0x56] = '>';
  fr_sh[0x56] = '<';

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
  if((key >= 0x10 && key <= 0x19) || (key >= 0x1e && key <= 0x28) || (key >= 0x2c && key <= 0x31))
    return true;
  return false;
}

static bool us_caps_scan(u8 key)
{
  unsigned char p = us_pl[key];
  return p >= 'a' && p <= 'z';
}

static kbd_layout_t layout = KBD_LAYOUT_US;
static bool         pend_e0;
static bool         lalt_dn;
static bool         ralt_dn;
static key_state_t  mod = {0};

void kbd_set_layout(kbd_layout_t lay)
{
  if((unsigned)lay >= KBD_LAYOUT_COUNT)
    lay = KBD_LAYOUT_US;
  layout       = lay;
  out_pend_w   = out_pend_r = 0;
  pend_e0      = false;
  lalt_dn      = false;
  ralt_dn      = false;
}

kbd_layout_t kbd_get_layout(void)
{
  return layout;
}

/**
 * @brief Handle one raw PS/2 byte from the keyboard ISR buffer.
 * @return true when *out should be delivered to fd 0 reader.
 */
static bool process_raw(u8 raw, unsigned char *out)
{
  if(raw == 0xe0) {
    pend_e0 = true;
    return false;
  }

  if(pend_e0) {
    pend_e0       = false;
    bool released = (raw & KEY_RELEASE) != 0;
    u8   ext      = raw & (u8)~KEY_RELEASE;

    if(ext == 0x38) {
      ralt_dn = !released;
      mod.alt = (lalt_dn || ralt_dn);
      return false;
    }

    if(released)
      return false;

    switch(ext) {
    case 0x48:
      pend_csi('A');
      break;
    case 0x50:
      pend_csi('B');
      break;
    case 0x4b:
      pend_csi('D');
      break;
    case 0x4d:
      pend_csi('C');
      break;
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
    mod.shift = !released;
    return false;
  case KEY_LCTRL:
    mod.ctrl = !released;
    return false;
  case KEY_LALT:
    lalt_dn = !released;
    mod.alt = (lalt_dn || ralt_dn);
    return false;
  case KEY_CAPSLOCK:
    if(!released)
      mod.capslock = !mod.capslock;
    return false;
  default:
    break;
  }

  if(released)
    return false;

  const unsigned char *pl = pick_pl(layout);
  const unsigned char *sh = pick_sh(layout);

  if(mod.ctrl) {
    unsigned char b = pl[key];
    if(b >= 'a' && b <= 'z') {
      *out = (unsigned char)(b - 'a' + 1);
      return true;
    }
    if(b >= 'A' && b <= 'Z') {
      *out = (unsigned char)(b - 'A' + 1);
      return true;
    }
    /* Ctrl+letter only; accented keys have no Ctrl sequence here */
    return false;
  }

  if(layout == KBD_LAYOUT_FR && (lalt_dn || ralt_dn) && fr_alt[key]) {
    *out = fr_alt[key];
    return true;
  }

  bool eff_shift = mod.shift;
  if(layout == KBD_LAYOUT_FR) {
    if(fr_caps_scan(key))
      eff_shift ^= mod.capslock;
  } else if(us_caps_scan(key)) {
    eff_shift ^= mod.capslock;
  }

  unsigned char c = eff_shift ? sh[key] : pl[key];
  if(!c)
    return false;
  *out = c;
  return true;
}

u64 kbd_read_translated(char *buf, u64 count)
{
  u64 filled = 0;
  while(filled < count) {
    unsigned char pb;
    if(out_pend_take(&pb)) {
      buf[filled++] = (char)pb;
      continue;
    }
    while(!keyboard_raw_available()) {
      cpu_enable_interrupts();
      __asm__ volatile("hlt");
      cpu_disable_interrupts();
    }
    u8            raw = keyboard_raw_pop();
    unsigned char oc;
    if(process_raw(raw, &oc))
      buf[filled++] = (char)oc;
  }
  return filled;
}

bool kbd_raw_pending(void)
{
  return out_pend_r != out_pend_w || keyboard_raw_available();
}
