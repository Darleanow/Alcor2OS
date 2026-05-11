# 🚀 Alcor2 OS

A modern, from-scratch `x86_64` operating system kernel and userland, built with a clean Clang/LLVM toolchain.

## 🌟 Philosophy & Features

- **Clang-Native Toolchain**: Built entirely without GCC/TCC, leveraging modern LLVM features and LLD for fast, strict linking.
- **Custom Userland**: Includes a bespoke, zero-allocation CLI framework (**Grendizer**) for advanced command parsing and routing.
- **POSIX-like Environment**: Custom standard C library integration (musl-based) and shell tools.
- **Robust Build System**: Automated ISO generation and `ext2` disk population via `fuse2fs`.

## 🛠️ Requirements

To build and run Alcor2, ensure you have the following dependencies installed on your host Linux system:
- **Compilers**: `clang`, `lld`, `nasm`
- **Build Tools**: `make`, `xorriso`, `e2fsprogs` (for `fuse2fs` and `mke2fs`)
- **Emulation**: `qemu-system-x86_64`

## 🚀 Getting Started

Clone the repository and build the operating system:

```sh
# Build the ISO and populate the ext2 disk image
make

# Run the OS in QEMU (automatically attaches the disk)
make run

# Clean all build artifacts
make clean
```

## 📁 Architecture

```text
Alcor2/
├── include/           # Kernel UAPI and driver headers
├── src/               # Kernel Source Code
│   ├── arch/x86_64/   # Architecture-specific initialization (GDT, IDT, CPUID)
│   ├── drivers/       # Device drivers (ATA block devices, PS/2 Keyboard)
│   └── kernel/        # Core kernel logic
├── user/              # Userland Ecosystem
│   ├── bin/           # Standard binaries (ls, shell, etc.)
│   ├── docs/          # Userland documentation
│   ├── include/       # Userland headers (no kernel linkage)
│   └── lib/           # Userland static libraries (e.g., libgrendizer.a)
├── scripts/           # Build system and CI scripts
└── thirdparty/        # External dependencies (Limine bootloader, musl libc)
```

## 📚 Userland Libraries

Alcor2 provides custom-built libraries for userland development.
- **[Grendizer CLI Framework](user/docs/grendizer.md)**: A zero-allocation option parser and subcommand dispatcher for C.