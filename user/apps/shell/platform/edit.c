/**
 * @file platform/edit.c
 * @brief Single-line interactive editor: cursor motion, history navigation,
 *        tab completion display, UTF-8-aware glyph counting.
 *
 * Reads input from an off-screen ncurses pad (not @c stdscr) so the keypad
 * escape parser is available without triggering an implicit @c wrefresh that
 * would clobber our write()-based output.
 */

#include <curses.h>
#include <shell/atlas.h>
#include <shell/shell.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <theme.h>
#include <unistd.h>

static WINDOW *s_input_pad;

/**
 * @brief Create the off-screen pad and enable extended key + flush flags.
 *
 * Must be called once after @c newterm() and before the first ::sh_read_line.
 *
 * @return 0 on success, -1 if @c newpad failed.
 */
int sh_edit_init(void)
{
  s_input_pad = newpad(1, 256);
  if(!s_input_pad)
    return -1;
  keypad(s_input_pad, TRUE);
  intrflush(s_input_pad, FALSE);
  return 0;
}

/**
 * @brief Repaint the input row in-place: clear, prompt, buffer, cursor.
 *
 * @param prompt       Prompt text (may include ANSI escapes).
 * @param prompt_cols  Visible column width of @p prompt.
 * @param buf          Current input buffer.
 * @param cur_cols     Visible column offset of the cursor inside @p buf.
 */
static void redraw_line(
    const char *prompt, int prompt_cols, const char *buf, int cur_cols
)
{
  char tail[16];
  /* \r → col 0; \033[K → clear to end of line. */
  sh_puts("\r\033[K");
  sh_puts(prompt);
  sh_puts(buf);
  /* CHA — Cursor Horizontal Absolute (1-based). */
  (void)snprintf(tail, sizeof tail, "\r\033[%dC", prompt_cols + cur_cols);
  if(cur_cols + prompt_cols > 0)
    sh_puts(tail);
  else
    sh_puts("\r");
}

/**
 * @brief Count visible UTF-8 glyphs in @p s.
 *
 * @param s  NUL-terminated UTF-8 string.
 * @return Number of code points (continuation bytes are skipped).
 */
static int utf8_cols(const char *s)
{
  int n = 0;
  for(; *s; s++)
    if(((unsigned char)*s & 0xC0u) != 0x80u)
      n++;
  return n;
}

/**
 * @brief Count visible columns in @p s, skipping @c ESC[...m sequences.
 *
 * @param s  NUL-terminated string that may contain ANSI escape sequences.
 * @return Visible column width (escape bytes contribute 0).
 */
static int visible_cols(const char *s)
{
  int n = 0;
  while(*s) {
    if((unsigned char)*s == 0x1b) {
      s++;
      if(*s == '[') {
        s++;
        while(*s && !(*s >= '@' && *s <= '~'))
          s++;
        if(*s)
          s++;
      } else if(*s) {
        s++;
      }
    } else if(((unsigned char)*s & 0xC0u) != 0x80u) {
      n++;
      s++;
    } else {
      s++;
    }
  }
  return n;
}

/**
 * @brief Move @p idx left to the previous UTF-8 character boundary.
 *
 * @param buf  NUL-terminated UTF-8 buffer (only @p idx is read).
 * @param idx  Current byte offset.
 * @return New byte offset; 0 if already at the start.
 */
static int prev_char_boundary(const char *buf, int idx)
{
  if(idx <= 0)
    return 0;
  idx--;
  while(idx > 0 && ((unsigned char)buf[idx] & 0xC0u) == 0x80u)
    idx--;
  return idx;
}

/**
 * @brief Move @p idx right to the next UTF-8 character boundary.
 *
 * @param buf  UTF-8 buffer.
 * @param len  Byte length of @p buf.
 * @param idx  Current byte offset.
 * @return New byte offset; @p len if already at the end.
 */
static int next_char_boundary(const char *buf, int len, int idx)
{
  if(idx >= len)
    return len;
  idx++;
  while(idx < len && ((unsigned char)buf[idx] & 0xC0u) == 0x80u)
    idx++;
  return idx;
}

/**
 * @brief Return the display name for a completion entry (strip dir prefix).
 *
 * @param entry  Full entry string (may contain path separators).
 * @return Pointer into @p entry past the last directory component.
 */
static const char *display_basename(const char *entry)
{
  const char *sl = strrchr(entry, '/');
  if(sl && sl[1] != '\0')
    return sl + 1;
  if(sl && sl[1] == '\0' && sl != entry) {
    const char *prev = sl - 1;
    while(prev > entry && prev[-1] != '/')
      prev--;
    return prev;
  }
  return entry;
}

/**
 * @brief Print completion candidates in coloured columns (double-tab).
 *
 * Layout matches @c ls: column-major order, column width derived from the
 * longest entry, column count from the terminal width.
 *
 * @param comp         Completion result set.
 * @param prompt       Current prompt (reprinted after the listing).
 * @param buf          Current line buffer (reprinted after the listing).
 * @param cur_b        Cursor byte-index within @p buf (for repositioning).
 * @param prompt_cols  Visible column width of @p prompt.
 */
