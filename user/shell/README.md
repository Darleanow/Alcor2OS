# vega

The Alcor2 userland shell. Bash-flavored semantics, brace-delimited control
flow, brace-style string interpolation, and a few cleanups. Named after
Commander Véga (Goldorak villain) — four chars, fits the bash/zsh/fish
family.

Statically linked against musl, ships as `shell.elf`, loaded by Limine as
the first user process.

---

## Table of contents

1. [Quick tour](#quick-tour)
2. [Language reference](#language-reference)
   - [Comments and whitespace](#comments-and-whitespace)
   - [Words, quoting, escapes](#words-quoting-escapes)
   - [Commands and arguments](#commands-and-arguments)
   - [Variables and expansion](#variables-and-expansion)
   - [Command substitution](#command-substitution)
   - [Pipes](#pipes)
   - [Redirections](#redirections)
   - [Here-strings and heredocs](#here-strings-and-heredocs)
   - [Logical operators and sequencing](#logical-operators-and-sequencing)
   - [Control flow: `if` / `else`](#control-flow-if--else)
   - [Control flow: `while`](#control-flow-while)
   - [Control flow: `for`](#control-flow-for)
   - [Functions](#functions)
   - [`cmd!` fail-fast sugar](#cmd-fail-fast-sugar)
   - [Multi-line input](#multi-line-input)
3. [Builtins](#builtins)
4. [Lexer rules](#lexer-rules)
5. [Formal grammar](#formal-grammar)
6. [Architecture](#architecture)
7. [Implementation notes](#implementation-notes)
8. [Differences from bash](#differences-from-bash)
9. [Known limitations](#known-limitations)
10. [Future work](#future-work)

---

## Quick tour

```sh
# Variables and brace interpolation
let name vega
echo "hello, {name}!"

# Command substitution
echo "you are in $(pwd)"

# Pipes and redirection
ls / | cat > /tmp.out
cat /tmp.out

# Here-strings and heredocs
cat <<< "one-shot literal"
cat << EOF
multi-line
literal
EOF

# Control flow
if cd /nope { echo no } else { echo yes }
for x in a b c { echo $x }
touch /m
while ls /m { rm /m; echo gone }

# Functions
fn greet(who) {
  echo "hi {who}"
}
greet world

# Fail-fast
cd! /nope    # if it fails, the shell exits with that status
```

---

## Language reference

### Comments and whitespace

`#` starts a line comment, runs to end-of-line. Horizontal whitespace
(`' '`, `\t`) separates tokens. Newlines act as statement separators
(equivalent to `;`) — see [Multi-line input](#multi-line-input).

### Words, quoting, escapes

Three word forms:

| Form              | Expansion       | Notes                                |
| ----------------- | --------------- | ------------------------------------ |
| `bare`            | `$`-syntax      | Splits on whitespace and metachars   |
| `'single'`        | None (literal)  | Backslashes are literal; no `$`      |
| `"double"`        | `$`- and `{}`-  | `\"`, `\\`, `\$` escape; others literal |

Inside a bareword, `\<char>` escapes the next character (so `\$` keeps a
literal `$`). The lexer also slurps `${VAR}` and `$(cmd)` into the
surrounding bareword so structural `{` / `(` don't split the token — see
[Lexer rules](#lexer-rules).

### Commands and arguments

A command is a whitespace-separated sequence of words. The first word is
the command name; the rest are arguments. Resolution order at execution:

1. **Function table** — user-defined functions, see [Functions](#functions).
2. **Builtins** — see [Builtins](#builtins).
3. **External path lookup** — `/bin/<name>`, then `/usr/bin/<name>`. Absolute
   paths are used as-is.

If none match, vega prints `<name>: command not found` and the command
returns 127.

### Variables and expansion

```sh
let greeting hello       # set
echo $greeting            # → hello
echo ${greeting}          # → hello (braces disambiguate)
echo "$greeting world"    # → hello world (in double quotes)
echo "{greeting} world"   # → hello world (brace interp, no $ needed)
```

Special variables:

| Name  | Meaning                                              |
| ----- | ---------------------------------------------------- |
| `$?`  | Exit status of the most recently completed command   |
| `$$`  | Current shell PID                                    |

Unset variables expand to the empty string. Field splitting on expanded
values is **not** performed — `$var` always produces a single argv entry.

`{var}` brace interpolation is **only** active inside double-quoted strings.
Outside quotes, `{` is a structural token (block delimiter), so `echo {x}`
is a syntax error.

### Command substitution

```sh
echo "today is $(pwd)"
let user $(whoami)
```

`$(cmd)` runs `cmd` in a forked subshell with stdout captured; the
captured bytes (with one trailing newline trimmed) replace the
substitution. `cmd` is itself parsed by vega — pipes, control flow, etc.
all work inside.

Like variables, substitutions produce a single argv entry; whitespace in
the captured output is preserved verbatim, never split.

Subshell limitation: variables / function definitions made inside `$(...)`
do not propagate back to the parent shell. Only the captured stdout does.

### Pipes

```sh
ls / | cat
ls | head -n 5 | tail -n 3        # if head/tail were available
```

Vega forks N children for an N-stage pipeline, plumbed by N−1 pipes. Each
stage's stdin/stdout is wired to the adjacent pipe ends; per-stage
redirections override the pipeline plumbing on the affected fd (matching
bash). Pipeline exit status is the **last stage's** status — there is no
`pipefail` mode.

Stage cap: `MAX_PIPE_STAGES = 16`. Beyond that, vega reports
`vega: pipeline too long` and returns 1.

Builtins inside a pipeline run in a forked subshell, so e.g. `cd /tmp | cat`
does not change the parent shell's directory.

### Redirections

| Operator | Effect                                    |
| -------- | ----------------------------------------- |
| `>`      | Truncate-write stdout to file             |
| `>>`     | Append-write stdout to file               |
| `<`      | Read stdin from file                      |
| `<<<`    | Here-string (see below)                   |
| `<<`     | Heredoc (see below)                       |

Multiple redirs targeting the same fd apply in source order — last one
wins, again matching bash. Targets are subject to expansion at apply time
(so `> $logfile` works when `$logfile` is set).

```sh
echo done > /tmp.out
echo more >> /tmp.out
cat < /tmp.out
```

Note: stderr (`2>`) is **not** supported. There is currently no way to
redirect or silence error output.

### Here-strings and heredocs

**Here-string `<<<`** — feeds a single string (with an implicit trailing
newline) onto stdin:

```sh
cat <<< "literal text"
```

**Heredoc `<<`** — feeds a multi-line body, terminated by a line whose
content equals the delimiter exactly:

```sh
cat << EOF
first line
second line
EOF
```

The delimiter must be unquoted and appear on a line by itself with no
leading or trailing whitespace. Variable expansion runs over the body
(unlike bash, which only expands when the delimiter is unquoted vs. quoted
— vega always expands).

Both forms cap at the kernel pipe-buffer size (~4 KB). Larger payloads are
silently truncated.

### Logical operators and sequencing

```sh
cmd1 && cmd2     # cmd2 runs only if cmd1 returned 0
cmd1 || cmd2     # cmd2 runs only if cmd1 returned non-zero
cmd1 ;  cmd2     # always run both, status is cmd2's
cmd1 \n cmd2     # newline acts as ;
```

`&&` and `||` short-circuit. `;` and newline are full statement separators
that always advance.

### Control flow: `if` / `else`

```sh
if cmd { ... }
if cmd { ... } else { ... }
if cmd { ... } else if cmd2 { ... } else { ... }
```

`cmd` is any pipeline; status 0 is "true", non-zero is "false". The body
is a brace-delimited list. Multi-line bodies need newlines or `;`
between statements.

### Control flow: `while`

```sh
while cmd { body }
```

Loops while `cmd` returns 0. There is no `break` or `continue`.

There is no Ctrl-C handler in vega; an infinite loop wedges the QEMU
console. Design test cases to terminate (e.g. `while ls /m { rm /m; ... }`
removes the marker the condition tests for).

### Control flow: `for`

```sh
for x in a b c { echo $x }
for f in "hello world" "second" { echo "{f}" }
```

Iterates over space-separated words, binding each to the loop variable.
`in` is required. Each word is expanded at iteration time — `for x in
$list` doesn't field-split, so `$list` is one iteration with the whole
value bound to `x`.

The loop variable persists after the loop in the global scope; there's
no local scope.

### Functions

```sh
fn greet(name) {
  echo "hi, {name}"
}
greet world           # → hi, world

fn pair(a b) {
  echo "{a} and {b}"
}
pair foo bar          # → foo and bar
```

Function definitions are registered into a global table at execution
time; the body steals ownership from the AST (see
[Implementation notes](#implementation-notes)). Calls bind positional
args (`argv[1..]`) into named parameters via `expand_setvar`. **There is
no local scope** — args clobber globals of the same name. Missing args
bind to the empty string; extra args are ignored.

Functions can call other functions (and themselves), use builtins, run
external commands, redirect, pipe, etc. Status is the body's last
command's status. There is no explicit `return` keyword.

Function table cap: `MAX_FUNCTIONS = 16`. Re-defining replaces the
previous body.

### `cmd!` fail-fast sugar

A trailing `!` on the command name marks the command as fail-fast: if it
exits non-zero, the shell exits with that status, printing
`vega: '<name>!' failed`.

```sh
cd! /work        # if /work doesn't exist, the shell exits
pwd!             # safe — pwd never fails
```

The `!` must be glued to the name (`cmd!`, not `cmd !`). It applies only
to `argv[0]`; trailing `!` on later arguments is literal.

Limitation: fail-fast does **not** trigger when the cmd appears inside a
pipeline (the failing stage runs in a forked child and only `_exit`s the
child, not the parent shell).

### Multi-line input

The REPL reads lines until the input forms a complete statement. "Complete"
means: no open quote, balanced braces, no pending heredoc body. While
incomplete, vega prints the continuation prompt `> ` and keeps reading.

```sh
alcor2:/$ if pwd {
> echo a
> echo b
> }
```

A stray `}` is treated as complete (negative brace depth doesn't wedge the
REPL); the parser then surfaces a `syntax error`. Pasting an entire
multi-line block in one go also works — the completeness check runs after
each newline.

Heredocs use the same loop: after `<< DELIM`, the walker stays in
"collecting body" mode until a line equals `DELIM`.

---

## Builtins

| Name      | Synopsis                          | Effect                                    |
| --------- | --------------------------------- | ----------------------------------------- |
| `help`    | `help`                            | List builtins and short descriptions      |
| `version` | `version`                         | Print vega + Alcor2 versions              |
| `clear`   | `clear`                           | Clear the framebuffer console             |
| `exit`    | `exit`                            | Exit the shell (status 0)                 |
| `cd`      | `cd <path>`                       | Change directory; status is `chdir(2)`'s  |
| `pwd`     | `pwd`                             | Print the current working directory       |
| `kbd`     | `kbd us\|fr`                      | Switch the keyboard layout (PS/2)         |
| `let`     | `let <name> <value>`              | Set a shell variable                      |

Builtins run in the shell process for standalone invocation (so `cd` can
mutate parent state); inside a pipeline they run in a forked subshell.

There is no `true` / `false`. To force success, use `pwd`; to force
failure, use `cd /nonexistent` or any non-existent command.

There is no explicit `return` from a function body; use the body's last
command's status. There is no `unset` for variables — `let var ""` blanks.

---

## Lexer rules

The lexer (frontend/lexer.c) is a hand-written character-driven scanner.
Token kinds: `EOF`, `WORD`, `STRING`, `PIPE`, `AND`, `OR`, `SEMI`,
`REDIR_OUT`, `REDIR_APPEND`, `REDIR_IN`, `HEREDOC`, `HERESTRING`, `LBRACE`,
`RBRACE`, `LPAREN`, `RPAREN`.

**Word delimiters** (terminate a bareword): `\0`, space, tab, newline,
`|`, `&`, `;`, `>`, `<`, `(`, `)`, `{`, `}`, `"`, `'`. Note: `,` is **not**
a delimiter, so `for` arg lists are space-separated, not comma-separated.

**Newlines** outside quotes lex as `TOK_SEMI`. Inside double or single
quotes, newlines are part of the string (preserved verbatim).

**Bareword escape**: `\<char>` includes `<char>` literally (the backslash
is dropped).

**Double-quote escapes**: only `\"`, `\\`, and `\$` are escapes. Other
backslash sequences include the backslash literally.

**Single-quote**: completely literal, no escapes (so `'\\'` is two
backslashes).

**`${...}` and `$(...)`**: the lexer slurps these into the surrounding
bareword so the parser sees the whole thing as one `TOK_WORD`. This works
inside barewords; inside double-quoted strings, expansion happens at
`expand_word` time on the string contents.

**`<<<`**: emitted as a single `TOK_HERESTRING`.

**`<<`**: emitted as `TOK_HEREDOC`. The lexer doesn't read the body; the
parser asks for it via `lex_read_heredoc_body(L, delim)` after consuming
the delimiter word. That helper skips the rest of the current line, then
collects subsequent lines until one matches `delim` exactly.

---

## Formal grammar

```
script     := list EOF
list       := and_or (SEMI and_or)*       -- newlines lex as SEMI too
and_or     := pipeline ((AND | OR) pipeline)*
pipeline   := unit (PIPE unit)*
unit       := if_stmt | while_stmt | for_stmt | fn_stmt | simple_command
if_stmt    := 'if' and_or '{' list '}' ('else' (if_stmt | '{' list '}'))?
while_stmt := 'while' and_or '{' list '}'
for_stmt   := 'for' WORD 'in' (WORD | STRING)* '{' list '}'
fn_stmt    := 'fn' WORD '(' WORD* ')' '{' list '}'
simple_command := (word_or_redir)+
word_or_redir  := WORD | STRING
                | (REDIR_OUT | REDIR_APPEND | REDIR_IN | HERESTRING) WORD
                | HEREDOC WORD            -- consumes lines after the
                                          -- current as the body
```

`if`/`else`/`while`/`for`/`fn`/`in` are reserved only at command
position (first token of a unit, or right after an `if` body's closing
brace). Elsewhere they are ordinary identifiers; `echo while` prints
`while`.

---

## Architecture

```
user/shell/
├── main.c                    REPL: read_complete_statement → vega_run
├── vega.c                    int vega_run(line) — parse, exec, free
├── frontend/
│   ├── lexer.c               tokenizer
│   ├── ast.c                 AST node lifecycle
│   └── parse.c               recursive-descent parser
├── runtime/
│   ├── exec.c                AST walker: dispatch, fork/exec, plumbing
│   ├── expand.c              $-syntax + brace interpolation
│   ├── fntab.c               user-defined function table
│   └── builtin.c             help/version/clear/exit/cd/pwd/kbd/let
├── platform/
│   ├── sys.c                 thin libc wrappers (open/close/stat/...)
│   ├── str.c                 sh_strlen, sh_strcmp
│   └── io.c                  sh_putchar/puts/getchar via stdio fallback
└── include/vega/             public headers, namespaced as <vega/...>
    ├── shell.h
    ├── vega.h
    ├── frontend/{ast,lexer,parse}.h
    └── runtime/{exec,expand,fntab}.h
```

**frontend** turns text into AST. **runtime** walks the AST, manages
expansion / pipes / processes / function dispatch. **platform** is the
libc-shim glue. `main.c` orchestrates the REPL loop; `vega.c` is the
"run this string" API used both by the REPL and by `$(...)` substitution.

### Compilation flow

```text
input string → lex (frontend/lexer.c) → tokens
             → parse (frontend/parse.c) → AST
             → vega_exec (runtime/exec.c) → status
             → ast_free (frontend/ast.c)
```

The AST owns its strings (heap-allocated). The runtime treats AST nodes
as **immutable** — see [Implementation notes](#implementation-notes).

---

## Implementation notes

### AST is immutable during execution

Loop bodies (`while`, `for`, function calls) re-execute the same AST
node tree each iteration. Earlier versions of `expand_cmd` mutated the
AST in place by freeing the source argv strings and replacing them with
expanded ones — which broke loops, because the second iteration would
"expand" the already-expanded text. The fix (`build_expanded_argv`)
allocates a fresh argv array per call and frees it after the command
returns. Redir target expansion was likewise moved into
`apply_one_redir` so each apply sees fresh `$var` values.

**Rule for contributors:** never write into AST node fields from runtime
code. Allocate side buffers if you need mutated copies.

### Function-table ownership

`fntab_set` takes ownership of a function's name, arg-names array, and
body AST. `vega_exec` of `AST_FN` calls `fntab_set` and then NULLs out
the AST node's fn fields so subsequent `ast_free` doesn't double-free.

Nested-function quirk: an `AST_FN` defined inside another function's
body is part of that body's tree, which lives in the function table.
The first call of the outer function "executes" the inner `AST_FN`,
stealing its body. Subsequent outer calls see body=NULL and skip
re-registration. Net effect: nested functions register on the first
call of their enclosing scope and stay registered. This is intentional;
disallow / deep-copy if a real local-scope semantics is added.

### Pipe-input plumbing for `<<<` and `<<`

Both `apply_pipe_input` (runtime/exec.c). The function creates a pipe,
writes the body content to the write end, closes the write end, and
`dup2`s the read end onto fd 0. Here-strings get an implicit trailing
newline appended to match bash; heredocs already include the newline
of their last body line.

The pipe buffer is the kernel's `PIPE_BUF_SIZE` (~4 KB on Alcor2). Larger
payloads need a tempfile; not yet implemented.

### Multi-line completeness walker

`is_input_complete` (main.c) is a forward-only walker tracking
`in_squote`, `in_dquote`, `brace_depth`, plus a small heredoc state
machine (`want_delim` / `in_hd_body`). When `<<` (not `<<<`) is seen
outside quotes, it captures the next non-whitespace word as the
delimiter and from the next newline onward watches for an exact-match
line. Until that line is seen, the walker reports incomplete.

`<<<` is detected and skipped wholesale (`p += 2`) so the trailing two
`<`s aren't re-interpreted as `<<`.

### Stdio fallback in builtin redirection

`run_builtin_redirected` saves fds 0/1 with `dup` before applying the
redirs. If `dup` returns -1, that means the shell is using the kernel's
fd-0/1-not-installed fallback path (no per-process stdio fd ever
opened). After the builtin runs, vega `dup2`s the saved fd back, or
just `close`s if `dup` had failed (drops back to fallback).

### Why `vfs_install_fd` starts at fd 3

Pre-fix: a freshly opened pipe could land on fd 0 or 1 in a shell that
hadn't explicitly opened stdio. Then `dup2(pipe_write, 1)` was a no-op
and the subsequent `close(1)` dropped the pipe end. Kernel-side fix:
`vfs_install_fd` now starts allocating at fd 3, leaving 0/1/2 alone.

### FS_BASE / GS_BASE clobber

In long mode, loading a selector into `%fs` or `%gs` overwrites the
corresponding `*_BASE` MSR with the GDT entry's base (zero for user
data) — wiping musl's TLS pointer. The `proc_enter_first_time` and
`proc_fork_child_entry` asm stubs no longer load `fs`/`gs` selectors.
Pre-fix this surfaced as `__post_Fork` NULL-derefing on `%fs:0x0`
inside `ls`.

### Pipe busy-wait must yield

Kernel `pipe_read_obj` / `pipe_write_obj` poll-wait when the pipe is
empty/full. They explicitly call `proc_schedule()` rather than relying
on timer preemption — syscall context has interrupts masked, so without
an explicit yield the polling process spins forever and the writer
never runs (matters for `$(cmd)` substitution which has the shell
itself reading a pipe).

---

## Differences from bash

| Feature             | Bash                              | Vega                              |
| ------------------- | --------------------------------- | --------------------------------- |
| `if`                | `if cmd; then ...; fi`            | `if cmd { ... }`                  |
| `else`              | `else ... fi`                     | `else { ... }`                    |
| `while`             | `while cmd; do ...; done`         | `while cmd { ... }`               |
| `for`               | `for x in a b; do ...; done`      | `for x in a b { ... }`            |
| Function            | `f() { ... }`                     | `fn f(a, b) { ... }` (space-sep)  |
| String interp       | `"$x and ${y}"`                   | `"$x and {y}"` (braces, no `$`)   |
| Arithmetic          | `$((a+b))`                        | (none yet)                        |
| Cmd substitution    | `` `cmd` `` and `$(cmd)`          | `$(cmd)` only                     |
| Last status         | `$?`                              | `$?`                              |
| Try-or-die          | manual `set -e`                   | `cmd!` (postfix)                  |
| Set variable        | `name=value`                      | `let name value`                  |
| Comment             | `#`                               | `#`                               |
| Field splitting     | `$var` splits on IFS              | `$var` is always one entry        |
| Pipefail            | `set -o pipefail`                 | (none)                            |
| stderr redir        | `2>file`, `2>&1`                  | (none)                            |
| Background `&`      | `cmd &`                           | (none)                            |
| `unset`             | `unset name`                      | use `let name ""`                 |

---

## Known limitations

- **No `true` / `false` builtins** — use `pwd` (returns 0) or
  `cd /nonexistent` (returns ≠0).
- **No stderr redirection** (`2>`, `2>&1`).
- **No background jobs** (`&`).
- **No pipefail** — pipeline status is the last stage's only.
- **No field splitting** — `$var` and `$(cmd)` always produce one argv
  entry. To split, you'd need actual word-splitting at expand time.
- **No local-variable scope** — function args clobber globals of the same
  name.
- **No `return` / `break` / `continue`** keywords.
- **No `unset`** — set a variable to `""` to blank it.
- **No quoted heredoc delimiters** — `<< 'EOF'` isn't supported; bodies
  always undergo expansion.
- **Heredoc / here-string body cap ~4 KB** (kernel pipe-buffer size).
- **Pipeline length cap** `MAX_PIPE_STAGES = 16`.
- **Function table cap** `MAX_FUNCTIONS = 16`.
- **Heredoc delimiter cap** `MAX_HEREDOC_DELIM = 64` chars.
- **`fail-fast (cmd!)` does not propagate through pipelines** — only
  triggers in simple-command position.
- **No Ctrl-C handler** — runaway loops wedge the console; reset QEMU.
- **No history / line editing** beyond Backspace and Ctrl-L (clear).
- **Stdio uses a kernel fallback path** when the shell hasn't explicitly
  opened fds 0/1/2; some interactions with redir-save/restore depend on
  this (see `run_builtin_redirected`).
- **`exit`** in `fork+exec` model: `cmd_exit()` calls `exit(0)`. Fine at
  the top level; if it ever runs in a forked subshell (e.g. a future
  `(...)` group), revisit.
- **Builtins inside `$(...)`** run in the substitution's child — variables
  / function definitions made there don't propagate back. So
  `let x $(let y hi; echo $y)` works (`x = "hi"`), but
  `$(let y hi)` followed by `echo $y` does not see `y`.

---

## Future work

Roughly ordered by impact:

- **stderr redirection** (`2>`, `2>&1`) — needs kernel `dup2` in user
  with `2` as a target, plus parser/lexer support.
- **`return` keyword** for early exit from functions (with a status).
- **`break` / `continue`** for `while` / `for`.
- **Local-variable scope** for function bodies — would unlock
  recursion-friendly patterns and make nested `let` safe.
- **Quoted heredoc delimiters** (`<< 'EOF'` to disable expansion in body).
- **Comma-separated function args** — needs `,` as a lexer delimiter
  (would also let later phases support `for x, y in ...`).
- **Field splitting on substitutions** — bash `IFS` semantics.
- **Background jobs** (`&`), `wait`, `jobs`.
- **`set -o pipefail`** and other shell options.
- **History + line editing** (arrow keys, Ctrl-R search).
- **Globbing** (`*`, `?`, `[...]`).
- **`trap`** — register handlers for signals (needs kernel SIGINT first).
- **Subshell groups** `(...)` and brace groups `{...}` as expressions.
- **Arithmetic** — `$((a+b))` or a `let` expression form.

The pipe-buffer cap is the most likely real-world ceiling to hit; a
tempfile-based fallback for `<<<` and `<<` would lift it without major
work.
