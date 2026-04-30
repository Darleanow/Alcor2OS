# Alcor2 user programs — common build settings.
# Include from each user program Makefile: include ../common.mk

USER_BASE    := $(realpath $(dir $(lastword $(MAKEFILE_LIST))))
BUILD_DIR    := $(USER_BASE)/build
MUSL_INSTALL := $(USER_BASE)/../thirdparty/musl/install
MUSL_INC     := $(MUSL_INSTALL)/include
MUSL_LIB     := $(MUSL_INSTALL)/lib
USER_LD      := $(USER_BASE)/user.ld

CC := gcc
LD := ld
AS := nasm

CFLAGS := -std=gnu11 -Wall -Wextra -Os \
          -ffreestanding -fno-stack-protector -fno-stack-check \
          -fno-lto -fno-PIC -fno-PIE -m64 -march=x86-64 \
          -mno-80387 -mno-mmx -mno-sse -mno-sse2 -mno-red-zone \
          -I$(MUSL_INC) \
          -I$(USER_BASE)/../include

LDFLAGS := -nostdlib -static -T $(USER_LD) --gc-sections

CRT0 := $(BUILD_DIR)/crt/crt0.o $(BUILD_DIR)/crt/alcor2_stdio_tty.o
LIBS := $(MUSL_LIB)/libc.a

ASFLAGS := -f elf64

# C++ user programs: use musl-cross g++ so libstdc++ matches musl (host g++ links against glibc).
MUSL_CROSS_GXX := $(wildcard $(USER_BASE)/../thirdparty/musl-cross/bin/x86_64-linux-musl-g++)
ifeq ($(MUSL_CROSS_GXX),)
CXX :=
else
CXX := $(MUSL_CROSS_GXX)
endif

# gnu++17 for older musl-cross; libstdc++ needs the normal x86-64 ABI.
CXXFLAGS := -std=gnu++17 -Wall -Wextra -Os \
          -fno-stack-protector -fno-stack-check \
          -fno-lto -fno-PIC -fno-PIE -m64 -march=x86-64 \
          -mno-red-zone \
          -I$(MUSL_INC) \
          -I$(USER_BASE)/../include

# Link order: libgcc_eh before libc so unwinder's refs (e.g. dl_iterate_phdr) resolve from libc.a.
CXX_SHLIBS := -lstdc++ -lgcc_eh -lc -lgcc
