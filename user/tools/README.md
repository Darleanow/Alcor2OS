# Vega tools

Developer tooling built on top of the vega SDK (`user/sdk/vega/`).
Empty for now — planned binaries:

- **`vega-fmt`** — vega source formatter (analogous to clang-format).
- **`vega-lint`** — static analyser for vega scripts (unused vars, dead branches,
  shadowed builtins, etc.).
- **`vega-repl`** — minimal interactive interpreter without the full shell's
  line-editor / fb_tty stack. Useful for embedded contexts and CI smoke tests.

Each tool lives in its own subdirectory and links `libvega.a` (which bundles
core + sdk objects). Headers come from `<vega/vega.h>` (entry points) plus
`<vega/ast.h>` (for tools that need to walk the AST).
