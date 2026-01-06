# =============================================================================
# Alcor2 User Programs - Common Makefile
#
# Include this from user program Makefiles:
#   include ../common.mk
# =============================================================================

# Paths
USER_BASE    := $(realpath $(dir $(lastword $(MAKEFILE_LIST))))
BUILD_DIR    := $(USER_BASE)/build
MUSL_INSTALL := $(USER_BASE)/../thirdparty/musl/install
MUSL_INC     := $(MUSL_INSTALL)/include
MUSL_LIB     := $(MUSL_INSTALL)/lib
USER_LD      := $(USER_BASE)/user.ld

# Toolchain
CC          := gcc
LD          := ld
AS          := nasm

# C Compiler flags
CFLAGS      := -std=gnu11 -Wall -Wextra -Os \
               -ffreestanding -fno-stack-protector -fno-stack-check \
               -fno-lto -fno-PIC -fno-PIE -m64 -march=x86-64 \
               -mno-80387 -mno-mmx -mno-sse -mno-sse2 -mno-red-zone \
               -I$(MUSL_INC)

# Linker flags  
LDFLAGS     := -nostdlib -static -T $(USER_LD) --gc-sections

# CRT - Use our minimal crt0 (built in build/crt/)
CRT0        := $(BUILD_DIR)/crt/crt0.o

# Libraries (musl libc)
LIBS        := $(MUSL_LIB)/libc.a

# Assembler flags
ASFLAGS     := -f elf64

