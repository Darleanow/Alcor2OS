include ../../common.mk

NC_ROOT := $(USER_BASE)/../thirdparty/ncurses-install/usr
NC_A    := $(NC_ROOT)/lib/libncurses.a $(NC_ROOT)/lib/libtinfo.a

CROSS := $(USER_BASE)/../thirdparty/musl-cross/bin/x86_64-linux-musl-gcc

# Default ncurses installs headers in include/ncurses/ unless configured with
# --enable-overwrite; support both layouts (guest + host agree after make ncurses).
NCURSES_SUB := $(wildcard $(NC_ROOT)/include/ncurses)
NC_CFLAGS     := -I$(NC_ROOT)/include $(if $(NCURSES_SUB),-I$(NC_ROOT)/include/ncurses)

ifeq ($(wildcard $(CROSS)),)
$(error ncurses-hello: missing $(CROSS) — run: make musl-cross)
endif
ifeq ($(wildcard $(NC_A)),)
$(error ncurses-hello: missing ncurses libs — run from repo root: make ncurses)
endif

APP     := ncurses-hello
OUT_DIR := $(BUILD_DIR)/apps/$(APP)
TARGET  := $(BUILD_DIR)/apps/$(APP).elf

.PHONY: all clean

all: $(TARGET)

$(OUT_DIR):
	@mkdir -p $(OUT_DIR)

# Match user/bin freestanding flags + ncurses headers (libc via CRT0 + musl libc.a).
$(TARGET): main.c $(CRT0) $(NC_A) | $(OUT_DIR)
	$(CROSS) $(CFLAGS) $(NC_CFLAGS) -c main.c -o $(OUT_DIR)/main.o
	$(CROSS) -nostdlib -static -T $(USER_LD) $(CRT0) $(OUT_DIR)/main.o \
	  $(NC_A) $(MUSL_LIB)/libc.a -lgcc -o $@
	@echo "Built $@"

clean:
	rm -rf $(OUT_DIR) $(TARGET)
