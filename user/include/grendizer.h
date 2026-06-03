/**
 * @file grendizer.h
 * @brief Single-header CLI parsing library for Alcor2 userland.
 *
 * Grendizer is a zero-dependency, freestanding-compatible C library that
 * provides structured command-line argument parsing and optional subcommand
 * routing.  It is designed for the Alcor2 userland and compiled against musl.
 *
 * @par Design principles
 * - Declarative, zero-allocation API: all state lives in caller-owned structs.
 * - Options are declared with a sentinel-terminated array of ::gr_opt.
 * - Short options (@c -x ) and long options (@c --name ) are supported.
 * - Value-taking options accept @c --name VALUE or @c --name=VALUE .
 * - Short flags may be clustered: @c -xvz expands to @c -x @c -v @c -z .
 * - A bare @c -- ends option scanning; everything after becomes positional.
 * - Built-in @c --help / @c -h handler calls ::gr_usage and returns
 *   ::GR_HELP when neither is defined by the caller.
 * - Positional arguments are collected into a slice; the original @p argv
 *   array is reordered in-place so @c argv[1..positional_count] form a
 *   contiguous block on return.
 *
 * @par Subcommands
 * Declare a sentinel-terminated ::gr_cmd array and call ::gr_dispatch.
 * Subcommands may be nested arbitrarily deep.  @c help and @c --help are
 * handled automatically at every level.
 *
 * @par Quick start
 * @code
 * #include <grendizer.h>
 *
 * int main(int argc, char **argv) {
 *     int verbose   = 0;
 *     const char *output = NULL;
 *
 *     gr_opt opts[] = {
 *         GR_FLAG('v', "verbose", &verbose,       "Enable verbose output"),
 *         GR_STR ('o', "output",  &output, "PATH","Write output to PATH"),
 *         GR_END
 *     };
 *     gr_spec spec = { "mytool", "mytool [options] <input>", opts, NULL };
 *     gr_rest rest;
 *
 *     int rc = gr_parse(&spec, argc, argv, &rest, NULL, 0);
 *     if (rc != GR_OK) return rc == GR_HELP ? 0 : 1;
 *
 *     // rest.argv[0..rest.argc-1] = positional args
 * }
 * @endcode
 *
 * @par Linking
 * @code
 * $(CC) ... -lgrendizer -lc
 * @endcode
 *
 * @par Thread safety
 * This library uses no global mutable state.  Concurrent calls with
 * independent @c gr_spec / @c gr_rest objects are safe.
 */

#ifndef GRENDIZER_H
#define GRENDIZER_H

#include <stddef.h>
#include <stdio.h>

