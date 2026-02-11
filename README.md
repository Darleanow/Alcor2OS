# Alcor2

A minimal x86_64 operating system kernel.

## Build

```sh
make           # Build ISO
make run-disk  # Run in QEMU (auto creates a disk (ext2) and uses it)
make clean     # Clean build artifacts
```

## Requirements

- gcc, ld, nasm
- xorriso
- qemu-system-x86_64

## Structure

```
├── include/           Public headers
│   └── alcor2/
├── scripts/           Build scripts & configs
├── src/
│   ├── arch/x86_64/   Architecture-specific
│   ├── drivers/       Device drivers
│   └── kernel/        Core kernel
└── thirdparty/        External dependencies
```
