# Alcor2OS

A small educational operating system project with kernel sources, userland hooks, and a Makefile-driven build.

## Features

- Kernel sources under `src/` with headers in `include/`
- Userland bits under `user/`
- Host tests under `tests/`
- Design notes under `docs/`
- clang-format / clang-tidy / clangd tooling checked in

## Prerequisites

- `make`
- A host C/C++ toolchain (GCC or Clang)
- Optional: `clangd` for IDE integration

## Getting started

```bash
git clone https://github.com/Darleanow/Alcor2OS.git
cd Alcor2OS
make
```

Run tests when available:

```bash
make test
```

See `Makefile` and `mk/` for additional targets.

## Project layout

| Path | Purpose |
|------|---------|
| `src/` | Kernel / core sources |
| `include/` | Public headers |
| `user/` | Userland components |
| `tests/` | Host-side tests |
| `docs/` | Design and notes |
| `scripts/` | Helper scripts |

## Contributing

1. Fork and create a topic branch
2. Keep changes focused and formatted (`clang-format`)
3. Add or update tests when behavior changes
4. Open a PR with a short rationale and test plan

## License

See the repository license file if present.
