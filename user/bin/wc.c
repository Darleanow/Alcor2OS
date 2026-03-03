#include <ctype.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

typedef struct {
  unsigned long long lines;
  unsigned long long words;
  unsigned long long bytes;
} wc_counts_t;

static void print_counts(wc_counts_t counts, int show_lines, int show_words,
                         int show_bytes, const char *name)
{
  if(show_lines) {
    printf("%llu ", counts.lines);
  }
  if(show_words) {
    printf("%llu ", counts.words);
  }
  if(show_bytes) {
    printf("%llu ", counts.bytes);
  }
  if(name) {
    printf("%s", name);
  }
  printf("\n");
}

int main(int argc, char *argv[])
{
  int   show_lines = 0;
  int   show_words = 0;
  int   show_bytes = 0;
  char *files[64];
  int   file_count = 0;
  int   status     = 0;
  int   stdin_only = 0;

  for(int i = 1; i < argc; i++) {
    char *arg = argv[i];

    if(arg[0] == '-' && arg[1] != '\0' && strcmp(arg, "-") != 0) {
      for(int j = 1; arg[j] != '\0'; j++) {
        if(arg[j] == 'l') {
          show_lines = 1;
        } else if(arg[j] == 'w') {
          show_words = 1;
        } else if(arg[j] == 'c') {
          show_bytes = 1;
        } else {
          printf("wc: invalid option -- '%c'\n", arg[j]);
          return 1;
        }
      }
    } else {
      if(file_count >= 64) {
        printf("wc: too many files\n");
        return 1;
      }
      files[file_count++] = arg;
    }
  }

  if(!show_lines && !show_words && !show_bytes) {
    show_lines = 1;
    show_words = 1;
    show_bytes = 1;
  }

  if(file_count == 0) {
    stdin_only          = 1;
    files[file_count++] = "-";
  }

  wc_counts_t total = {0, 0, 0};

  for(int i = 0; i < file_count; i++) {
    int        fd;
    ssize_t    n;
    int        in_word = 0;
    char       buf[512];
    wc_counts_t counts = {0, 0, 0};

    if(strcmp(files[i], "-") == 0) {
      fd = STDIN_FILENO;
    } else {
      fd = open(files[i], O_RDONLY);
      if(fd < 0) {
        printf("wc: cannot open '%s'\n", files[i]);
        status = 1;
        continue;
      }
    }

    while((n = read(fd, buf, sizeof(buf))) > 0) {
      counts.bytes += (unsigned long long)n;

      for(ssize_t j = 0; j < n; j++) {
        unsigned char c = (unsigned char)buf[j];

        if(c == '\n') {
          counts.lines++;
        }

        if(isspace((int)c)) {
          in_word = 0;
        } else if(!in_word) {
          counts.words++;
          in_word = 1;
        }
      }
    }

    if(fd != STDIN_FILENO) {
      close(fd);
    }

    if(n < 0) {
      printf("wc: read error on '%s'\n", files[i]);
      status = 1;
      continue;
    }

    print_counts(counts, show_lines, show_words, show_bytes,
                 stdin_only ? NULL : files[i]);
    total.lines += counts.lines;
    total.words += counts.words;
    total.bytes += counts.bytes;
  }

  if(!stdin_only && file_count > 1) {
    print_counts(total, show_lines, show_words, show_bytes, "total");
  }

  return status;
}
