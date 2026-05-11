# Alcor2

A minimal x86_64 operating system kernel.

## Build

```sh
make           # Build ISO
make run       # Run in QEMU
make clean     # Clean build artifacts
```

## Requirements

- gcc, ld, nasm
- xorriso
- qemu-system-x86_64

## Structure

```
├── include/           Kernel UAPI / driver headers (alcor2/)
├── user/
│   ├── docs/          Userland library documentation (Markdown)
│   ├── include/       Headers for user programs (no kernel linkage)
│   ├── lib/           Userland static libraries
│   ├── bin/ shell/ … Programs and runtime
├── scripts/           Build scripts & configs
├── src/
│   ├── arch/x86_64/   Architecture-specific
│   ├── drivers/       Device drivers
│   └── kernel/        Core kernel
└── thirdparty/        External dependencies
```

## Userland Libraries

[`user/docs/grendizer.md`](user/docs/grendizer.md) documents **Grendizer**.