static void list_candidates(
    const comp_result_t *comp, const char *prompt, const char *buf, int cur_b,
    int prompt_cols
)
{
  const char *names[COMP_MAX];
  int         name_lens[COMP_MAX];
  int         dir_flags[COMP_MAX];
  int         max_w = 0;

  for(int i = 0; i < comp->count; i++) {
    names[i]     = display_basename(comp->entries[i]);
    name_lens[i] = (int)strlen(names[i]);
    dir_flags[i] = (name_lens[i] > 0 && names[i][name_lens[i] - 1] == '/');
    if(name_lens[i] > max_w)
      max_w = name_lens[i];
  }

  int col_w      = max_w + 2;
  int cols_avail = COLS > 0 ? COLS : 80;
  int n_cols     = col_w >= cols_avail ? 1 : cols_avail / col_w;
  if(n_cols < 1)
    n_cols = 1;
  int n_rows = (comp->count + n_cols - 1) / n_cols;

  sh_puts("\n");
  for(int row = 0; row < n_rows; row++) {
    for(int col = 0; col < n_cols; col++) {
      int idx = col * n_rows + row;
      if(idx >= comp->count)
        continue;
      int last =
          (col == n_cols - 1) || ((col + 1) * n_rows + row >= comp->count);
      sh_puts(dir_flags[idx] ? THEME_ANSI_PRIMARY_B : THEME_ANSI_SUCCESS_B);
      sh_puts(names[idx]);
      sh_puts(THEME_ANSI_RESET);
      if(!last) {
        int padding = col_w - name_lens[idx];
        for(int p = 0; p < padding; p++)
          sh_puts(" ");
      }
    }
    sh_puts("\n");
  }

  sh_puts(prompt);
  sh_puts(buf);
  int  cur_cols = utf8_cols(buf) - utf8_cols(buf + cur_b);
  char seq[32];
  (void)snprintf(seq, sizeof(seq), "\r\033[%dC", prompt_cols + cur_cols);
  if(prompt_cols + cur_cols > 0)
    sh_puts(seq);
  else
    sh_puts("\r");
}

/**
 * @brief Handle a Tab keypress: complete or list candidates.
 *
 * Single Tab inserts the longest common prefix; double Tab (same as previous
 * keystroke) prints all candidates in coloured columns.
 *
 * @param buf           Line buffer (modified in place on completion).
 * @param len           Pointer to byte length of @p buf content.
 * @param cur_b         Pointer to cursor byte-index within @p buf.
 * @param cap           Total capacity of @p buf.
 * @param last_was_tab  Pointer to the double-tab flag; updated on return.
 * @param prompt        Current prompt string (for redrawing after listing).
 * @param prompt_cols   Visible column width of @p prompt.
 */
static void handle_tab(
    char *buf, int *len, int *cur_b, size_t cap, int *last_was_tab,
    const char *prompt, int prompt_cols
)
{
  int word_start = *cur_b;
  while(word_start > 0 && buf[word_start - 1] != ' ')
    word_start--;
  char prefix[MAX_CMD_LEN];
  int  wlen = *cur_b - word_start;
  if(wlen > 0)
    memcpy(prefix, buf + word_start, wlen);
  prefix[wlen] = '\0';

  bool is_cmd = true;
  for(int i = 0; i < word_start; i++) {
    if(buf[i] != ' ') {
      is_cmd = false;
      break;
    }
  }

  comp_result_t comp;
  sh_complete(prefix, is_cmd, &comp);

  if(comp.count == 0) {
    *last_was_tab = 0;
    return;
  }

  if(*last_was_tab && comp.count > 1) {
    list_candidates(&comp, prompt, buf, *cur_b, prompt_cols);
    *last_was_tab = 0;
    return;
  }

  int clen = (int)strlen(comp.common);
  if(clen > wlen) {
    const char *suffix     = comp.common + wlen;
    int         suffix_len = clen - wlen;
    int need_space = (comp.count == 1 && comp.common[clen - 1] != '/') ? 1 : 0;
    if(*len + suffix_len + need_space < (int)cap - 1) {
      (void)memmove(
          buf + *cur_b + suffix_len + need_space, buf + *cur_b,
          (size_t)*len - (size_t)*cur_b + 1
      );
      memcpy(buf + *cur_b, suffix, suffix_len);
      if(need_space)
        buf[*cur_b + suffix_len] = ' ';
      *len += suffix_len + need_space;
      *cur_b += suffix_len + need_space;
      buf[*len] = '\0';
      redraw_line(
          prompt, prompt_cols, buf, utf8_cols(buf) - utf8_cols(buf + *cur_b)
      );
    }
  }
  *last_was_tab = (comp.count > 1) ? 1 : 0;
}

