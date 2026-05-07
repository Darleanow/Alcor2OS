# Paths and host toolchain
KERNEL := alcor2.elf
ISO    := alcor2.iso

BUILD   := build
SRC     := src
INCLUDE := include

DISK      := disk.img
DISK_SIZE := 1024M

UNAME := $(shell uname -s)
# mke2fs is keg-only in Homebrew so PATH may not have it; fall back to the standard install dir.
ifeq ($(UNAME), Darwin)
  CC          := x86_64-elf-gcc
  LD          := x86_64-elf-ld
  JOBS        := $(shell sysctl -n hw.ncpu 2>/dev/null || echo 1)
  MUSL_PREFIX := _install
  MKE2FS      := $(shell command -v mke2fs 2>/dev/null \
                 || ls /opt/homebrew/opt/e2fsprogs/sbin/mke2fs 2>/dev/null \
                 || ls /usr/local/opt/e2fsprogs/sbin/mke2fs 2>/dev/null \
                 || echo mke2fs)
else
  CC          := gcc
  LD          := ld
  JOBS        := $(shell nproc 2>/dev/null || echo 1)
  MUSL_PREFIX := install
  MKE2FS      := mke2fs
endif
AS := nasm

# Kernel compile / link
CFLAGS := -std=gnu11 -Wall -Wextra -Werror \
          -ffreestanding -fno-stack-protector -fno-stack-check \
          -fno-lto -fPIE -m64 -march=x86-64 \
          -mno-80387 -mno-mmx -mno-sse -mno-sse2 -mno-red-zone \
          -I$(INCLUDE) -MMD -MP

LDFLAGS := -nostdlib -static -pie --no-dynamic-linker \
           -z text -z max-page-size=0x1000 -T scripts/linker.ld

ASFLAGS := -f elf64

# Lint: userland .c handled here (KERNEL_SRCS_* live in mk/kernel.mk)
USER_SRCS_C := $(shell find user \( -path '*/.cache/*' \) -prune -o -name '*.c' -print 2>/dev/null | LC_ALL=C sort)

# QEMU — hardware acceleration is optional (KVM on Linux, HVF on Intel Mac).
# Apple Silicon hosts can't accelerate an x86_64 guest, so they stay on TCG.
# USE_KVM: empty/auto = enable if available, 1 = force, 0 = TCG only (slower).
QEMU       ?= qemu-system-x86_64
QEMU_RAM   ?= 2048M
USE_KVM    ?=

QEMU_KVM :=
ifeq ($(UNAME),Linux)
  ifeq ($(USE_KVM),0)
    QEMU_KVM :=
  else ifeq ($(USE_KVM),1)
    QEMU_KVM := -enable-kvm -cpu host
  else
    QEMU_KVM := $(shell test -r /dev/kvm && printf '%s' '-enable-kvm -cpu host')
  endif
else ifeq ($(UNAME),Darwin)
  ifneq ($(USE_KVM),0)
    ifeq ($(shell uname -m),x86_64)
      QEMU_KVM := -accel hvf -cpu host
    endif
  endif
endif
