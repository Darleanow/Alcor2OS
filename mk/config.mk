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
  CC          ?= x86_64-elf-gcc
  LD          ?= x86_64-elf-ld
  JOBS        := $(shell sysctl -n hw.ncpu 2>/dev/null || echo 1)
  MUSL_PREFIX := _install
  MKE2FS      := $(shell command -v mke2fs 2>/dev/null \
                 || ls /opt/homebrew/opt/e2fsprogs/sbin/mke2fs 2>/dev/null \
                 || ls /usr/local/opt/e2fsprogs/sbin/mke2fs 2>/dev/null \
                 || echo mke2fs)
else
  CC          ?= clang
  LD          ?= ld
  JOBS        := $(shell nproc 2>/dev/null || echo 1)
  MUSL_PREFIX := install
  MKE2FS      := mke2fs
endif
AS := nasm

# ccache wrapper — set CCACHE=1 to transparently wrap CC.
# CI sets this automatically; local builds can opt-in.
CCACHE        ?= 0
CCACHE_BIN    := $(shell command -v ccache 2>/dev/null)
ifeq ($(CCACHE),1)
  ifneq ($(CCACHE_BIN),)
    CCACHE_PREFIX := $(CCACHE_BIN)
  else
    $(warning ccache requested but not found in PATH — building without it)
    CCACHE_PREFIX :=
  endif
else
  CCACHE_PREFIX :=
endif

# Version string from the nearest v* milestone tag (the rolling nightly tag is
# excluded so it never masks the real version). Falls back to the short SHA.
GIT_VERSION := $(shell git describe --tags --match 'v*' --always --dirty 2>/dev/null || echo dev)
DEBUG       ?= 0

# Kernel compile / link
CFLAGS := -std=gnu11 -Wall -Wextra -Werror \
          -O2 -ffreestanding -fno-stack-protector -fno-stack-check \
          -fno-lto -fPIE -m64 -march=x86-64 \
          -mno-80387 -mno-mmx -mno-sse -mno-sse2 -mno-red-zone \
          -DALCOR2_VERSION=\"$(GIT_VERSION)\" \
          -I$(INCLUDE) -Isrc -MMD -MP
ifeq ($(DEBUG),1)
  CFLAGS += -g
endif

LDFLAGS := -nostdlib -static -pie --no-dynamic-linker \
           -z text -z max-page-size=0x1000 -T scripts/linker.ld

ASFLAGS := -f elf64

# Lint: userland .c handled here (KERNEL_SRCS_* live in mk/kernel.mk).
# doomgeneric is a third-party submodule and is excluded from lint/format.
USER_SRCS_C := $(shell find user \( -path '*/.cache/*' -o -path '*/doomgeneric/*' \) -prune -o -name '*.c' -print 2>/dev/null | LC_ALL=C sort)

# QEMU — hardware acceleration is optional (KVM on Linux, HVF on Intel Mac).
# Apple Silicon hosts can't accelerate an x86_64 guest, so they stay on TCG.
# USE_KVM: empty/auto = enable if available, 1 = force, 0 = TCG only (slower).
QEMU       ?= qemu-system-x86_64
QEMU_RAM   ?= 2048M
USE_KVM    ?=

# Default display (Linux/GTK): grab-on-hover confines the pointer so relative
# mouse deltas stay clean at the screen edge; zoom-to-fit=off shows the guest
# 1:1 instead of bilinear-stretching it (which blurs text in fullscreen).
# Override with QEMU_DISPLAY= on other hosts or to pick another UI.
ifeq ($(UNAME),Linux)
  QEMU_DISPLAY ?= gtk,grab-on-hover=on,zoom-to-fit=off
endif

QEMU_KVM := -cpu max
ifeq ($(UNAME),Linux)
  ifeq ($(USE_KVM),0)
    QEMU_KVM := -cpu max
  else ifeq ($(USE_KVM),1)
    QEMU_KVM := -enable-kvm -cpu host
  else
    QEMU_KVM := $(shell test -r /dev/kvm && printf '%s' '-enable-kvm -cpu host' || printf '%s' '-cpu max')
  endif
else ifeq ($(UNAME),Darwin)
  ifneq ($(USE_KVM),0)
    ifeq ($(shell uname -m),x86_64)
      QEMU_KVM := -accel hvf -cpu host
    endif
  endif
endif
