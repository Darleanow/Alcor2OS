/**
 * @file user/apps/ncurses-hello/main.c
 * @brief Interactive ncurses demo for Alcor2.
 *
 * Exercises the shell's framebuffer terminal: color pairs, attribute flags,
 * box borders (DEC line drawing), and the keyname/keycode mapping. Useful as
 * a smoke test for the relayed-stdout path between user apps and fb_tty.
 *
 * Navigation: arrow keys or hjkl in the menu, Enter to open a sub-screen, q
 * to go back or quit.
 */

#include <curses.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CP_NORMAL 1
#define CP_HEADER 2
#define CP_HILITE 3
#define CP_WARN   4

typedef enum
{
  SCR_MENU,
  SCR_COLORS,
  SCR_ATTRS,
  SCR_INPUT,
  SCR_QUIT
} screen_t;

static const char *const menu_labels[] = {
    "Color pairs",
    "Text attributes",
    "Keyboard input",
    "Quit",
};

#define N_MENU_ITEMS ((int)(sizeof menu_labels / sizeof menu_labels[0]))

/**
 * @brief Fill the header bar with @p title and the app name.
 */
static void draw_header(WINDOW *w, int cols, const char *title)
{
  wattron(w, COLOR_PAIR(CP_HEADER) | A_BOLD);
  mvwhline(w, 0, 0, ' ', cols);
  mvwprintw(w, 0, 2, " Alcor2 ncurses  |  %s", title);
  wattroff(w, COLOR_PAIR(CP_HEADER) | A_BOLD);
  wrefresh(w);
}

/**
 * @brief Fill the footer bar with @p hint.
 */
static void draw_footer(WINDOW *w, int cols, const char *hint)
{
  wattron(w, COLOR_PAIR(CP_HEADER));
  mvwhline(w, 0, 0, ' ', cols);
  mvwprintw(w, 0, 2, " %s", hint);
  wattroff(w, COLOR_PAIR(CP_HEADER));
  wrefresh(w);
}

/**
 * @brief Show the main menu and return the screen the user picked.
 */
static screen_t
    screen_menu(WINDOW *hdr, WINDOW *body, WINDOW *ftr, int rows, int cols)
{
  int brows = rows - 2;
  int sel   = 0;

  draw_header(hdr, cols, "Main Menu");
  draw_footer(ftr, cols, "^/v  j/k  move     Enter  open     q  quit");

  for(;;) {
    werase(body);
    box(body, 0, 0);

    int item_w = 26;
    int item_x = (cols - item_w) / 2;
    int item_y = (brows - N_MENU_ITEMS * 2) / 2;

    for(int i = 0; i < N_MENU_ITEMS; i++) {
      attr_t a = (i == sel) ? (COLOR_PAIR(CP_HILITE) | A_BOLD)
                            : (attr_t)COLOR_PAIR(CP_NORMAL);
      wattron(body, a);
      mvwprintw(body, item_y + i * 2, item_x, "  %-22s  ", menu_labels[i]);
      wattroff(body, a);
    }
    wrefresh(body);

    switch(wgetch(body)) {
    case KEY_UP:
    case 'k':
      if(sel > 0)
        sel--;
      break;
    case KEY_DOWN:
    case 'j':
      if(sel < N_MENU_ITEMS - 1)
        sel++;
      break;
    case '\n':
    case KEY_ENTER:
      return (screen_t)(SCR_COLORS + sel);
    case 'q':
    case 'Q':
      return SCR_QUIT;
    }
  }
}

/**
 * @brief Render every foreground color on black and white backgrounds.
 */
static void
    screen_colors(WINDOW *hdr, WINDOW *body, WINDOW *ftr, int rows, int cols)
{
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
  const int n = (int)(sizeof colors / sizeof colors[0]);

  (void)rows;
  draw_header(hdr, cols, "Color Pairs");
  draw_footer(ftr, cols, "any key  back");

  werase(body);
  box(body, 0, 0);

  mvwprintw(body, 2, 4, "Foreground on BLACK:");
  for(int i = 0; i < n; i++) {
    short id = (short)(10 + i);
    init_pair(id, colors[i].fg, COLOR_BLACK);
    wattron(body, COLOR_PAIR(id));
    mvwprintw(body, 4 + i, 6, "  %s  Hello, Alcor2!", colors[i].name);
    wattroff(body, COLOR_PAIR(id));
  }

  mvwprintw(body, 4 + n + 1, 4, "Foreground on WHITE:");
  for(int i = 0; i < n; i++) {
    short id = (short)(20 + i);
    init_pair(id, colors[i].fg, COLOR_WHITE);
    wattron(body, COLOR_PAIR(id));
    mvwprintw(body, 4 + n + 3 + i, 6, "  %s  Hello, Alcor2!", colors[i].name);
    wattroff(body, COLOR_PAIR(id));
  }

  wrefresh(body);
  wgetch(body);
}

