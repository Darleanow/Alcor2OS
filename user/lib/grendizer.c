/**
 * @file grendizer.c
 * @brief Grendizer CLI parsing library — implementation.
 */

#include <grendizer.h>

#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>

/** Maximum positional arguments collected during a single ::gr_parse call. */
#define GR_MAX_POSITIONAL 512

/**
 * @brief Format a diagnostic message into @p buf or print to @p fallback.
 *
 * When @p buf is non-NULL and @p cap @> 0 the message is written there and
 * truncated to fit, with a trailing @c "..." inserted when truncation occurs.
 * Otherwise the message is written directly to @p fallback (which may be
 * @c NULL to suppress output).
 */
static void
    gr__errf(char *buf, size_t cap, FILE *fallback, const char *fmt, ...)
{
  char    tmp[512];
  va_list args;

  va_start(args, fmt);
  vsnprintf(tmp, sizeof tmp, fmt, args);
  va_end(args);
  tmp[sizeof tmp - 1] = '\0';

  if(buf && cap > 0) {
    size_t avail = cap - 1;
    strncpy(buf, tmp, avail);
    buf[avail] = '\0';
    if(strlen(tmp) > avail && avail > 3) {
      buf[avail - 1] = '.';
      buf[avail - 2] = '.';
      buf[avail - 3] = '.';
    }
  } else if(fallback) {
    fputs(tmp, fallback);
    fputc('\n', fallback);
  }
}

/**
 * @brief Return the basename of @p path (the part after the last '/').
 */
static const char *gr__basename(const char *path)
{
  const char *slash;
  if(!path || !*path)
    return "program";
  slash = strrchr(path, '/');
  return slash ? slash + 1 : path;
}

/**
 * @brief Resolve the display program name from @p spec and @p argv0.
 */
static const char *gr__progname(const gr_spec *spec, const char *argv0)
{
  if(spec->program && *spec->program)
    return spec->program;
  return gr__basename(argv0);
}

/**
 * @brief Find the option with @p key as its short name, or @c NULL.
 */
static const gr_opt *gr__by_short(const gr_opt *opts, char key)
{
  for(; opts->kind != GR_KIND_END; opts++)
    if(opts->short_name && opts->short_name == key)
      return opts;
  return NULL;
}

/**
 * @brief Find the option whose long name exactly matches @p name[0..len-1].
 */
static const gr_opt *
    gr__by_long(const gr_opt *opts, const char *name, size_t len)
{
  for(; opts->kind != GR_KIND_END; opts++) {
    if(opts->long_name && strncmp(opts->long_name, name, len) == 0 &&
       opts->long_name[len] == '\0')
      return opts;
  }
  return NULL;
}

/**
 * @brief Return non-zero when @p o expects an argument value.
 */
static int gr__needs_value(const gr_opt *o)
{
  return o->kind == GR_KIND_STR || o->kind == GR_KIND_INT ||
         o->kind == GR_KIND_UINT || o->kind == GR_KIND_FLOAT;
}

/**
 * @brief Apply a ::GR_KIND_FLAG occurrence to its storage.
 */
static int gr__apply_flag(const gr_opt *o, char *buf, size_t cap)
{
  int *p = (int *)o->storage;
  if(!p) {
    gr__errf(buf, cap, stderr, "internal: null storage for flag");
    return GR_ERR;
  }
  *p = 1;
  return GR_OK;
}

/**
 * @brief Apply a ::GR_KIND_COUNT occurrence to its storage.
 */
static int gr__apply_count(const gr_opt *o, char *buf, size_t cap)
{
  int *p = (int *)o->storage;
  if(!p) {
    gr__errf(buf, cap, stderr, "internal: null storage for count");
    return GR_ERR;
  }
  (*p)++;
  return GR_OK;
}

/**
 * @brief Parse a signed decimal integer from @p text into @p out.
 *
 * @param label  Option label for error messages (e.g. @c "--count").
 */
