# Alcor2 userland — shared by init, shell, bin, apps

USER_BASE    := $(realpath $(dir $(lastword $(MAKEFILE_LIST))))
BUILD_DIR    := $(USER_BASE)/build
MUSL_INSTALL := $(USER_BASE)/../thirdparty/musl/install
MUSL_INC     := $(MUSL_INSTALL)/include
MUSL_LIB     := $(MUSL_INSTALL)/lib
USER_LD      := $(USER_BASE)/user.ld

UNAME := $(shell uname -s)
ifeq ($(UNAME), Darwin)
  CC := x86_64-elf-gcc
  LD := x86_64-elf-ld
else
  CC := gcc
  LD := ld
endif
AS  := nasm

ASFLAGS := -f elf64

CFLAGS := -std=gnu11 -Wall -Wextra -Os \
          -ffreestanding -fno-stack-protector -fno-stack-check \
          -fno-lto -fno-PIC -fno-PIE -m64 -march=x86-64 \
          -mno-80387 -mno-mmx -mno-sse -mno-sse2 -mno-red-zone \
          -I$(MUSL_INC) \
          -I$(USER_BASE)/../include

LDFLAGS := -nostdlib -static -T $(USER_LD) --gc-sections

CRT0  := $(BUILD_DIR)/crt/crt0.o $(BUILD_DIR)/crt/alcor2_stdio_tty.o
LIBS  := $(MUSL_LIB)/libc.a

# C++ apps: musl-cross g++ matches musl-hosted libstdc++ (avoid host libstdc++).
MUSL_CROSS_GXX := $(wildcard $(USER_BASE)/../thirdparty/musl-cross/bin/x86_64-linux-musl-g++)
ifeq ($(strip $(MUSL_CROSS_GXX)),)
CXX :=
else
CXX := $(MUSL_CROSS_GXX)
endif

CXXFLAGS := -std=gnu++17 -Wall -Wextra -Os \
            -fno-stack-protector -fno-stack-check \
            -fno-lto -fno-PIC -fno-PIE -m64 -march=x86-64 \
            -mno-red-zone \
            -I$(MUSL_INC) \
            -I$(USER_BASE)/../include

# libgcc_eh before libc (unwind / dl_iterate_phdr from libc.a)
CXX_SHLIBS := -lstdc++ -lgcc_eh -lc -lgcc
