#include <ctype.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#define WC_VERSION "1.0.0"

typedef struct {
  unsigned long long lines;
  unsigned long long words;
  unsigned long long bytes;
  unsigned long long chars;
  unsigned long long max_line_len;
} wc_counts_t;

typedef enum { TOTAL_AUTO, TOTAL_ALWAYS, TOTAL_ONLY, TOTAL_NEVER } total_mode_t;

static void print_counts(wc_counts_t c, int l, int w, int b, int m, int L, const char *name)
{
  if (l) printf("%llu ", c.lines);
  if (w) printf("%llu ", c.words);
  if (b) printf("%llu ", c.bytes);
  if (m) printf("%llu ", c.chars);
  if (L) printf("%llu ", c.max_line_len);
  if (name) printf("%s", name);
  printf("\n");
}

static int count_fd(int fd, wc_counts_t *out)
{
  unsigned char      buf[512];
  ssize_t            n;
  int                in_word = 0;
  unsigned long long cur_line = 0;

  *out = (wc_counts_t){0};

  while ((n = read(fd, buf, sizeof(buf))) > 0) {
    out->bytes += (unsigned long long)n;
    out->chars += (unsigned long long)n;
    for (ssize_t i = 0; i < n; i++) {
      unsigned char c = buf[i];
      if (c == '\n') {
        out->lines++;
        if (cur_line > out->max_line_len)
          out->max_line_len = cur_line;
        cur_line = 0;
      } else {
        cur_line++;
      }
      if (isspace(c)) in_word = 0;
      else if (!in_word) { out->words++; in_word = 1; }
    }
  }

  if (cur_line > out->max_line_len)
    out->max_line_len = cur_line;

  return (n < 0) ? -1 : 0;
}

static void print_help(void)
{
  printf("Usage: wc [OPTION]... [FILE]...\n");
  printf("       wc [OPTION]... --files0-from=F\n");
  printf("Print newline, word, and byte counts for each FILE, and a total line\n");
  printf("if more than one FILE is specified.\n\n");
  printf("  -c, --bytes            print the byte counts\n");
  printf("  -m, --chars            print the character counts\n");
  printf("  -l, --lines            print the newline counts\n");
  printf("  -w, --words            print the word counts\n");
  printf("  -L, --max-line-length  print the maximum display width\n");
  printf("      --files0-from=F    read input from the files specified by\n");
  printf("                           NUL-terminated names in file F\n");
  printf("      --total=WHEN       when to print a line with total counts;\n");
  printf("                           WHEN: auto, always, only, never\n");
  printf("      --help             display this help and exit\n");
  printf("      --version          output version information and exit\n");
}

static int process_files0_from(const char *path, int l, int w, int b, int m, int L,
                                total_mode_t total_mode, wc_counts_t *total, int *counted)
{
  int src = strcmp(path, "-") ? open(path, O_RDONLY) : STDIN_FILENO;
  if (src < 0) { printf("wc: cannot open '%s'\n", path); return -1; }

  char    namebuf[4096];
  int     namelen = 0;
  char    buf[512];
  ssize_t n;
  int     status = 0;

  while ((n = read(src, buf, sizeof(buf))) > 0) {
    for (ssize_t i = 0; i < n; i++) {
      if (buf[i] == '\0') {
        namebuf[namelen] = '\0';
        if (namelen > 0) {
          int fd = open(namebuf, O_RDONLY);
          if (fd < 0) { printf("wc: cannot open '%s'\n", namebuf); status = 1; }
          else {
            wc_counts_t c;
            if (count_fd(fd, &c) < 0) {
              printf("wc: read error on '%s'\n", namebuf); status = 1;
            } else {
              if (total_mode != TOTAL_ONLY)
                print_counts(c, l, w, b, m, L, namebuf);
              total->lines += c.lines; total->words += c.words;
              total->bytes += c.bytes; total->chars += c.chars;
              if (c.max_line_len > total->max_line_len)
                total->max_line_len = c.max_line_len;
              (*counted)++;
            }
            close(fd);
          }
        }
        namelen = 0;
      } else if (namelen < (int)sizeof(namebuf) - 1) {
        namebuf[namelen++] = buf[i];
      }
    }
  }

  if (src != STDIN_FILENO) close(src);
  return status;
}

