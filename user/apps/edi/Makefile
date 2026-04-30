include ../../common.mk

ifeq ($(strip $(CXX)),)
$(error Need thirdparty/musl-cross/bin/x86_64-linux-musl-g++)
endif

APP     := edi
OUT_DIR := $(BUILD_DIR)/apps/$(APP)
SRCS    := src/main.cpp
OBJS    := $(SRCS:%.cpp=$(OUT_DIR)/%.o)
TARGET  := $(BUILD_DIR)/apps/$(APP).elf

.PHONY: all clean

all: $(TARGET)

$(OUT_DIR):
	@mkdir -p $(OUT_DIR)

$(OUT_DIR)/%.o: %.cpp | $(OUT_DIR)
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(TARGET): $(OBJS) $(CRT0)
	$(CXX) -nostdlib -static -T $(USER_LD) $(CRT0) $(OBJS) $(CXX_SHLIBS) -o $@
	@echo "Built $@"

clean:
	rm -rf $(OUT_DIR) $(TARGET)