static int gr__parse_int(
    const char *text, long *out, char *buf, size_t cap, const char *label
)
{
  char *end = NULL;
  long  val;

  if(!text || !*text) {
    gr__errf(buf, cap, stderr, "%s: missing integer value", label);
    return GR_ERR;
  }
  errno = 0;
  val   = strtol(text, &end, 10);
  if(errno == ERANGE || end == text) {
    gr__errf(buf, cap, stderr, "%s: invalid or out-of-range integer", label);
    return GR_ERR;
  }
  if(end && *end) {
    gr__errf(buf, cap, stderr, "%s: trailing garbage after integer", label);
    return GR_ERR;
  }
  *out = val;
  return GR_OK;
}

/**
 * @brief Parse an unsigned decimal integer from @p text into @p out.
 *
 * @param label  Option label for error messages.
 */
static int gr__parse_uint(
    const char *text, unsigned long *out, char *buf, size_t cap,
    const char *label
)
{
  char         *end = NULL;
  unsigned long val;

  if(!text || !*text) {
    gr__errf(buf, cap, stderr, "%s: missing unsigned value", label);
    return GR_ERR;
  }
  if(text[0] == '-') {
    gr__errf(buf, cap, stderr, "%s: value must not be negative", label);
    return GR_ERR;
  }
  errno = 0;
  val   = strtoul(text, &end, 10);
  if(errno == ERANGE || end == text) {
    gr__errf(buf, cap, stderr, "%s: invalid or out-of-range value", label);
    return GR_ERR;
  }
  if(end && *end) {
    gr__errf(buf, cap, stderr, "%s: trailing garbage after value", label);
    return GR_ERR;
  }
  *out = val;
  return GR_OK;
}

/**
 * @brief Parse a floating-point value from @p text into @p out.
 *
 * Hand-rolled to avoid a @c strtod dependency (musl supplies one, but this
 * keeps the implementation self-contained for potential freestanding builds).
 *
 * @param label  Option label for error messages.
 */
static int gr__parse_float(
    const char *text, double *out, char *buf, size_t cap, const char *label
)
{
  const char *p   = text;
  double      acc = 0.0;
  double      frac;
  int         sig  = 1;
  int         esig = 1;
  long        exp  = 0;
  int         saw  = 0;

  if(!text || !*text) {
    gr__errf(buf, cap, stderr, "%s: missing floating-point value", label);
    return GR_ERR;
  }

  if(*p == '-') {
    sig = -1;
    p++;
  } else if(*p == '+') {
    p++;
  }

  for(; *p >= '0' && *p <= '9'; p++) {
    acc = acc * 10.0 + (*p - '0');
    saw = 1;
  }

  if(*p == '.') {
    p++;
    frac = 1.0;
    for(; *p >= '0' && *p <= '9'; p++) {
      saw = 1;
      frac *= 0.1;
      acc += (*p - '0') * frac;
    }
  }
  if(!saw) {
    gr__errf(buf, cap, stderr, "%s: invalid floating-point value", label);
    return GR_ERR;
  }

  if(*p == 'e' || *p == 'E') {
    p++;
    if(*p == '-') {
      esig = -1;
      p++;
    } else if(*p == '+') {
      p++;
    }
    if(*p < '0' || *p > '9') {
      gr__errf(buf, cap, stderr, "%s: malformed exponent", label);
      return GR_ERR;
    }
    for(; *p >= '0' && *p <= '9'; p++) {
      if(exp > 10000) {
        gr__errf(buf, cap, stderr, "%s: exponent too large", label);
        return GR_ERR;
      }
      exp = exp * 10 + (*p - '0');
    }
    exp *= esig;
    for(; exp > 0; exp--)
      acc *= 10.0;
    for(; exp < 0; exp++)
      acc *= 0.1;
  }

  if(*p) {
    gr__errf(buf, cap, stderr, "%s: trailing garbage after number", label);
    return GR_ERR;
  }

  *out = sig * acc;
  return GR_OK;
}