int main(int argc, char *argv[])
{
  int          show_lines = 0, show_words = 0, show_bytes = 0, show_chars = 0, show_max = 0;
  int          status = 0, file_count = 0;
  char        *files[64];
  char        *files0_from = NULL;
  total_mode_t total_mode  = TOTAL_AUTO;

  for (int i = 1; i < argc; i++) {
    char *arg = argv[i];
    if (arg[0] == '-' && arg[1] && strcmp(arg, "-") != 0) {
      if (strcmp(arg, "--") == 0) continue;
      if (arg[1] == '-') {
        if (strcmp(arg, "--lines") == 0)               show_lines = 1;
        else if (strcmp(arg, "--words") == 0)          show_words = 1;
        else if (strcmp(arg, "--bytes") == 0)          show_bytes = 1;
        else if (strcmp(arg, "--chars") == 0)          show_chars = 1;
        else if (strcmp(arg, "--max-line-length") == 0) show_max  = 1;
        else if (strncmp(arg, "--files0-from=", 14) == 0) files0_from = arg + 14;
        else if (strncmp(arg, "--total=", 8) == 0) {
          char *when = arg + 8;
          if      (strcmp(when, "auto")   == 0) total_mode = TOTAL_AUTO;
          else if (strcmp(when, "always") == 0) total_mode = TOTAL_ALWAYS;
          else if (strcmp(when, "only")   == 0) total_mode = TOTAL_ONLY;
          else if (strcmp(when, "never")  == 0) total_mode = TOTAL_NEVER;
          else { printf("wc: invalid argument '%s' for '--total'\n", when); return 1; }
        }
        else if (strcmp(arg, "--help")    == 0) { print_help(); return 0; }
        else if (strcmp(arg, "--version") == 0) { printf("wc " WC_VERSION "\n"); return 0; }
        else { printf("wc: unrecognized option '%s'\n", arg); return 1; }
      } else {
        for (int j = 1; arg[j]; j++) {
          if      (arg[j] == 'l') show_lines = 1;
          else if (arg[j] == 'w') show_words = 1;
          else if (arg[j] == 'c') show_bytes = 1;
          else if (arg[j] == 'm') show_chars = 1;
          else if (arg[j] == 'L') show_max   = 1;
          else { printf("wc: invalid option -- '%c'\n", arg[j]); return 1; }
        }
      }
    } else if (file_count < 64) {
      files[file_count++] = arg;
    } else {
      printf("wc: too many files\n"); return 1;
    }
  }

  if (!show_lines && !show_words && !show_bytes && !show_chars && !show_max)
    show_lines = show_words = show_bytes = 1;

  wc_counts_t total        = {0};
  int         counted_files = 0;

  if (files0_from) {
    int ret = process_files0_from(files0_from, show_lines, show_words, show_bytes,
                                  show_chars, show_max, total_mode, &total, &counted_files);
    if (ret < 0) return 1;
    status = ret;
  }

  int stdin_only = !file_count && !files0_from;
  if (stdin_only) files[file_count++] = "-";

  for (int i = 0; i < file_count; i++) {
    int fd = strcmp(files[i], "-") ? open(files[i], O_RDONLY) : STDIN_FILENO;
    if (fd < 0) { printf("wc: cannot open '%s'\n", files[i]); status = 1; continue; }

    wc_counts_t c;
    if (count_fd(fd, &c) < 0) {
      printf("wc: read error on '%s'\n", files[i]); status = 1;
      if (fd != STDIN_FILENO) close(fd);
      continue;
    }
    if (fd != STDIN_FILENO) close(fd);

    if (total_mode != TOTAL_ONLY)
      print_counts(c, show_lines, show_words, show_bytes, show_chars, show_max,
                   stdin_only ? NULL : files[i]);
    total.lines += c.lines; total.words += c.words;
    total.bytes += c.bytes; total.chars += c.chars;
    if (c.max_line_len > total.max_line_len)
      total.max_line_len = c.max_line_len;
    counted_files++;
  }

  int print_total = (total_mode == TOTAL_ALWAYS || total_mode == TOTAL_ONLY ||
                     (total_mode == TOTAL_AUTO && counted_files > 1));
  if (print_total)
    print_counts(total, show_lines, show_words, show_bytes, show_chars, show_max, "total");

  return status;
}
