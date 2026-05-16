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

static void print_counts(wc_counts_t c, int l, int w, int b, int m, int L, const char *name)
{
  if(l)
    printf("%llu ", c.lines);
  if(w)
    printf("%llu ", c.words);
  if(b)
    printf("%llu ", c.bytes);
  if(m)
    printf("%llu ", c.chars);
  if(L)
    printf("%llu ", c.max_line_len);
  if(name)
    printf("%s", name);

  printf("\n");
}

static int count_fd(int fd, wc_counts_t *out)
{
  unsigned char      buf[512];
  ssize_t            n;
  int                in_word = 0;
  unsigned long long cur_line = 0;

  *out = (wc_counts_t){0};

  while((n = read(fd, buf, sizeof(buf))) > 0) {
    out->bytes += (unsigned long long)n;
    out->chars += (unsigned long long)n;

    for(ssize_t i = 0; i < n; i++) {
      unsigned char c = buf[i];

      if(c == '\n') {
        out->lines++;

        if(cur_line > out->max_line_len)
          out->max_line_len = cur_line;

        cur_line = 0;
      } else {
        cur_line++;
      }

      if(isspace(c)) {
        in_word = 0;
      } else if(!in_word) {
        out->words++;
        in_word = 1;
      }
    }
  }

  if(cur_line > out->max_line_len)
    out->max_line_len = cur_line;

  return (n < 0) ? -1 : 0;
}

int main(int argc, char *argv[])
{
  int show_lines = 0;
  int show_words = 0;
  int show_bytes = 0;
  int show_chars = 0;
  int show_max   = 0;

  gr_opt opts[] = {
      GR_FLAG('c', "bytes", &show_bytes, "print the byte counts"),
      GR_FLAG('m', "chars", &show_chars, "print the character counts"),
      GR_FLAG('l', "lines", &show_lines, "print the newline counts"),
      GR_FLAG('w', "words", &show_words, "print the word counts"),
      GR_FLAG('L', "max-line-length", &show_max, "print the maximum display width"),
      GR_END
  };

  gr_spec spec = {
      .program = "wc",
      .usage   = "[options] [file...]",
      .options = opts
  };

  gr_rest rest;

  int rc = gr_parse(&spec, argc, argv, &rest, NULL, 0);

  if(rc != GR_OK)
    return (rc == GR_HELP) ? 0 : 1;

  if(!show_lines && !show_words && !show_bytes && !show_chars && !show_max)
    show_lines = show_words = show_bytes = 1;

  wc_counts_t total         = {0};
  int         counted_files = 0;
  int         status        = 0;

  int stdin_only = (rest.argc == 0);

  if(stdin_only) {
    wc_counts_t c;

    if(count_fd(STDIN_FILENO, &c) < 0) {
      fprintf(stderr, "wc: read error\n");
      return 1;
    }

    print_counts(c, show_lines, show_words, show_bytes, show_chars, show_max, NULL);
    return 0;
  }

  for(int i = 0; i < rest.argc; i++) {
    const char *path = rest.argv[i];

    int fd = strcmp(path, "-") ? open(path, O_RDONLY) : STDIN_FILENO;

    if(fd < 0) {
      fprintf(stderr, "wc: cannot open '%s'\n", path);
      status = 1;
      continue;
    }

    wc_counts_t c;

    if(count_fd(fd, &c) < 0) {
      fprintf(stderr, "wc: read error on '%s'\n", path);

      if(fd != STDIN_FILENO)
        close(fd);

      status = 1;
      continue;
    }

    if(fd != STDIN_FILENO)
      close(fd);

    print_counts(c, show_lines, show_words, show_bytes, show_chars, show_max, path);

    total.lines += c.lines;
    total.words += c.words;
    total.bytes += c.bytes;
    total.chars += c.chars;

    if(c.max_line_len > total.max_line_len)
      total.max_line_len = c.max_line_len;

    counted_files++;
  }

  if(counted_files > 1)
    print_counts(total,
                 show_lines,
                 show_words,
                 show_bytes,
                 show_chars,
                 show_max,
                 "total");

  return status;
}