/**
 * @brief Apply a value-taking option @p o with string value @p val.
 *
 * @param label  Display label used in error messages.
 */
static int gr__apply_value(
    const gr_opt *o, const char *val, char *buf, size_t cap, const char *label
)
{
  switch(o->kind) {
  case GR_KIND_STR: {
    const char **p = (const char **)o->storage;
    if(!p) {
      gr__errf(buf, cap, stderr, "internal: null storage");
      return GR_ERR;
    }
    *p = val;
    return GR_OK;
  }
  case GR_KIND_INT:
    return gr__parse_int(val, (long *)o->storage, buf, cap, label);
  case GR_KIND_UINT:
    return gr__parse_uint(val, (unsigned long *)o->storage, buf, cap, label);
  case GR_KIND_FLOAT:
    return gr__parse_float(val, (double *)o->storage, buf, cap, label);
  default:
    gr__errf(buf, cap, stderr, "internal: unexpected option kind");
    return GR_ERR;
  }
}

void gr_usage(const gr_spec *spec, FILE *stream)
{
  const gr_opt *o;
  unsigned      col = 0, w;

  if(!stream)
    stream = stdout;

  fprintf(
      stream, "Usage: %s %s\n",
      (spec->program && *spec->program) ? spec->program : "PROGRAM",
      (spec->usage && *spec->usage) ? spec->usage : "[options]"
  );

  /* Pass 1: measure the left column width. */
  for(o = spec->options; o->kind != GR_KIND_END; o++) {
    char tmp[64];
    int  n = 0;
    if(o->short_name)
      n += 2; /* -x  */
    if(o->short_name && o->long_name)
      n += 2; /* ", " */
    if(o->long_name)
      n += 2 + (int)strlen(o->long_name); /* --name */
    if(gr__needs_value(o)) {
      const char *hint =
          (o->value_hint && *o->value_hint) ? o->value_hint : "VALUE";
      n += 1 + (int)strlen(hint);
    }
    (void)tmp;
    w = (unsigned)n;
    if(w > col)
      col = w;
  }
  if(col > 32)
    col = 32;

  fprintf(stream, "\nOptions:\n");

  /* Pass 2: print. */
  for(o = spec->options; o->kind != GR_KIND_END; o++) {
    char cell[80] = {0};
    int  len      = 0;

    if(o->short_name)
      len +=
          snprintf(cell + len, sizeof cell - (size_t)len, "-%c", o->short_name);
    if(o->short_name && o->long_name)
      len += snprintf(cell + len, sizeof cell - (size_t)len, ", ");
    if(o->long_name)
      len +=
          snprintf(cell + len, sizeof cell - (size_t)len, "--%s", o->long_name);
    if(gr__needs_value(o)) {
      const char *hint =
          (o->value_hint && *o->value_hint) ? o->value_hint : "VALUE";
      len += snprintf(cell + len, sizeof cell - (size_t)len, " %s", hint);
    }

    fprintf(stream, "  %-*s  %s\n", (int)col, cell, o->help ? o->help : "");
  }

  if(spec->epilog && *spec->epilog)
    fprintf(stream, "\n%s\n", spec->epilog);
}

