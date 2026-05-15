[![CI](https://github.com/Darleanow/Alcor2OS/actions/workflows/ci.yml/badge.svg)](https://github.com/Darleanow/Alcor2OS/actions/workflows/ci.yml)
[![Release](https://github.com/Darleanow/Alcor2OS/actions/workflows/release.yml/badge.svg)](https://github.com/Darleanow/Alcor2OS/actions/workflows/release.yml)

# Alcor2

x86_64 operating system written from scratch.

---

## What's in it

**Kernel**
- Physical and virtual memory management (PMM, VMM, slab heap)
- Process model: fork, exec (ELF), scheduler, signals
- VFS layer with ext2 (read) and ramfs
- Syscall table dispatched from ring 0 → ring 3
- Drivers: ATA block device, PS/2 keyboard, PIC, PIT, PCI, framebuffer console

**Userland** (statically linked against musl)
- `init` — PID 1, launches the shell
- `vega` — custom shell with bash-flavored syntax (brace-delimited control flow, `fn` functions, `let` variables, pipes, heredocs, `cmd!` fail-fast)
- Standard binaries: `cat`, `echo`, `ls`, `mkdir`, `pwd`, `rm`, `touch`, `cc`
- **Grendizer** — zero-allocation CLI option parser and subcommand dispatcher ([docs](user/docs/grendizer.md))
- ncurses, FreeType, HarfBuzz (cross-compiled for the target)

**Toolchain**
- Kernel: `clang` + `lld` + `nasm`
- Userland: `musl-cross` (`x86_64-linux-musl`) + `clang++` for C++ apps
- Bootloader: Limine (BIOS + UEFI)
- Disk image: 1 GiB ext2, populated via `fuse2fs`

---

## Requirements

Host: Linux with the following packages installed.

| Tool | Purpose |
|------|---------|
| `clang`, `lld`, `nasm` | Compiler + assembler |
| `make` | Build orchestration |
| `xorriso` | ISO generation |
| `e2fsprogs`, `fuse2fs` | ext2 disk image creation and population |
| `qemu-system-x86_64` | Emulation |
| `meson`, `ninja` | Third-party builds (ncurses, FreeType, HarfBuzz) |

---

## Build

```sh
# Local dev: bootstraps the full toolchain (musl-cross, FreeType, HarfBuzz,
# ncurses, on-disk clang+lld), then builds kernel + userland + ISO + disk
# and boots in QEMU. First run is ~1h on a clean tree (clang is the slow
# part); subsequent runs are incremental.
make run

# CI / quick sanity check: kernel-only ISO, skips userland and toolchain.
make iso-kernel

# Clean
make clean         # build/ only, keeps thirdparty/
make distclean     # also wipes thirdparty/ and disk.img
```

CI accelerates builds with ccache. Pass `CCACHE=1` to enable it locally.

---

## Repository layout

```
Alcor2/
├── src/                        Kernel source
│   ├── arch/x86_64/            GDT, IDT, ISR, syscall entry, user trampoline
│   ├── drivers/                ATA, console, keyboard, PCI, PIC, PIT
│   ├── fs/                     VFS, ext2, ramfs
│   ├── kernel/                 Process, scheduler, signals, syscall handlers
│   ├── lib/                    Kernel stdlib, compiler ABI stubs
│   └── mm/                     PMM, VMM, heap
├── include/alcor2/             Kernel headers (UAPI + internal)
├── user/
│   ├── bin/                    cat, echo, ls, mkdir, pwd, rm, touch, cc
│   ├── apps/                   shell (vega REPL), vega (CLI), font-demo, fleed, ncurses-hello, ...
│   ├── lib/                    libvega (language interpreter), libgrendizer (option parser)
│   ├── crt/                    crt0, stdio TTY shim
│   └── init/                   PID 1
├── mk/                         Makefile modules (config, kernel, thirdparty, repo)
├── scripts/                    Linker script, Limine config, disk population
├── thirdparty/                 Limine, musl, musl-cross, ncurses, FreeType, HarfBuzz
└── .github/workflows/          CI (lint + kernel build) and Release (full ISO + disk)
```

---

## CI / Release

**CI** runs on every push and pull request:
- `clang-format` diff check + `cppcheck` static analysis
- Kernel ISO build with clang-18 and ccache

**Release** runs on `main` push and `v*` tags:
- Full build: kernel + userland + third-party
- Produces `alcor2.iso` (bootable BIOS/UEFI) and `disk.img` (ext2 userland)
- Pushes to a rolling `nightly` GitHub Release, or a named release for `v*` tags

### Quick boot from a release

```sh
qemu-system-x86_64 \
  -cdrom alcor2.iso \
  -drive file=disk.img,format=raw,if=ide,cache=writeback \
  -boot order=d \
  -m 2048M -enable-kvm -cpu host
```

---

## Vega shell

Vega is the Alcor2 userland shell. Bash-flavored syntax with a few cleanups:

```sh
let name alcor2
echo "running on {name}"

fn greet(who) {
  echo "hi, {who}"
}
greet world

ls / | cat > /tmp.out
if cd /work { echo ok } else { echo missing }
cd! /work    # exits shell on failure
```

Full language reference: [user/lib/vega/README.md](user/lib/vega/README.md)
