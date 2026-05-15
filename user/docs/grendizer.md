# Grendizer: The Alcor2 CLI Framework

Grendizer is the standard C library for command-line argument parsing and subcommand routing within the Alcor2 userland environment.

It is designed with the following principles:
* **Declarative**: Options are defined using a clear, sentinel-terminated array of structures.
* **Zero-allocation**: The library performs zero heap allocations (no `malloc` or `free`).
* **Memory Safety**: Positional arguments are stored as pointers to the original, reordered `argv` array.
* **Feature-rich**: Native support for short options (`-v`), long options (`--verbose`), inline values (`--file=a.txt`), and flag clustering (`-xvz`).

---

## 1. Integration

Link `libgrendizer.a` in your build system. If you are developing an Alcor2 userland application, this is automatically handled by `user/common.mk`.

Include the header in your source files:
```c
#include <grendizer.h>
```

---

## 2. Exhaustive Parsing Example

The following example demonstrates all supported data types, error handling, and positional argument extraction.

```c
#include <stdio.h>
#include <grendizer.h>

int main(int argc, char **argv) {
    /* 1. Define storage for all possible option types */
    int verbose = 0;            /* Flag */
    int debug_level = 0;        /* Count */
    const char *config = NULL;  /* String */
    long max_retries = -1;      /* Signed Integer */
    unsigned long port = 8080;  /* Unsigned Integer */
    double threshold = 0.5;     /* Floating Point */

    /* 2. Define the option table
     * Use GR_* macros. Set short key to '0' or long key to 'NULL' if not needed.
     * The table MUST be terminated with GR_END.
     */
    gr_opt opts[] = {
        GR_FLAG ('v', "verbose",   &verbose,             "Enable verbose output"),
        GR_COUNT('d', "debug",     &debug_level,         "Increase debug verbosity (e.g., -ddd)"),
        GR_STR  ('c', "config",    &config,      "FILE", "Path to the configuration file"),
        GR_INT  ('r', "retries",   &max_retries, "NUM",  "Maximum connection retries"),
        GR_UINT ('p', "port",      &port,        "PORT", "Server port number (must be positive)"),
        GR_FLOAT('t', "threshold", &threshold,   "VAL",  "Tolerance threshold (e.g., 0.75 or 1e-3)"),
        GR_END
    };

    /* 3. Configure the parser */
    gr_spec spec = {
        .program = "advanced_tool",
        .usage = "[options] -- <files...>",
        .options = opts,
        .epilog = "Advanced usage:\n"
                  "  advanced_tool -vd -c/etc/app.conf --port=9090 -- file1.txt file2.txt\n"
                  "  advanced_tool --retries -5 --threshold 3.14"
    };

    /* 4. Parse the arguments */
    gr_rest rest;
    char errbuf[256];

    /* Note: errbuf receives a human-readable error if parsing fails */
    int rc = gr_parse(&spec, argc, argv, &rest, errbuf, sizeof(errbuf));
    
    /* 5. Handle parser returns */
    if (rc == GR_HELP) {
        /* The user requested help (--help or -h). Usage has been printed automatically. */
        return 0;
    }
    if (rc == GR_ERR) {
        /* A parsing error occurred. The library prints the error to stderr automatically,
         * but we also captured it in errbuf for custom logging if needed. */
        fprintf(stderr, "Fatal Error: %s\n", errbuf);
        return 1;
    }

    /* 6. Utilize parsed values */
    printf("--- Configuration ---\n");
    printf("Verbose enabled: %s\n", verbose ? "Yes" : "No");
    printf("Debug level    : %d\n", debug_level);
    printf("Config path    : %s\n", config ? config : "(none)");
    printf("Max retries    : %ld\n", max_retries);
    printf("Target port    : %lu\n", port);
    printf("Threshold      : %f\n", threshold);

    /* 7. Access positional arguments 
     * E.g., if the user types: advanced_tool -v -- arg1 arg2
     * rest.argv[0] == "arg1", rest.argv[1] == "arg2"
     */
    printf("\n--- Positional Arguments (%d) ---\n", rest.argc);
    for (int i = 0; i < rest.argc; i++) {
        printf("  [%d]: %s\n", i, rest.argv[i]);
    }

    return 0;
}
```

