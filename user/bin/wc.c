/**
 * wc - Print newline, word, and byte counts for each file
 *
 * wc [OPTION]... [FILE]...
 * wc [OPTION]... --files0-from=F
 */

#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

typedef struct {
    unsigned long long lines;
    unsigned long long words;
    unsigned long long bytes;
} wc_counts_t;

static void add_counts(wc_counts_t *dst, const wc_counts_t *src) {
    dst->lines += src->lines;
    dst->words += src->words;
    dst->bytes += src->bytes;
}

static int is_space_byte(unsigned char c) {
    return isspace((int)c);
}