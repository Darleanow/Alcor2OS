#include <dirent.h>
#include <grendizer.h>
#include <spazer/palette.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

#define MAX_ENTRIES  1024
#define NAME_MAX_LEN 256
#define COL_GAP      2

#define CLR_DIR   SPZ_ANSI_BLUE_B
#define CLR_FILE  SPZ_ANSI_TEXT
#define CLR_EXEC  SPZ_ANSI_GREEN_B
#define CLR_RESET SPZ_ANSI_RESET

typedef struct
{
  char name[NAME_MAX_LEN];
  int  is_dir;
  int  is_exec;
} entry_t;

static entry_t s_entries[MAX_ENTRIES];
static int     s_n;

static int cmp_entry(const void *a, const void *b)
{
  const entry_t *ea = (const entry_t *)a;
  const entry_t *eb = (const entry_t *)b;
  if(ea->is_dir != eb->is_dir)
    return eb->is_dir - ea->is_dir;
  return strcmp(ea->name, eb->name);
}

static int term_cols(void)
{
  struct winsize ws;
  if(ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == 0 && ws.ws_col > 0)
    return (int)ws.ws_col;
  const char *e = getenv("COLUMNS");
  if(e && *e) {
    int v = atoi(e);
    if(v > 0)
      return v;
  }
  return 80;
}

static size_t buf_add(char **buf, size_t *cap, size_t pos, const char *s)
{
  size_t n = strlen(s);
  if(pos + n + 1 > *cap) {
    size_t nc = *cap ? *cap * 2 : 4096;
    while(nc < pos + n + 1) nc *= 2;
    *buf = (char *)realloc(*buf, nc);
    *cap = nc;
  }
  memcpy(*buf + pos, s, n);
  return pos + n;
}

int main(int argc, char *argv[])
{
  int    show_all = 0;
  int    one_col  = 0;
  gr_opt opts[]   = {
      GR_FLAG('a', "all", &show_all, "Do not ignore entries starting with ."),
      GR_FLAG('1', "one",  &one_col, "One entry per line"),
      GR_END
  };

  gr_spec spec = {.program = "ls", .usage = "[options] [path]", .options = opts};

  gr_rest rest;
  int     rc = gr_parse(&spec, argc, argv, &rest, NULL, 0);
  if(rc != GR_OK)
    return (rc == GR_HELP) ? 0 : 1;

  const char *path = (rest.argc > 0) ? rest.argv[0] : ".";

  DIR *dir = opendir(path);
  if(!dir) {
    fprintf(stderr, "ls: cannot access '%s': No such directory\n", path);
    return 1;
  }

  s_n = 0;
  const struct dirent *de;
  while((de = readdir(dir)) != NULL && s_n < MAX_ENTRIES) {
    if(!show_all && de->d_name[0] == '.')
      continue;
    entry_t *e = &s_entries[s_n++];
    strncpy(e->name, de->d_name, NAME_MAX_LEN - 1);
    e->name[NAME_MAX_LEN - 1] = '\0';
    e->is_dir  = (de->d_type == DT_DIR);
    e->is_exec = (de->d_type == DT_REG && !e->is_dir);
  }
  closedir(dir);

  qsort(s_entries, (size_t)s_n, sizeof(entry_t), cmp_entry);

  if(s_n == 0)
    return 0;

  int max_w = 0;
  for(int i = 0; i < s_n; i++) {
    int w = (int)strlen(s_entries[i].name) + s_entries[i].is_dir;
    if(w > max_w)
      max_w = w;
  }

  int cols_avail = term_cols();
  int col_w      = max_w + COL_GAP;
  int n_cols     = (one_col || col_w >= cols_avail) ? 1 : (cols_avail / col_w);
  if(n_cols < 1) n_cols = 1;

  int n_rows = (s_n + n_cols - 1) / n_cols;

  char  *out = NULL;
  size_t cap = 0;
  size_t pos = 0;

  char pad[NAME_MAX_LEN + 4];

  for(int row = 0; row < n_rows; row++) {
    for(int col = 0; col < n_cols; col++) {
      int idx = col * n_rows + row;
      if(idx >= s_n) continue;

      const entry_t *e    = &s_entries[idx];
      int            last = (col == n_cols - 1) || ((col + 1) * n_rows + row >= s_n);

      const char *clr = e->is_dir ? CLR_DIR : (e->is_exec ? CLR_EXEC : CLR_FILE);
      pos = buf_add(&out, &cap, pos, clr);
      pos = buf_add(&out, &cap, pos, e->name);
      if(e->is_dir)
        pos = buf_add(&out, &cap, pos, "/");
      pos = buf_add(&out, &cap, pos, CLR_RESET);

      if(!last) {
        int printed = (int)strlen(e->name) + e->is_dir;
        int padding = col_w - printed;
        if(padding > 0 && padding < (int)sizeof pad) {
          memset(pad, ' ', (size_t)padding);
          pad[padding] = '\0';
          pos = buf_add(&out, &cap, pos, pad);
        }
      }
    }
    pos = buf_add(&out, &cap, pos, "\n");
  }

  if(out && pos > 0)
    write(STDOUT_FILENO, out, pos);

  free(out);
  return 0;
}