/**
 * @brief Render each ncurses attribute flag on its own row.
 */
static void
    screen_attrs(WINDOW *hdr, WINDOW *body, WINDOW *ftr, int rows, int cols)
{
  static const struct
  {
    attr_t      attr;
    const char *name;
  } attrs[] = {
      {A_NORMAL,    "A_NORMAL    - plain text"             },
      {A_BOLD,      "A_BOLD      - bold / bright"          },
      {A_DIM,       "A_DIM       - half-bright"            },
      {A_UNDERLINE, "A_UNDERLINE - underline"              },
      {A_REVERSE,   "A_REVERSE   - video reverse"          },
      {A_BLINK,     "A_BLINK     - blinking"               },
      {A_STANDOUT,  "A_STANDOUT  - terminal best highlight"},
  };
  const int n = (int)(sizeof attrs / sizeof attrs[0]);

  (void)rows;
  draw_header(hdr, cols, "Text Attributes");
  draw_footer(ftr, cols, "any key  back");

  werase(body);
  box(body, 0, 0);

  mvwprintw(
      body, 2, 4, "ncurses attribute flags (rendering depends on terminal):"
  );

  for(int i = 0; i < n; i++) {
    wattron(body, attrs[i].attr);
    mvwprintw(body, 4 + i * 2, 6, "  %s  ", attrs[i].name);
    wattroff(body, attrs[i].attr);
  }

  wattron(body, A_BOLD | A_REVERSE);
  mvwprintw(body, 4 + n * 2 + 1, 6, "  A_BOLD | A_REVERSE  combined  ");
  wattroff(body, A_BOLD | A_REVERSE);

  wrefresh(body);
  wgetch(body);
}

/**
 * @brief Running UTF-8 byte assembler for ::screen_input.
 *
 * The kernel keyboard layer emits UTF-8 (so é arrives as 0xC3 0xA9, à as
 * 0xC3 0xA0, …). Narrow ncurses returns one byte at a time and labels every
 * high-bit byte as @c "M-x", which is useless for the demo. We keep our own
 * UTF-8 state machine: lead byte starts a sequence, continuation bytes feed
 * into it, and on completion we surface the codepoint plus its raw bytes.
 */
typedef struct
{
  unsigned remaining;   /**< Continuation bytes still expected (1..3). */
  uint32_t codepoint;   /**< Partial codepoint accumulated so far. */
  unsigned char raw[4]; /**< Bytes of the in-flight sequence, for display. */
  unsigned      n_raw;
} utf8_acc_t;

/**
 * @brief Feed one byte to @p acc; return decoded codepoint when complete.
 *
 * @return Codepoint on completion, @c 0 while a sequence is still in flight,
 *         or @c (uint32_t)-1 when @p byte is an invalid continuation.
 */
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

/**
 * @brief Format the raw bytes of @p acc as @c "0xAA 0xBB ..." into @p buf.
 */
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

/**
 * @brief Log every key press as keycode + name (UTF-8 aware) until 'q' is hit.
 *
 * ASCII and ncurses key codes (>= 0x100, e.g. KEY_UP) print straight away.
 * Bytes in a UTF-8 multi-byte sequence are buffered and only logged once the
 * whole codepoint is in hand, so é shows up as "é (U+00E9, 0xc3 0xa9)" rather
 * than two consecutive @c "M-X" lines.
 */
