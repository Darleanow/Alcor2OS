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

