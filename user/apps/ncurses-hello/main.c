#include <curses.h>
#include <locale.h>
#include <spazer/spazer.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef enum
{
  SCR_MENU,
  SCR_COLORS,
  SCR_ATTRS,
  SCR_INPUT,
  SCR_BLINK,
  SCR_QUIT
} screen_t;

static const char *const g_menu[] = {
    "Color pairs", "Text attributes", "Keyboard input", "Blink demo", "Quit",
};
#define N_ITEMS 5

static void draw_statusbar(const char *hint)
{
  spz_statusbar("Alcor2 v1.0", "ncurses-hello", hint);
}

static screen_t screen_menu(spz_panel_t *p)
{
  spz_panel_redraw(p, "ALCOR2  DEMO");
  draw_statusbar("Enter  open     q  quit");
  spz_panel_refresh(p);

  int mx = (COLS - 21) / 2;
  if(mx < 1)
    mx = 1;

  int sel = spz_menu(3, mx, g_menu, N_ITEMS, NULL);
  if(sel < 0 || sel == N_ITEMS - 1)
    return SCR_QUIT;
  return (screen_t)(SCR_COLORS + sel);
}

static void screen_colors(spz_panel_t *p)
{
  WINDOW *w = p->body;
  spz_panel_redraw(p, "Color Pairs");
  draw_statusbar("any key  back");

  static const struct
  {
    short       fg;
    const char *name;
  } colors[] = {
      {COLOR_WHITE,   "WHITE  "},
      {COLOR_RED,     "RED    "},
      {COLOR_GREEN,   "GREEN  "},
      {COLOR_YELLOW,  "YELLOW "},
      {COLOR_BLUE,    "BLUE   "},
      {COLOR_MAGENTA, "MAGENTA"},
      {COLOR_CYAN,    "CYAN   "},
  };

  spz_label(w, 1, 2, "Foreground on black:", SPZ_STYLE_DIM);
  for(int i = 0; i < 7; i++) {
    short id = (short)(10 + i);
    init_pair(id, colors[i].fg, COLOR_BLACK);
    wattron(w, COLOR_PAIR(id));
    mvwprintw(w, 3 + i, 4, "  %s  Hello, Alcor2!", colors[i].name);
    wattroff(w, COLOR_PAIR(id));
  }

  spz_label(w, 11, 2, "Foreground on white:", SPZ_STYLE_DIM);
  for(int i = 0; i < 7; i++) {
    short id = (short)(20 + i);
    init_pair(id, colors[i].fg, COLOR_WHITE);
    wattron(w, COLOR_PAIR(id));
    mvwprintw(w, 13 + i, 4, "  %s  Hello, Alcor2!", colors[i].name);
    wattroff(w, COLOR_PAIR(id));
  }

  spz_panel_refresh(p);
  keypad(w, TRUE);
  wgetch(w);
}

static void screen_attrs(spz_panel_t *p)
{
  WINDOW *w = p->body;
  spz_panel_redraw(p, "Text Attributes");
  draw_statusbar("any key  back");

  static const struct
  {
    attr_t      attr;
    const char *name;
  } attrs[] = {
      {A_NORMAL,    "A_NORMAL    plain text"    },
      {A_BOLD,      "A_BOLD      bold / bright" },
      {A_DIM,       "A_DIM       half-bright"   },
      {A_UNDERLINE, "A_UNDERLINE underline"     },
      {A_REVERSE,   "A_REVERSE   video reverse" },
      {A_STANDOUT,  "A_STANDOUT  best highlight"},
  };
  const int n = (int)(sizeof attrs / sizeof attrs[0]);

  spz_label(w, 1, 2, "Standard ncurses attribute flags:", SPZ_STYLE_DIM);

  for(int i = 0; i < n; i++) {
    wattron(w, COLOR_PAIR(SPZ_PAIR_TEXT) | attrs[i].attr);
    mvwprintw(w, 3 + i * 2, 4, "  %s  ", attrs[i].name);
    wattroff(w, COLOR_PAIR(SPZ_PAIR_TEXT) | attrs[i].attr);
  }

  wattron(w, A_BOLD | A_REVERSE | COLOR_PAIR(SPZ_PAIR_ACCENT));
  mvwprintw(w, 3 + n * 2 + 1, 4, "  A_BOLD | A_REVERSE combined  ");
  wattroff(w, A_BOLD | A_REVERSE | COLOR_PAIR(SPZ_PAIR_ACCENT));

  spz_panel_refresh(p);
  keypad(w, TRUE);
  wgetch(w);
}

