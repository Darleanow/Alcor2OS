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
          -I$(MUSL_INC)

LDFLAGS := -nostdlib -static -T $(USER_LD) --gc-sections

CRT0 := $(BUILD_DIR)/crt/crt0.o
LIBS := $(MUSL_LIB)/libc.a

ASFLAGS := -f elf64