/**
 * @brief Read one input line with editing, history, and tab completion.
 *
 * Blocks until the user presses Enter, Ctrl-C, Ctrl-D, or Ctrl-L. Cursor
 * motion (←/→, Home, End, Backspace, Delete) and history navigation (↑/↓)
 * are handled in-place via ::redraw_line.
 *
 * @param buf     Caller-owned buffer; receives a NUL-terminated line.
 * @param cap     Capacity of @p buf in bytes.
 * @param prompt  Prompt string (may include ANSI escapes).
 * @return Byte length of the line, @c RL_EOF (Ctrl-D on empty line),
 *         @c RL_INTERRUPT (Ctrl-C), or @c RL_CLEAR (Ctrl-L).
 */
int sh_read_line(char *buf, size_t cap, const char *prompt)
{
  int prompt_cols  = visible_cols(prompt);
  int len          = 0; /* bytes in buf */
  int cur_b        = 0; /* byte index of cursor */
  int hist_view    = sh_hist_count();
  int last_was_tab = 0;
  buf[0]           = '\0';

  sh_puts(prompt);

  for(;;) {
    int c = wgetch(s_input_pad);
    if(c == ERR)
      continue;
    if(c != '\t')
      last_was_tab = 0;

    switch(c) {
    case '\n':
    case '\r':
    case KEY_ENTER:
      sh_puts("\n");
      buf[len] = '\0';
      return len;

    case 0x7f:
    case '\b':
    case KEY_BACKSPACE: {
      if(cur_b == 0)
        break;
      int prev = prev_char_boundary(buf, cur_b);
      (void)memmove(buf + prev, buf + cur_b, (size_t)len - (size_t)cur_b + 1);
      len -= (cur_b - prev);
      cur_b = prev;
      redraw_line(
          prompt, prompt_cols, buf, utf8_cols(buf) - utf8_cols(buf + cur_b)
      );
      break;
    }

    case KEY_DC: {
      if(cur_b >= len)
        break;
      int nx = next_char_boundary(buf, len, cur_b);
      (void)memmove(buf + cur_b, buf + nx, (size_t)len - (size_t)nx + 1);
      len -= (nx - cur_b);
      redraw_line(
          prompt, prompt_cols, buf, utf8_cols(buf) - utf8_cols(buf + cur_b)
      );
      break;
    }

    case KEY_LEFT:
      if(cur_b > 0) {
        cur_b = prev_char_boundary(buf, cur_b);
        redraw_line(
            prompt, prompt_cols, buf, utf8_cols(buf) - utf8_cols(buf + cur_b)
        );
      }
      break;

    case KEY_RIGHT:
      if(cur_b < len) {
        cur_b = next_char_boundary(buf, len, cur_b);
        redraw_line(
            prompt, prompt_cols, buf, utf8_cols(buf) - utf8_cols(buf + cur_b)
        );
      }
      break;

    case KEY_HOME:
      cur_b = 0;
      redraw_line(prompt, prompt_cols, buf, 0);
      break;

    case KEY_END:
      cur_b = len;
      redraw_line(prompt, prompt_cols, buf, utf8_cols(buf));
      break;

    case KEY_UP:
      if(hist_view > 0) {
        hist_view--;
        const char *h = sh_hist_at(hist_view);
        if(h) {
          strncpy(buf, h, cap - 1);
          buf[cap - 1] = '\0';
          len          = (int)strlen(buf);
          cur_b        = len;
          redraw_line(prompt, prompt_cols, buf, utf8_cols(buf));
        }
      }
      break;

    case KEY_DOWN: {
      int hc = sh_hist_count();
      if(hist_view < hc) {
        hist_view++;
        if(hist_view == hc) {
          buf[0] = '\0';
          len    = 0;
          cur_b  = 0;
        } else {
          const char *h = sh_hist_at(hist_view);
          if(h) {
            strncpy(buf, h, cap - 1);
            buf[cap - 1] = '\0';
            len          = (int)strlen(buf);
            cur_b        = len;
          }
        }
        redraw_line(prompt, prompt_cols, buf, utf8_cols(buf));
      }
      break;
    }

    case '\t':
      handle_tab(buf, &len, &cur_b, cap, &last_was_tab, prompt, prompt_cols);
      break;

    case 0x03: /* Ctrl-C */
      sh_puts("^C\n");
      buf[0] = '\0';
      return RL_INTERRUPT;

    case 0x04: /* Ctrl-D */
      if(len == 0)
        return RL_EOF;
      break;

    case 0x0C: /* Ctrl-L */
      return RL_CLEAR;

    default:
      /* Insert byte at cur_b. ASCII printables (0x20..0x7E) and UTF-8 bytes
       * (0x80..0xFF) are kept; everything else is ignored. KEY_* values are
       * all >= KEY_MIN (0x101), which falls outside our printable range. */
      if(c < 0x100 && (c == ' ' || (c >= 0x21 && c <= 0x7e) || c >= 0x80)) {
        if(len + 1 >= (int)cap - 1)
          break;
        (void
        )memmove(buf + cur_b + 1, buf + cur_b, (size_t)len - (size_t)cur_b + 1);
        buf[cur_b++] = (char)c;
        len++;
        redraw_line(
            prompt, prompt_cols, buf, utf8_cols(buf) - utf8_cols(buf + cur_b)
        );
      }
      break;
    }
  }
}