#ifdef __cplusplus
extern "C"
{
#endif

/**
 * @defgroup gr_rc Return codes
 * @{
 */

/** Parsing succeeded; positional args are ready in ::gr_rest. */
#define GR_OK 0

/**
 * Built-in help was triggered (@c --help or @c -h with no caller definition).
 * Usage has been printed; the caller should exit with code 0.
 */
#define GR_HELP 1

/** A parse error occurred; an error message was written to the caller buffer.
 */
#define GR_ERR (-1)

  /** @} */

  /**
   * @brief Kind of a single ::gr_opt entry.
   *
   * Select the appropriate kind when declaring options with ::gr_opt or the
   * helper macros (::GR_FLAG, ::GR_STR, ...).
   */
  typedef enum
  {
    /** Sentinel: terminates a ::gr_opt array.  Use ::GR_END . */
    GR_KIND_END = 0,

    /**
     * Boolean presence flag.  @c storage must point to an @c int ;
     * it is set to @c 1 on the first occurrence.  Repeated occurrences
     * are silently accepted and keep the value at @c 1 .
     */
    GR_KIND_FLAG,

    /**
     * Accumulating counter.  @c storage must point to an @c int that is
     * incremented once per occurrence.  Useful for verbosity levels
     * (@c -v @c -v @c -v or @c -vvv ).
     */
    GR_KIND_COUNT,

    /**
     * String value.  @c storage must point to a @c const @c char* ;
     * it is made to point into the original @p argv slice (no heap).
     */
    GR_KIND_STR,

    /**
     * Signed decimal integer.  @c storage must point to a @c long .
     */
    GR_KIND_INT,

    /**
     * Unsigned decimal integer.  @c storage must point to an
     * @c unsigned @c long .  A leading minus sign is rejected.
     */
    GR_KIND_UINT,

    /**
     * Floating-point value.  @c storage must point to a @c double .
     * Supported: @c -1.5 , @c 3.14e+2 (hand-rolled; no @c strtod dependency).
     */
    GR_KIND_FLOAT,
  } gr_kind;

  /**
   * @brief Descriptor for one command-line option.
   *
   * Build option tables with the helper macros and terminate with ::GR_END :
   * @code
   * gr_opt opts[] = {
   *     GR_FLAG('q', "quiet",  &quiet_flag, "Suppress informational output"),
   *     GR_INT ('n', "count",  &count_val,  "N", "Repeat N times"),
   *     GR_END
   * };
   * @endcode
   *
   * Fields @c short_name and @c long_name may independently be @c 0 / @c NULL ,
   * but at least one must be non-empty for the option to be usable.
   */
  typedef struct gr_opt
  {
    /** Short single-character key, e.g. @c 'v' for @c -v ; @c 0 if none. */
    char short_name;

    /** Long name without leading dashes, e.g. @c "verbose" ; @c NULL if none.
     */
    const char *long_name;

    /** Typed output pointer; must match ::gr_kind as documented. */
    void *storage;

    /**
     * Hint shown in the value column for value-taking kinds
     * (::GR_KIND_STR, ::GR_KIND_INT, etc.).  E.g. @c "PATH" or @c "N" .
     * Ignored for ::GR_KIND_FLAG and ::GR_KIND_COUNT .
     */
    const char *value_hint;

    /** One-line description shown in generated help text. */
    const char *help;

    /** @private Kind tag; set by the helper macros. */
    gr_kind kind;
  } gr_opt;

/**
 * @defgroup gr_macros Option constructor macros
 *
 * Use these instead of designated initialisers for cleaner table declarations.
 * @{
 */

/** @brief Declare a ::GR_KIND_FLAG option. */
#define GR_FLAG(short, long, storage, help)                                    \
  {                                                                            \
    (short), (long), (storage), NULL, (help), GR_KIND_FLAG                     \
  }

/** @brief Declare a ::GR_KIND_COUNT option. */
#define GR_COUNT(short, long, storage, help)                                   \
  {                                                                            \
    (short), (long), (storage), NULL, (help), GR_KIND_COUNT                    \
  }

/** @brief Declare a ::GR_KIND_STR option. */
#define GR_STR(short, long, storage, hint, help)                               \
  {                                                                            \
    (short), (long), (storage), (hint), (help), GR_KIND_STR                    \
  }

/** @brief Declare a ::GR_KIND_INT option. */
#define GR_INT(short, long, storage, hint, help)                               \
  {                                                                            \
    (short), (long), (storage), (hint), (help), GR_KIND_INT                    \
  }

/** @brief Declare a ::GR_KIND_UINT option. */
#define GR_UINT(short, long, storage, hint, help)                              \
  {                                                                            \
    (short), (long), (storage), (hint), (help), GR_KIND_UINT                   \
  }

/** @brief Declare a ::GR_KIND_FLOAT option. */
#define GR_FLOAT(short, long, storage, hint, help)                             \
  {                                                                            \
    (short), (long), (storage), (hint), (help), GR_KIND_FLOAT                  \
  }

/** @brief Sentinel that terminates a ::gr_opt array. */
#define GR_END                                                                 \
  {                                                                            \
    0, NULL, NULL, NULL, NULL, GR_KIND_END                                     \
  }

  /** @} */

  /**
   * @brief Parsing context passed to ::gr_parse.
   *
   * All fields except @c options are optional (may be @c NULL or zero).
   */
  typedef struct gr_spec
  {
    /**
     * Program name shown in usage output.  When @c NULL the library uses
     * the basename of @c argv[0] .
     */
    const char *program;

    /**
     * One-line usage synopsis appended after the program name, e.g.
     * @c "[options] <input> [<output>]" .  When @c NULL the library
     * emits @c "[options]" .
     */
    const char *usage;

    /**
     * Sentinel-terminated option table.  Must not be @c NULL unless no
     * options are expected; use a table with only ::GR_END in that case.
     */
    const gr_opt *options;

    /**
     * Optional paragraph printed at the end of usage output, below the
     * options table.  Useful for examples or notes.
     */
    const char *epilog;
  } gr_spec;

  /**
   * @brief Positional arguments collected by ::gr_parse.
   *
   * On success, @c argv[0..argc-1] point into the original @p argv memory.
   * The slice is valid as long as the original @p argv is in scope.
   */
  typedef struct gr_rest
  {
    int    argc; /**< Number of positional arguments. */
    char **argv; /**< Pointer into the reordered @p argv array. */
  } gr_rest;

  /**
   * @brief Parse command-line arguments according to @p spec.
   *
   * On success the function returns ::GR_OK and populates @p rest with
   * the slice of positional arguments found after option processing.
   *
   * Built-in help is printed and ::GR_HELP is returned when @c --help or
   * @c -h is encountered and the caller has not defined an option with
   * that name in @p spec.options .
   *
   * On error the function writes a NUL-terminated human-readable message
   * into @p errbuf (if non-NULL and @p errcap @> 0) and returns ::GR_ERR .
   * The message is always terminated correctly; it is truncated with
   * @c "..." when it exceeds the buffer capacity.
   *
   * @param spec     Parsing specification (non-NULL).
   * @param argc     @c main @c argc .
   * @param argv     @c main @c argv ; reordered in place on return.
   * @param rest     Filled with the positional slice (non-NULL).
   * @param errbuf   Optional buffer for error messages.
   * @param errcap   Capacity of @p errbuf in bytes, including the NUL
   * terminator.
   *
   * @return ::GR_OK, ::GR_HELP, or ::GR_ERR.
   */
  int gr_parse(
      const gr_spec *spec, int argc, char **argv, gr_rest *rest, char *errbuf,
      size_t errcap
  );

  /**
   * @brief Emit formatted usage text for @p spec to @p stream.
   *
   * Produces a usage line, an aligned options table, and the optional
   * epilog.  Suitable for custom @c --help handlers.
   *
   * @param spec    Specification to describe (non-NULL).
   * @param stream  Destination stream; uses @c stdout when @c NULL.
   */
  void gr_usage(const gr_spec *spec, FILE *stream);

  /**
   * @brief Leaf handler called after the subcommand path is resolved.
   *
   * @param userdata  Arbitrary pointer forwarded from ::gr_app.
   * @param argc      Remaining argument count (not including the subcommand
   * token).
   * @param argv      Remaining argument vector.
   *
   * @return Conventional process exit code; 0 means success.
   */
  typedef int (*gr_cmd_fn)(void *userdata, int argc, char **argv);

  /**
   * @brief Node in the subcommand tree.
   *
   * Leaf nodes have @c children == @c NULL and @c child_count == 0 , and
   * must supply a non-NULL @c run handler.
   *
   * Group nodes have @c child_count @> 0 and @c run == @c NULL .
   *
   * Terminate a sibling array with ::GR_CMD_END .
   */
  typedef struct gr_cmd
  {
    /** Subcommand token, e.g. @c "push" or @c "schema" .  @c NULL = sentinel.
     */
    const char *name;

    /** One-line summary shown in the parent group's help listing. */
    const char *summary;

    /** Optional extended description shown in the command's own help output. */
    const char *details;

    /** Handler for leaf nodes; @c NULL for group nodes. */
    gr_cmd_fn run;

    /** Child command table for group nodes; @c NULL for leaf nodes. */
    const struct gr_cmd *children;

    /** Number of entries in @p children (excluding the sentinel). */
    size_t child_count;
  } gr_cmd;

/** @brief Sentinel that terminates a ::gr_cmd array. */
#define GR_CMD_END                                                             \
  {                                                                            \
    NULL, NULL, NULL, NULL, NULL, 0                                            \
  }

  /**
   * @brief Application root descriptor passed to ::gr_dispatch.
   */
  typedef struct gr_app
  {
    /** Program name; @c NULL falls back to @c argv[0] basename. */
    const char *program;

    /** Short banner shown in the top-level help output. */
    const char *blurb;

    /** Top-level command table (non-NULL). */
    const gr_cmd *commands;

    /** Number of entries in @p commands (excluding the sentinel). */
    size_t command_count;

    /** Forwarded verbatim to every ::gr_cmd_fn @c userdata parameter. */
    void *userdata;
  } gr_app;

  /**
   * @brief Dispatch @c main arguments through the subcommand tree.
   *
   * Resolves the subcommand path (e.g. @c tool @c schema @c dump @c ... ),
   * handles @c help and @c --help at every level, and delegates to the
   * matching leaf handler.
   *
   * @param app   Application descriptor (non-NULL).
   * @param argc  @c main @c argc .
   * @param argv  @c main @c argv .
   *
   * @return The leaf handler's return value, @c 0 after help output, or
   *         @c 2 on usage errors.
   */
  int gr_dispatch(const gr_app *app, int argc, char **argv);

#ifdef __cplusplus
}
#endif

#endif /* GRENDIZER_H */