typedef struct
{
  unsigned      remaining;
  uint32_t      codepoint;
  unsigned char raw[4];
  unsigned      n_raw;
} utf8_acc_t;

static uint32_t utf8_feed(utf8_acc_t *acc, unsigned char byte)
{
  if(acc->remaining == 0) {
    acc->raw[0] = byte;
    acc->n_raw  = 1;
    if(byte < 0x80u) {
      acc->codepoint = byte;
      return byte;
    }
    if((byte & 0xe0u) == 0xc0u) {
      acc->codepoint = byte & 0x1fu;
      acc->remaining = 1;
      return 0;
    }
    if((byte & 0xf0u) == 0xe0u) {
      acc->codepoint = byte & 0x0fu;
      acc->remaining = 2;
      return 0;
    }
    if((byte & 0xf8u) == 0xf0u) {
      acc->codepoint = byte & 0x07u;
      acc->remaining = 3;
      return 0;
    }
    return (uint32_t)-1;
  }
  if((byte & 0xc0u) != 0x80u) {
    acc->remaining = 0;
    return (uint32_t)-1;
  }
  if(acc->n_raw < sizeof acc->raw)
    acc->raw[acc->n_raw++] = byte;
  acc->codepoint = (acc->codepoint << 6) | (byte & 0x3fu);
  acc->remaining--;
  return acc->remaining ? 0 : acc->codepoint;
}

static void utf8_format_raw(const utf8_acc_t *acc, char *buf, size_t cap)
{
  static const char hex[] = "0123456789abcdef";
  size_t            w     = 0;
  for(unsigned i = 0; i < acc->n_raw && w + 6 < cap; i++) {
    if(i)
      buf[w++] = ' ';
    buf[w++] = '0';
    buf[w++] = 'x';
    buf[w++] = hex[(acc->raw[i] >> 4) & 0xfu];
    buf[w++] = hex[acc->raw[i] & 0xfu];
  }
  buf[w] = '\0';
}

static void screen_input(spz_panel_t *p)
{
  WINDOW *w = p->body;
  int     brows, bcols;
  getmaxyx(w, brows, bcols);

  int        log_y = 4;
  int        max_y = brows - 2;
  utf8_acc_t acc   = {0};

  spz_panel_redraw(p, "Keyboard Input");
  draw_statusbar("press keys to log them   q  back");
  spz_label(w, 2, 2, "Key log (codepoint / raw bytes):", SPZ_STYLE_DIM);
  spz_panel_refresh(p);
  keypad(w, TRUE);

  for(;;) {
    int ch = wgetch(w);
    if(ch == 'q' || ch == 'Q')
      return;

    char line[64];
    if(ch >= 0x100) {
      const char *name = keyname(ch);
      (void)snprintf(
          line, sizeof line, "%-12s  code %d", name ? name : "?", ch
      );
    } else if(ch < 0x80) {
      const char *name = keyname(ch);
      (void)snprintf(line, sizeof line, "%-12s  0x%02x", name ? name : "?", ch);
      acc.remaining = 0;
    } else {
      uint32_t cp = utf8_feed(&acc, (unsigned char)ch);
      if(cp == 0)
        continue;
      if(cp == (uint32_t)-1) {
        (void)snprintf(line, sizeof line, "invalid UTF-8 byte 0x%02x", ch);
      } else {
        char     raw[24];
        char     ch_buf[5];
        unsigned cn = 0;
        utf8_format_raw(&acc, raw, sizeof raw);
        for(unsigned i = 0; i < acc.n_raw && cn < sizeof ch_buf - 1; i++)
          ch_buf[cn++] = (char)acc.raw[i];
        ch_buf[cn] = '\0';
        (void)snprintf(line, sizeof line, "%-4s U+%04X  %s", ch_buf, cp, raw);
      }
    }

    if(log_y >= max_y) {
      spz_clear_region(w, 4, 1, max_y - 4, bcols - 2);
      log_y = 4;
    }

    wattron(w, COLOR_PAIR(SPZ_PAIR_ACCENT) | A_BOLD);
    mvwprintw(w, log_y, 4, "  %-*s", bcols - 8, line);
    wattroff(w, COLOR_PAIR(SPZ_PAIR_ACCENT) | A_BOLD);
    log_y++;
    spz_panel_refresh(p);
  }
}

