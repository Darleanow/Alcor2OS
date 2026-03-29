#include <ctype.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
typedef struct { unsigned long long lines, words, bytes; } wc_counts_t;
static void print_counts(wc_counts_t c, int l, int w, int b, const char *name) {
  if(l) printf("%llu ", c.lines);
  if(w) printf("%llu ", c.words);
  if(b) printf("%llu ", c.bytes);
  if(name) printf("%s", name);
  printf("\n");
}
int main(int argc, char *argv[]) {
  int show_lines = 0, show_words = 0, show_bytes = 0, file_count = 0, status = 0, stdin_only;
  char *files[64];
  for(int i = 1; i < argc; i++) {
    char *arg = argv[i];
    if(arg[0] == '-' && arg[1] && strcmp(arg, "-")) {
      for(int j = 1; arg[j]; j++)
        if(arg[j] == 'l') show_lines = 1;
        else if(arg[j] == 'w') show_words = 1;
        else if(arg[j] == 'c') show_bytes = 1;
        else { printf("wc: invalid option -- '%c'\n", arg[j]); return 1; }
    } else if(file_count < 64) files[file_count++] = arg;
    else { printf("wc: too many files\n"); return 1; }
  }
  if(!show_lines && !show_words && !show_bytes) show_lines = show_words = show_bytes = 1;
  stdin_only = !file_count;
  if(stdin_only) files[file_count++] = "-";
  wc_counts_t total = {0}, counts;
  for(int i = 0; i < file_count; i++) {
    int fd = strcmp(files[i], "-") ? open(files[i], O_RDONLY) : STDIN_FILENO, in_word = 0;
    ssize_t n, j;
    char buf[512];
    counts = (wc_counts_t){0};
    if(fd < 0) { printf("wc: cannot open '%s'\n", files[i]); status = 1; continue; }
    while((n = read(fd, buf, sizeof(buf))) > 0) {
      counts.bytes += (unsigned long long)n;
      for(j = 0; j < n; j++) {
        unsigned char c = (unsigned char)buf[j];
        if(c == '\n') counts.lines++;
        if(isspace(c)) in_word = 0;
        else if(!in_word) counts.words++, in_word = 1;
      }
    }
    if(fd != STDIN_FILENO) close(fd);
    if(n < 0) { printf("wc: read error on '%s'\n", files[i]); status = 1; continue; }
    print_counts(counts, show_lines, show_words, show_bytes, stdin_only ? NULL : files[i]);
    total.lines += counts.lines, total.words += counts.words, total.bytes += counts.bytes;
  }
  if(!stdin_only && file_count > 1) print_counts(total, show_lines, show_words, show_bytes, "total");
  return status;
}
