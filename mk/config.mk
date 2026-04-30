# Paths and host toolchain
KERNEL := alcor2.elf
ISO    := alcor2.iso

BUILD   := build
SRC     := src
INCLUDE := include

DISK      := disk.img
DISK_SIZE := 1024M

UNAME := $(shell uname -s)
ifeq ($(UNAME), Darwin)
  CC := x86_64-elf-gcc
  LD := x86_64-elf-ld
else
  CC := gcc
  LD := ld
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

# QEMU — KVM is optional acceleration
# USE_KVM: empty/auto = enable if /dev/kvm is readable, 1 = force, 0 = TCG only (slower).
QEMU       ?= qemu-system-x86_64
QEMU_RAM   ?= 512M
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
endif