int gr_parse(
    const gr_spec *spec, int argc, char **argv, gr_rest *rest, char *errbuf,
    size_t errcap
)
{
  char       *pos[GR_MAX_POSITIONAL];
  int         pos_count    = 0;
  int         scan_options = 1;
  char        lbl[80];
  const char *prog;
  int         i;

  if(!spec || !argv || !rest) {
    gr__errf(errbuf, errcap, stderr, "gr_parse: null argument");
    return GR_ERR;
  }
  if(argc < 1) {
    gr__errf(errbuf, errcap, stderr, "gr_parse: argc < 1");
    return GR_ERR;
  }

  prog       = gr__progname(spec, argv[0]);
  rest->argc = 0;
  rest->argv = NULL;

  for(i = 1; i < argc; i++) {
    const char *tok = argv[i];

    if(!scan_options || tok[0] != '-' || tok[1] == '\0') {
      if(pos_count >= GR_MAX_POSITIONAL) {
        gr__errf(
            errbuf, errcap, stderr, "too many positional arguments (max %d)",
            GR_MAX_POSITIONAL
        );
        return GR_ERR;
      }
      pos[pos_count++] = argv[i];
      continue;
    }

    if(strcmp(tok, "--") == 0) {
      scan_options = 0;
      continue;
    }

    if(tok[1] == '-') {
      const char   *body = tok + 2;
      const char   *eq   = strchr(body, '=');
      size_t        nlen = eq ? (size_t)(eq - body) : strlen(body);
      const gr_opt *o;

      /* Built-in --help (only when the caller does not claim --help). */
      if(nlen == 4 && memcmp(body, "help", 4) == 0 &&
         !gr__by_long(spec->options, "help", 4)) {
        gr_spec s = *spec;
        s.program = prog;
        gr_usage(&s, stdout);
        return GR_HELP;
      }

      o = gr__by_long(spec->options, body, nlen);
      if(!o) {
        gr__errf(
            errbuf, errcap, stderr, "%s: unknown option '--%.*s'", prog,
            (int)nlen, body
        );
        return GR_ERR;
      }

      if(!gr__needs_value(o)) {
        if(eq) {
          gr__errf(
              errbuf, errcap, stderr, "%s: option '--%s' does not take a value",
              prog, o->long_name
          );
          return GR_ERR;
        }
        int rc = (o->kind == GR_KIND_FLAG) ? gr__apply_flag(o, errbuf, errcap)
                                           : gr__apply_count(o, errbuf, errcap);
        if(rc != GR_OK)
          return rc;
        continue;
      }

      /* Value-taking long option. */
      const char *val = eq ? eq + 1 : NULL;
      if(!val) {
        if(i + 1 >= argc) {
          gr__errf(
              errbuf, errcap, stderr, "%s: option '--%s' requires a value",
              prog, o->long_name
          );
          return GR_ERR;
        }
        val = argv[++i];
      }
      snprintf(lbl, sizeof lbl, "--%s", o->long_name);
      int rc = gr__apply_value(o, val, errbuf, errcap, lbl);
      if(rc != GR_OK)
        return rc;
      continue;
    }

    /* Built-in -h (only when the caller does not claim -h). */
    if(tok[1] == 'h' && tok[2] == '\0' && !gr__by_short(spec->options, 'h')) {
      gr_spec s = *spec;
      s.program = prog;
      gr_usage(&s, stdout);
      return GR_HELP;
    }

    for(const char *p = tok + 1; *p; p++) {
      char          key = *p;
      const gr_opt *o   = gr__by_short(spec->options, key);
      if(!o) {
        gr__errf(errbuf, errcap, stderr, "%s: unknown option '-%c'", prog, key);
        return GR_ERR;
      }

      if(o->kind == GR_KIND_FLAG) {
        gr__apply_flag(o, errbuf, errcap);
        continue;
      }
      if(o->kind == GR_KIND_COUNT) {
        gr__apply_count(o, errbuf, errcap);
        continue;
      }

      /* Value-taking short option: remainder of cluster or next token. */
      const char *val = (p[1] != '\0') ? p + 1 : NULL;
      if(!val) {
        if(i + 1 >= argc) {
          gr__errf(
              errbuf, errcap, stderr, "%s: option '-%c' requires a value", prog,
              key
          );
          return GR_ERR;
        }
        val = argv[++i];
      }
      snprintf(lbl, sizeof lbl, "-%c", key);
      int rc = gr__apply_value(o, val, errbuf, errcap, lbl);
      if(rc != GR_OK)
        return rc;
      break; /* consumed the rest of the cluster as the value */
    }
  }

  /* Reorder argv so positionals fill argv[1..pos_count]. */
  for(int j = 0; j < pos_count; j++)
    argv[j + 1] = pos[j];
  if(pos_count < argc - 1)
    argv[pos_count + 1] = NULL;

  rest->argc = pos_count;
  rest->argv = pos_count > 0 ? argv + 1 : NULL;

  return GR_OK;
}

