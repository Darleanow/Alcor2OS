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

SPZ_INC := -I$(USER_BASE)/sdk/spazer/include
SPZ_SRC := $(USER_BASE)/sdk/spazer/spazer.c

APP     := ncurses-hello
OUT_DIR := $(BUILD_DIR)/apps/$(APP)
TARGET  := $(BUILD_DIR)/apps/$(APP).elf

.PHONY: all clean

all: $(TARGET)

$(OUT_DIR):
	@mkdir -p $(OUT_DIR)

$(OUT_DIR)/spazer.o: $(SPZ_SRC) | $(OUT_DIR)
	$(CROSS) $(CFLAGS) $(NC_CFLAGS) $(SPZ_INC) -c $< -o $@

$(OUT_DIR)/main.o: main.c | $(OUT_DIR)
	$(CROSS) $(CFLAGS) $(NC_CFLAGS) $(SPZ_INC) -c $< -o $@

$(TARGET): $(OUT_DIR)/main.o $(OUT_DIR)/spazer.o $(CRT0) $(NC_A)
	$(CROSS) -nostdlib -static -T $(USER_LD) $(CRT0) \
	  $(OUT_DIR)/main.o $(OUT_DIR)/spazer.o \
	  $(NC_A) $(MUSL_LIB)/libc.a -lgcc -o $@
	@echo "Built $@"

clean:
	rm -rf $(OUT_DIR) $(TARGET)