static void screen_blink(spz_panel_t *p)
{
  WINDOW *w = p->body;
  spz_panel_redraw(p, "Blink Demo");
  draw_statusbar("any key  back");

  spz_label(w, 1, 2, "Cell blink via A_BLINK (SGR 5):", SPZ_STYLE_DIM);

  spz_label(w, 3, 4, "Static text  -  never blinks.", SPZ_STYLE_NORMAL);

  wattron(w, A_BLINK | COLOR_PAIR(SPZ_PAIR_ACCENT) | A_BOLD);
  mvwaddstr(w, 5, 4, "  Neon yellow - blinking!  ");
  wattroff(w, A_BLINK | COLOR_PAIR(SPZ_PAIR_ACCENT) | A_BOLD);

  wattron(w, A_BLINK | COLOR_PAIR(SPZ_PAIR_TITLE) | A_BOLD);
  mvwaddstr(w, 7, 4, "  Mauve title - blinking!  ");
  wattroff(w, A_BLINK | COLOR_PAIR(SPZ_PAIR_TITLE) | A_BOLD);

  wattron(w, A_BLINK | COLOR_PAIR(SPZ_PAIR_SELECT));
  mvwaddstr(w, 9, 4, "  Inverted select row - blinking!  ");
  wattroff(w, A_BLINK | COLOR_PAIR(SPZ_PAIR_SELECT));

  spz_label(w, 12, 2, "Blink rate: ~0.5 Hz (1 s period)", SPZ_STYLE_DIM);
  spz_label(w, 13, 2, "SGR sequence: ESC[5m ... ESC[25m", SPZ_STYLE_DIM);

  spz_panel_refresh(p);
  keypad(w, TRUE);
  wgetch(w);
}

int main(void)
{
  const char *term = getenv("TERM");
  if(!term || !term[0])
    term = "xterm-256color";

  (void)setlocale(LC_ALL, "C.UTF-8");
  (void)setvbuf(stdout, NULL, _IONBF, 0);

  SCREEN *scr = newterm(term, stdout, stdin);
  if(!scr) {
    (void)fputs(
        "newterm() failed — set TERM and ensure /usr/share/terminfo exists\n",
        stderr
    );
    return 1;
  }
  set_term(scr);
  typeahead(-1);

  raw();
  noecho();
  curs_set(0);
  spz_init();

  int rows, cols;
  getmaxyx(stdscr, rows, cols);

  spz_panel_t *p =
      spz_panel_new(0, 0, rows - 1, cols, "ALCOR2  DEMO", SPZ_BORDER_DOUBLE);
  if(!p) {
    endwin();
    delscreen(scr);
    return 1;
  }
  keypad(p->body, TRUE);

  screen_t cur = SCR_MENU;
  while(cur != SCR_QUIT) {
    switch(cur) {
    case SCR_MENU:
      cur = screen_menu(p);
      break;
    case SCR_COLORS:
      screen_colors(p);
      cur = SCR_MENU;
      break;
    case SCR_ATTRS:
      screen_attrs(p);
      cur = SCR_MENU;
      break;
    case SCR_INPUT:
      screen_input(p);
      cur = SCR_MENU;
      break;
    case SCR_BLINK:
      screen_blink(p);
      cur = SCR_MENU;
      break;
    case SCR_QUIT:
      break;
    }
  }

  spz_panel_del(p);
  endwin();
  delscreen(scr);
  return 0;
}