static void
    screen_input(WINDOW *hdr, WINDOW *body, WINDOW *ftr, int rows, int cols)
{
  int        brows = rows - 2;
  int        log_y = 4;
  int        max_y = brows - 2;
  utf8_acc_t acc   = {0};

  draw_header(hdr, cols, "Keyboard Input");
  draw_footer(ftr, cols, "press keys to see their names   q  back");

  werase(body);
  box(body, 0, 0);
  mvwprintw(body, 2, 4, "Key log (codepoint / raw bytes):");
  wrefresh(body);

  for(;;) {
    int ch = wgetch(body);

    if(ch == 'q' || ch == 'Q')
      return;

    /* Routing:
     *   - bytes >= 0x100 are ncurses KEY_* codes (arrows, function keys);
     *   - bytes < 0x80 are plain ASCII/control — keyname() already prints
     *     them nicely as "a" / "^C" / "^[";
     *   - bytes 0x80..0xff are UTF-8 fragments — assemble them ourselves
     *     so an é shows up as one line, not two anonymous M-X codes. */
    char line[64];
    if(ch >= 0x100) {
      const char *name = keyname(ch);
      snprintf(line, sizeof line, "%-12s  code %d", name ? name : "?", ch);
    } else if(ch < 0x80) {
      const char *name = keyname(ch);
      snprintf(line, sizeof line, "%-12s  0x%02x", name ? name : "?", ch);
      acc.remaining = 0;
    } else {
      uint32_t cp = utf8_feed(&acc, (unsigned char)ch);
      if(cp == 0)
        continue;
      if(cp == (uint32_t)-1) {
        snprintf(line, sizeof line, "invalid UTF-8 byte 0x%02x", ch);
      } else {
        char     raw[24];
        char     ch_buf[5];
        unsigned cn = 0;
        utf8_format_raw(&acc, raw, sizeof raw);
        for(unsigned i = 0; i < acc.n_raw && cn < sizeof ch_buf - 1; i++)
          ch_buf[cn++] = (char)acc.raw[i];
        ch_buf[cn] = '\0';
        snprintf(line, sizeof line, "%-4s U+%04X  %s", ch_buf, cp, raw);
      }
    }

    if(log_y >= max_y) {
      for(int y = 4; y < max_y; y++)
        mvwhline(body, y, 1, ' ', cols - 2);
      box(body, 0, 0);
      mvwprintw(body, 2, 4, "Key log (codepoint / raw bytes):");
      log_y = 4;
    }

    wattron(body, COLOR_PAIR(CP_HILITE) | A_BOLD);
    mvwprintw(body, log_y, 6, "  %-*s", cols - 12, line);
    wattroff(body, COLOR_PAIR(CP_HILITE) | A_BOLD);
    log_y++;
    wrefresh(body);
  }
}

/**
 * @brief Initialize the color pairs used by the demo.
 */
static void init_color_pairs(void)
{
  if(!has_colors())
    return;
  start_color();
  init_pair(CP_NORMAL, COLOR_WHITE, COLOR_BLACK);
  init_pair(CP_HEADER, COLOR_BLACK, COLOR_CYAN);
  init_pair(CP_HILITE, COLOR_BLACK, COLOR_GREEN);
  init_pair(CP_WARN, COLOR_RED, COLOR_BLACK);
}

int main(void)
{
  const char *term = getenv("TERM");
  if(!term || !term[0])
    term = "xterm-256color";

  (void)setvbuf(stdout, NULL, _IONBF, 0);

  SCREEN *scr = newterm(term, stdout, stdin);
  if(!scr) {
    (void)fputs(
        "newterm() failed - set TERM and ensure /usr/share/terminfo exists\n"
        "  guest: . /etc/profile\n"
        "  host:  make ncurses && make disk-populate\n",
        stderr
    );
    return 1;
  }
  set_term(scr);
  typeahead(-1);

  raw();
  keypad(stdscr, TRUE);
  noecho();
  curs_set(0);
  init_color_pairs();

  int rows;
  int cols;
  getmaxyx(stdscr, rows, cols);

  WINDOW *hdr  = newwin(1, cols, 0, 0);
  WINDOW *body = newwin(rows - 2, cols, 1, 0);
  WINDOW *ftr  = newwin(1, cols, rows - 1, 0);

  screen_t cur = SCR_MENU;
  while(cur != SCR_QUIT) {
    switch(cur) {
    case SCR_MENU:
      cur = screen_menu(hdr, body, ftr, rows, cols);
      break;
    case SCR_COLORS:
      screen_colors(hdr, body, ftr, rows, cols);
      cur = SCR_MENU;
      break;
    case SCR_ATTRS:
      screen_attrs(hdr, body, ftr, rows, cols);
      cur = SCR_MENU;
      break;
    case SCR_INPUT:
      screen_input(hdr, body, ftr, rows, cols);
      cur = SCR_MENU;
      break;
    case SCR_QUIT:
      break;
    }
  }

  delwin(ftr);
  delwin(body);
  delwin(hdr);
  endwin();
  delscreen(scr);
  return 0;
}