/**
 * @brief Return the effective program name from @p app and original @p argv0.
 */
static const char *gr__app_prog(const gr_app *app, const char *argv0)
{
  if(app->program && *app->program)
    return app->program;
  return gr__basename(argv0);
}

/**
 * @brief Look up @p name in a flat ::gr_cmd table of length @p n.
 */
static const gr_cmd *
    gr__find_cmd(const gr_cmd *table, size_t n, const char *name)
{
  size_t i;
  if(!name)
    return NULL;
  for(i = 0; i < n; i++)
    if(table[i].name && strcmp(table[i].name, name) == 0)
      return &table[i];
  return NULL;
}

/**
 * @brief Build @p dst = @p prefix + " " + @p seg into a fixed-size buffer.
 */
static void
    gr__path_join(char *dst, size_t cap, const char *prefix, const char *seg)
{
  if(!seg || !*seg)
    return;
  if(!prefix || !*prefix)
    snprintf(dst, cap, "%s", seg);
  else
    snprintf(dst, cap, "%s %s", prefix, seg);
}

/**
 * @brief Print a group-level help listing for @p cmds to @p stream.
 */
static void gr__print_group(
    const gr_app *app, FILE *stream, const gr_cmd *cmds, size_t n,
    const char *path
)
{
  unsigned col = 0, w;
  size_t   i;

  for(i = 0; i < n; i++) {
    if(!cmds[i].name)
      continue;
    w = (unsigned)strlen(cmds[i].name);
    if(w > col)
      col = w;
  }
  if(col > 24)
    col = 24;

  fprintf(stream, "Commands:\n");
  for(i = 0; i < n; i++) {
    if(!cmds[i].name)
      continue;
    fprintf(
        stream, "  %-*s  %s\n", (int)col, cmds[i].name,
        cmds[i].summary ? cmds[i].summary : ""
    );
  }
  fprintf(stream, "\nRun '%s", app->program ? app->program : "program");
  if(path && *path)
    fprintf(stream, " %s", path);
  fprintf(stream, " help <command>' for details.\n");
}

/**
 * @brief Print help for a single command node @p cmd at @p path.
 */
static void gr__print_cmd(
    const gr_app *app, FILE *stream, const gr_cmd *cmd, const char *path
)
{
  const char *prog = app->program ? app->program : "program";

  if(cmd->child_count > 0) {
    fprintf(stream, "Usage: %s %s <command> [...]\n\n", prog, path);
    if(cmd->summary)
      fprintf(stream, "%s\n\n", cmd->summary);
    if(cmd->details)
      fprintf(stream, "%s\n\n", cmd->details);
    gr__print_group(app, stream, cmd->children, cmd->child_count, path);
  } else {
    fprintf(stream, "Usage: %s %s [arguments]\n\n", prog, path);
    if(cmd->summary)
      fprintf(stream, "%s\n", cmd->summary);
    if(cmd->details)
      fprintf(stream, "\n%s\n", cmd->details);
  }
}

/**
 * @brief Recursively resolve a help path starting from @p argv[0].
 *
 * @param prog      Display program name.
 * @param app       Application descriptor.
 * @param cmds      Current-level command table.
 * @param n         Length of @p cmds.
 * @param path      Accumulated path string (modified in place).
 * @param cap       Capacity of @p path buffer.
 * @param argc      Remaining argument count.
 * @param argv      Remaining argument vector.
 *
 * @return 0 on success, 2 on error.
 */