---

## 3. Macro Reference

Grendizer provides macros to construct the `gr_opt` array cleanly.

| Macro | Description | Expected `storage` Type |
|-------|-------------|-------------------------|
| `GR_FLAG(short, long, storage, help)` | A boolean flag. Sets storage to `1` if present. | `int *` |
| `GR_COUNT(short, long, storage, help)`| An accumulating counter. Increments per occurrence. | `int *` |
| `GR_STR(short, long, storage, hint, help)`| A string pointer. | `const char **` |
| `GR_INT(short, long, storage, hint, help)`| A signed decimal integer. | `long *` |
| `GR_UINT(short, long, storage, hint, help)`| An unsigned decimal integer. | `unsigned long *` |
| `GR_FLOAT(short, long, storage, hint, help)`| A double-precision floating-point number. | `double *` |
| `GR_END` | Sentinel macro. Must be the last element. | - |

*(Note: Provide `0` for the short name or `NULL` for the long name if an option should only support one format.)*

---

## 4. Subcommand Routing

For complex applications utilizing nested subcommands (e.g., `git clone`, `docker container ls`), use the `gr_dispatch` function.

Each leaf command must implement a handler function matching the `gr_cmd_fn` signature:
`int handler(void *userdata, int argc, char **argv);`

```c
#include <stdio.h>
#include <grendizer.h>

/* Handler for the 'db init' subcommand */
int cmd_db_init(void *userdata, int argc, char **argv) {
    (void)userdata;
    int force = 0;
    const char *engine = "innodb";

    gr_opt opts[] = {
        GR_FLAG('f', "force",  &force,  "Force initialization (drop existing)"),
        GR_STR ('e', "engine", &engine, "NAME", "Storage engine to use"),
        GR_END
    };

    gr_spec spec = {
        .program = "tool db init",
        .usage = "[options] <db_name>",
        .options = opts,
    };

    gr_rest rest;
    char errbuf[256];
    int rc = gr_parse(&spec, argc, argv, &rest, errbuf, sizeof(errbuf));
    if (rc == GR_HELP) return 0;
    if (rc == GR_ERR) {
        fprintf(stderr, "Error: %s\n", errbuf);
        return 1;
    }

    if (rest.argc < 1) {
        fprintf(stderr, "tool db init: missing <db_name>\n");
        return 1;
    }

    printf("Initializing database '%s'\n", rest.argv[0]);
    printf("Engine: %s\n", engine);
    if (force) printf("Warning: Force flag is set!\n");

    return 0;
}

/* Handler for the 'db drop' subcommand */
int cmd_db_drop(void *userdata, int argc, char **argv) {
    (void)userdata;
    if (argc < 1) {
        fprintf(stderr, "Usage: tool db drop <db_name>\n");
        return 1;
    }
    printf("Dropping database '%s'.\n", argv[0]);
    return 0;
}

int main(int argc, char **argv) {
    /* Define the child commands for the 'db' group */
    gr_cmd db_children[] = {
        { "init", "Initialize the database", NULL, cmd_db_init, NULL, 0 },
        { "drop", "Destroy the database",    NULL, cmd_db_drop, NULL, 0 },
        GR_CMD_END
    };

    /* Define the root command table */
    gr_cmd root_commands[] = {
        { "db", "Manage the database system", NULL, NULL, db_children, 2 },
        GR_CMD_END
    };

    /* Configure the application descriptor */
    gr_app app = {
        .program = "tool",
        .blurb = "The ultimate system management tool.",
        .commands = root_commands,
        .command_count = 1,
        .userdata = NULL /* Pointer forwarded to all handler functions */
    };

    /* gr_dispatch automatically handles routing and nested --help */
    return gr_dispatch(&app, argc, argv);
}
```

### Routing Behavior:
1. Executing `tool` without arguments prints the global usage and command list.
2. Executing `tool db` prints the group-specific usage, listing `init` and `drop`.
3. Executing `tool db init --help` prints the `init` specific options (`-f`, `-e`).
4. Executing `tool db init -f my_db` routes to `cmd_db_init`, parses the flag, and extracts `my_db` as a positional argument.
