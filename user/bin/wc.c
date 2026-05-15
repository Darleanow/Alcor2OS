#include <ctype.h>
#include <fcntl.h>
#include <grendizer.h>
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
  unsigned char buf[512];
  ssize_t n;
  int in_word = 0;
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

      if (isspace(c)) {
        in_word = 0;
      } else if (!in_word) {
        out->words++;
        in_word = 1;
      }
    }
  }

  if (cur_line > out->max_line_len)
    out->max_line_len = cur_line;

  return (n < 0) ? -1 : 0;
}

static int parse_total_mode(const char *when, total_mode_t *mode)
{
  if (!when || strcmp(when, "auto") == 0)
    *mode = TOTAL_AUTO;
  else if (strcmp(when, "always") == 0)
    *mode = TOTAL_ALWAYS;
  else if (strcmp(when, "only") == 0)
    *mode = TOTAL_ONLY;
  else if (strcmp(when, "never") == 0)
    *mode = TOTAL_NEVER;
  else
    return -1;

  return 0;
}

static int process_files0_from(const char *path, int l, int w, int b, int m, int L,
                               total_mode_t total_mode, wc_counts_t *total, int *counted)
{
  int src = strcmp(path, "-") ? open(path, O_RDONLY) : STDIN_FILENO;
  if (src < 0) {
    printf("wc: cannot open '%s'\n", path);
    return -1;
  }

  char namebuf[4096];
  int namelen = 0;
  char buf[512];
  ssize_t n;
  int status = 0;

  while ((n = read(src, buf, sizeof(buf))) > 0) {
    for (ssize_t i = 0; i < n; i++) {
      if (buf[i] == '\0') {
        namebuf[namelen] = '\0';

        if (namelen > 0) {
          int fd = open(namebuf, O_RDONLY);

          if (fd < 0) {
            printf("wc: cannot open '%s'\n", namebuf);
            status = 1;
          } else {
            wc_counts_t c;

            if (count_fd(fd, &c) < 0) {
              printf("wc: read error on '%s'\n", namebuf);
              status = 1;
            } else {
              if (total_mode != TOTAL_ONLY)
                print_counts(c, l, w, b, m, L, namebuf);

              total->lines += c.lines;
              total->words += c.words;
              total->bytes += c.bytes;
              total->chars += c.chars;

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

  if (src != STDIN_FILENO)
    close(src);

  return status;
}

int main(int argc, char *argv[])
{
  int show_lines = 0;
  int show_words = 0;
  int show_bytes = 0;
  int show_chars = 0;
  int show_max = 0;
  int show_version = 0;
  int status = 0;
  const char *files0_from = NULL;
  const char *total_when = "auto";
  total_mode_t total_mode = TOTAL_AUTO;

  gr_opt opts[] = {
      GR_FLAG('c', "bytes", &show_bytes, "print the byte counts"),
      GR_FLAG('m', "chars", &show_chars, "print the character counts"),
      GR_FLAG('l', "lines", &show_lines, "print the newline counts"),
      GR_FLAG('w', "words", &show_words, "print the word counts"),
      GR_FLAG('L', "max-line-length", &show_max, "print the maximum display width"),
      GR_STR(0, "files0-from", &files0_from, "F", "read input from NUL-terminated file names in F"),
      GR_STR(0, "total", &total_when, "WHEN", "when to print total counts: auto, always, only, never"),
      GR_FLAG(0, "version", &show_version, "output version information and exit"),
      GR_END};

  gr_spec spec = {
      .program = "wc",
      .usage = "[OPTION]... [FILE]...",
      .options = opts,
      .epilog = "Print newline, word, and byte counts for each FILE, and a total line if more than one FILE is specified."};

  gr_rest rest;
  char errbuf[256];

  int rc = gr_parse(&spec, argc, argv, &rest, errbuf, sizeof(errbuf));

  if (rc == GR_HELP)
    return 0;

  if (rc == GR_ERR)
    return 1;

  if (show_version) {
    printf("wc " WC_VERSION "\n");
    return 0;
  }

  if (parse_total_mode(total_when, &total_mode) < 0) {
    printf("wc: invalid argument '%s' for '--total'\n", total_when);
    return 1;
  }

  if (!show_lines && !show_words && !show_bytes && !show_chars && !show_max)
    show_lines = show_words = show_bytes = 1;

  if (rest.argc > 64) {
    printf("wc: too many files\n");
    return 1;
  }

  wc_counts_t total = {0};
  int counted_files = 0;

  if (files0_from) {
    int ret = process_files0_from(files0_from, show_lines, show_words, show_bytes,
                                  show_chars, show_max, total_mode, &total, &counted_files);
    if (ret < 0)
      return 1;
    status = ret;
  }

  int stdin_only = !rest.argc && !files0_from;

  if (stdin_only) {
    wc_counts_t c;

    if (count_fd(STDIN_FILENO, &c) < 0) {
      printf("wc: read error on '-'\n");
      return 1;
    }

    if (total_mode != TOTAL_ONLY)
      print_counts(c, show_lines, show_words, show_bytes, show_chars, show_max, NULL);

    total = c;
    counted_files++;
  }

  for (int i = 0; i < rest.argc; i++) {
    char *path = rest.argv[i];
    int fd = strcmp(path, "-") ? open(path, O_RDONLY) : STDIN_FILENO;

    if (fd < 0) {
      printf("wc: cannot open '%s'\n", path);
      status = 1;
      continue;
    }

    wc_counts_t c;

    if (count_fd(fd, &c) < 0) {
      printf("wc: read error on '%s'\n", path);
      status = 1;

      if (fd != STDIN_FILENO)
        close(fd);

      continue;
    }

    if (fd != STDIN_FILENO)
      close(fd);

    if (total_mode != TOTAL_ONLY)
      print_counts(c, show_lines, show_words, show_bytes, show_chars, show_max, path);

    total.lines += c.lines;
    total.words += c.words;
    total.bytes += c.bytes;
    total.chars += c.chars;

    if (c.max_line_len > total.max_line_len)
      total.max_line_len = c.max_line_len;

    counted_files++;
  }

  int print_total = total_mode == TOTAL_ALWAYS || total_mode == TOTAL_ONLY ||
                    (total_mode == TOTAL_AUTO && counted_files > 1);

  if (print_total)
    print_counts(total, show_lines, show_words, show_bytes, show_chars, show_max, "total");

  return status;
}


