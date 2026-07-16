# Alcor2 userland — shared by init, shell, bin, apps

UNAME := $(shell uname -s)
# AR runs the Unix archiver: packs .o files into a static library (.a).
ifeq ($(UNAME), Darwin)
  CC          := x86_64-elf-gcc
  LD          := x86_64-elf-ld
  AR          := x86_64-elf-ar
  MUSL_PREFIX := _install
else
  CC          := gcc
  LD          := ld
  AR          := ar
  MUSL_PREFIX := install
endif

USER_BASE    := $(realpath $(dir $(lastword $(MAKEFILE_LIST))))
BUILD_DIR    := $(USER_BASE)/build
MUSL_INSTALL := $(USER_BASE)/../thirdparty/musl/$(MUSL_PREFIX)
MUSL_INC     := $(MUSL_INSTALL)/include
MUSL_LIB     := $(MUSL_INSTALL)/lib

# The sysroot must carry the Alcor ABI headers; a vanilla musl install means
# the fork was not built. Fail early with the fix instead of a header error.
ifneq ($(wildcard $(MUSL_INC)),)
  ifeq ($(wildcard $(MUSL_INC)/bits/alcor_syscall.h),)
    $(error musl sysroot has no Alcor ABI headers; run: make musl)
  endif
endif
USER_LD      := $(USER_BASE)/user.ld
AS  := nasm

ASFLAGS := -f elf64

GIT_VERSION  := $(shell git -C $(USER_BASE)/.. describe --tags --always --dirty 2>/dev/null || echo dev)
DEBUG        ?= 0

CFLAGS := -std=gnu11 -Wall -Wextra -Os -MMD -MP \
          -ffreestanding -fno-stack-protector -fno-stack-check \
          -fno-lto -fno-PIC -fno-PIE -m64 -march=x86-64 \
          -mno-80387 -mno-mmx -mno-sse -mno-sse2 -mno-red-zone \
          -DALCOR2_VERSION=\"$(GIT_VERSION)\" \
          -I$(MUSL_INC) \
          -I$(USER_BASE)/include
ifeq ($(DEBUG),1)
  CFLAGS += -g -fsanitize=undefined
endif

LDFLAGS := -nostdlib -static -T $(USER_LD) --gc-sections

CRT0  := $(BUILD_DIR)/crt/crt0.o $(BUILD_DIR)/crt/alcor2_stdio_tty.o
LIBS  := $(BUILD_DIR)/lib/libgrendizer.a $(MUSL_LIB)/libc.a

# C++ apps: musl-cross g++ matches musl-hosted libstdc++ (avoid host libstdc++).
MUSL_CROSS_GXX := $(wildcard $(USER_BASE)/../thirdparty/musl-cross/bin/x86_64-linux-musl-g++)
ifeq ($(strip $(MUSL_CROSS_GXX)),)
CXX :=
else
CXX := $(MUSL_CROSS_GXX)
endif

CXXFLAGS := -std=gnu++17 -Wall -Wextra -Os -MMD -MP \
            -fno-stack-protector -fno-stack-check \
            -fno-lto -fno-PIC -fno-PIE -m64 -march=x86-64 \
            -mno-red-zone \
            -I$(MUSL_INC) \
            -I$(USER_BASE)/include

# libgcc_eh before libc (unwind / dl_iterate_phdr from libc.a)
CXX_SHLIBS := -lstdc++ -lgcc_eh -lc -lgcc
