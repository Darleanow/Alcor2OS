include ../../common.mk

ifeq ($(strip $(CXX)),)
$(error Need thirdparty/musl-cross/bin/x86_64-linux-musl-g++)
endif

NC_ROOT   := $(USER_BASE)/../thirdparty/ncurses-install/usr
NC_A      := $(NC_ROOT)/lib/libncursesw.a $(NC_ROOT)/lib/libtinfow.a
NCURSES_SUB := $(wildcard $(NC_ROOT)/include/ncurses)
NC_CFLAGS := -I$(NC_ROOT)/include $(if $(NCURSES_SUB),-I$(NC_ROOT)/include/ncurses)

ifeq ($(wildcard $(NC_A)),)
$(error fleed: missing ncurses libs - run from repo root: make ncurses)
endif

APP     := fleed
OUT_DIR := $(BUILD_DIR)/apps/$(APP)
SRCS    := $(wildcard src/*.cpp)
OBJS    := $(SRCS:%.cpp=$(OUT_DIR)/%.o)
TARGET  := $(BUILD_DIR)/apps/$(APP).elf

GR_A    := $(BUILD_DIR)/lib/libgrendizer.a
SPZ_A   := $(BUILD_DIR)/lib/libspazer.a

.PHONY: all clean

all: $(TARGET)

$(OUT_DIR):
	@mkdir -p $(OUT_DIR)

$(OUT_DIR)/%.o: %.cpp | $(OUT_DIR)
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -Iinclude $(NC_CFLAGS) -D_XOPEN_SOURCE_EXTENDED -c $< -o $@

$(TARGET): $(OBJS) $(CRT0) $(GR_A) $(SPZ_A) $(NC_A)
	$(CXX) -nostdlib -static -T $(USER_LD) $(CRT0) $(OBJS) \
	  $(GR_A) $(SPZ_A) $(NC_A) $(MUSL_LIB)/libc.a $(CXX_SHLIBS) -o $@
	@echo "Built $@"

clean:
	rm -rf $(OUT_DIR) $(TARGET)