static int gr__help_walk(
    const char *prog, const gr_app *app, const gr_cmd *cmds, size_t n,
    char *path, size_t cap, int argc, char **argv
)
{
  const gr_cmd *cmd;
  char          next[256];

  if(argc == 0) {
    fprintf(stderr, "%s: 'help' requires a command name\n", prog);
    return 2;
  }
  cmd = gr__find_cmd(cmds, n, argv[0]);
  if(!cmd) {
    fprintf(stderr, "%s: no such command: %s\n", prog, argv[0]);
    return 2;
  }
  gr__path_join(next, sizeof next, path, cmd->name);

  if(argc == 1) {
    gr__print_cmd(app, stdout, cmd, next);
    return 0;
  }
  if(cmd->child_count == 0) {
    fprintf(stderr, "%s: '%s' has no subcommands\n", prog, next);
    return 2;
  }
  return gr__help_walk(
      prog, app, cmd->children, cmd->child_count, next, cap, argc - 1, argv + 1
  );
}

/**
 * @brief Recursive dispatch worker.
 */
static int gr__dispatch(
    const char *prog, const gr_app *app, const gr_cmd *parent,
    const gr_cmd *cmds, size_t n, const char *path, int argc, char **argv
)
{
  const gr_cmd *cmd;
  char          next[256];

  /* No tokens left: show help at this level. */
  if(argc == 0) {
    if(parent)
      gr__print_cmd(app, stdout, parent, path);
    else {
      fprintf(
          stdout, "Usage: %s <command> [...]\n\n",
          app->program ? app->program : "program"
      );
      if(app->blurb)
        fprintf(stdout, "%s\n\n", app->blurb);
      gr__print_group(app, stdout, cmds, n, "");
    }
    return 0;
  }

  /* help / --help at this level. */
  if(strcmp(argv[0], "help") == 0 || strcmp(argv[0], "--help") == 0) {
    if(argc == 1) {
      if(parent)
        gr__print_cmd(app, stdout, parent, path);
      else {
        fprintf(
            stdout, "Usage: %s <command> [...]\n\n",
            app->program ? app->program : "program"
        );
        if(app->blurb)
          fprintf(stdout, "%s\n\n", app->blurb);
        gr__print_group(app, stdout, cmds, n, "");
      }
      return 0;
    }
    char empty[256] = {0};
    return gr__help_walk(
        prog, app, cmds, n, empty, sizeof empty, argc - 1, argv + 1
    );
  }

  cmd = gr__find_cmd(cmds, n, argv[0]);
  if(!cmd) {
    fprintf(stderr, "%s: unknown command '%s'\n", prog, argv[0]);
    fprintf(stderr, "Run '%s help' for usage.\n", prog);
    return 2;
  }

  gr__path_join(next, sizeof next, path, cmd->name);

  /* Descend into group. */
  if(cmd->child_count > 0)
    return gr__dispatch(
        prog, app, cmd, cmd->children, cmd->child_count, next, argc - 1,
        argv + 1
    );

  /* Leaf: check for inline --help. */
  if(argc >= 2 &&
     (strcmp(argv[1], "--help") == 0 || strcmp(argv[1], "-h") == 0)) {
    gr__print_cmd(app, stdout, cmd, next);
    return 0;
  }

  if(!cmd->run) {
    fprintf(stderr, "%s: internal: no handler for '%s'\n", prog, cmd->name);
    return 2;
  }

  return cmd->run(app->userdata, argc - 1, argv + 1);
}

int gr_dispatch(const gr_app *app, int argc, char **argv)
{
  const char *prog;

  if(!app || !argv) {
    fputs("gr_dispatch: null argument\n", stderr);
    return 2;
  }
  if(!app->commands || app->command_count == 0) {
    fputs("gr_dispatch: no commands registered\n", stderr);
    return 2;
  }

  prog = gr__app_prog(app, argc > 0 ? argv[0] : NULL);

  return gr__dispatch(
      prog, app, NULL, app->commands, app->command_count, "",
      argc > 1 ? argc - 1 : 0, argc > 1 ? argv + 1 : NULL
  );
}
