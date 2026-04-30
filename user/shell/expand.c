/**
 * @file user/shell/expand.c
 * @brief vega expansion: $-syntax in words, special vars, user vars.
 *
 * The expander writes into a growing heap buffer to keep the implementation
 * straightforward. User variable storage is a tiny static table — fine for
 * MVP; will be revisited if/when scoping gets richer.
 */

#include "expand.h"
#include "shell.h"
#include <stdlib.h>
#include <unistd.h>

#define MAX_VARS    32
#define NAME_MAX    64
#define PID_BUF_MAX 16

static int  last_status = 0;
static char pid_buf[PID_BUF_MAX]; /* lazily filled with current PID as a string */

typedef struct {
  char *name;
  char *value;
} var_t;

static var_t vars[MAX_VARS];
static int   var_count = 0;

void expand_set_status(int status)
{
  last_status = status;
}

static var_t *find_var(const char *name)
{
  for(int i = 0; i < var_count; i++) {
    if(sh_strcmp(vars[i].name, name) == 0)
      return &vars[i];
  }
  return NULL;
}

static char *strdup_alcor(const char *s)
{
  size_t n = sh_strlen(s);
  char  *p = (char *)malloc(n + 1);
  if(!p)
    return NULL;
  for(size_t i = 0; i <= n; i++)
    p[i] = s[i];
  return p;
}

int expand_setvar(const char *name, const char *value)
{
  if(!name || !*name)
    return -1;

  var_t *existing = find_var(name);
  if(existing) {
    char *new_val = strdup_alcor(value);
    if(!new_val)
      return -1;
    free(existing->value);
    existing->value = new_val;
    return 0;
  }

  if(var_count >= MAX_VARS)
    return -1;

  char *n = strdup_alcor(name);
  char *v = strdup_alcor(value);
  if(!n || !v) {
    free(n);
    free(v);
    return -1;
  }
  vars[var_count].name  = n;
  vars[var_count].value = v;
  var_count++;
  return 0;
}

const char *expand_getvar(const char *name)
{
  var_t *v = find_var(name);
  return v ? v->value : "";
}

/* Append @p src (length @p len) to a growing heap buffer. The buffer is
 * realloc'd as needed; *cap reflects the allocated size, *len_out the
 * occupied length. NUL terminator is maintained on success. */
static int buf_append(char **buf, size_t *cap, size_t *len_out,
                      const char *src, size_t n)
{
  size_t need = *len_out + n + 1;
  if(need > *cap) {
    size_t new_cap = (*cap == 0) ? 32 : *cap * 2;
    while(new_cap < need)
      new_cap *= 2;
    char *new_buf = (char *)realloc(*buf, new_cap);
    if(!new_buf)
      return -1;
    *buf = new_buf;
    *cap = new_cap;
  }
  for(size_t i = 0; i < n; i++)
    (*buf)[(*len_out)++] = src[i];
  (*buf)[*len_out] = '\0';
  return 0;
}

/* Render an unsigned integer in decimal into @p out (caller-sized big enough
 * for u64; PID_BUF_MAX is plenty). */
static void render_uint(unsigned long n, char *out)
{
  char tmp[24];
  int  i = 0;
  if(n == 0)
    tmp[i++] = '0';
  while(n > 0) {
    tmp[i++] = (char)('0' + (n % 10));
    n /= 10;
  }
  int j = 0;
  while(i > 0)
    out[j++] = tmp[--i];
  out[j] = '\0';
}

static void render_int(int n, char *out)
{
  if(n < 0) {
    out[0] = '-';
    render_uint((unsigned long)(-n), out + 1);
  } else {
    render_uint((unsigned long)n, out);
  }
}

static int is_name_start(char c)
{
  return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_';
}

static int is_name_cont(char c)
{
  return is_name_start(c) || (c >= '0' && c <= '9');
}

/* Substitute one $-expression starting at *cur (which points just past '$').
 * Returns the number of source bytes consumed past the '$' (so caller can
 * advance src + 1 + returned). On allocation failure returns -1. */
static int expand_one(const char *cur, char **buf, size_t *cap, size_t *len)
{
  /* $? — last status */
  if(*cur == '?') {
    char  s[16];
    render_int(last_status, s);
    if(buf_append(buf, cap, len, s, sh_strlen(s)) < 0)
      return -1;
    return 1;
  }

  /* $$ — current PID */
  if(*cur == '$') {
    if(pid_buf[0] == '\0')
      render_uint((unsigned long)getpid(), pid_buf);
    if(buf_append(buf, cap, len, pid_buf, sh_strlen(pid_buf)) < 0)
      return -1;
    return 1;
  }

  /* ${NAME} */
  if(*cur == '{') {
    const char *name_start = cur + 1;
    const char *p          = name_start;
    while(*p && *p != '}')
      p++;
    if(*p != '}') {
      /* unterminated; emit the literal '${' and let the caller continue */
      if(buf_append(buf, cap, len, "${", 2) < 0)
        return -1;
      return 1;
    }
    char name[NAME_MAX];
    size_t nlen = (size_t)(p - name_start);
    if(nlen >= NAME_MAX)
      nlen = NAME_MAX - 1;
    for(size_t i = 0; i < nlen; i++)
      name[i] = name_start[i];
    name[nlen] = '\0';

    const char *val = expand_getvar(name);
    if(buf_append(buf, cap, len, val, sh_strlen(val)) < 0)
      return -1;
    return (int)(p - cur) + 1; /* consumed { ... } */
  }

  /* $NAME — bareword variable */
  if(is_name_start(*cur)) {
    const char *p = cur;
    while(is_name_cont(*p))
      p++;
    char name[NAME_MAX];
    size_t nlen = (size_t)(p - cur);
    if(nlen >= NAME_MAX)
      nlen = NAME_MAX - 1;
    for(size_t i = 0; i < nlen; i++)
      name[i] = cur[i];
    name[nlen] = '\0';

    const char *val = expand_getvar(name);
    if(buf_append(buf, cap, len, val, sh_strlen(val)) < 0)
      return -1;
    return (int)(p - cur);
  }

  /* Lone '$' followed by something non-special — emit the '$' literally. */
  if(buf_append(buf, cap, len, "$", 1) < 0)
    return -1;
  return 0;
}

char *expand_word(const char *src)
{
  if(!src)
    return NULL;

  char  *buf = NULL;
  size_t cap = 0;
  size_t len = 0;

  const char *p = src;
  while(*p) {
    if(*p == '$') {
      p++; /* skip '$' */
      int consumed = expand_one(p, &buf, &cap, &len);
      if(consumed < 0) {
        free(buf);
        return NULL;
      }
      p += consumed;
      continue;
    }
    if(buf_append(&buf, &cap, &len, p, 1) < 0) {
      free(buf);
      return NULL;
    }
    p++;
  }

  if(!buf) {
    /* Empty input — return empty heap string so caller can free uniformly. */
    buf = (char *)malloc(1);
    if(buf)
      buf[0] = '\0';
  }
  return buf;
}
