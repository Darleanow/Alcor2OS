include ../../common.mk

ifeq ($(strip $(CXX)),)
$(error font-demo: need thirdparty/musl-cross/bin/x86_64-linux-musl-g++)
endif

CROSS  := $(USER_BASE)/../thirdparty/musl-cross/bin/x86_64-linux-musl-gcc
FT_PRE  := $(USER_BASE)/../thirdparty/freetype-install/usr
HB_PRE  := $(USER_BASE)/../thirdparty/harfbuzz-install/usr
FT_LIB  := $(FT_PRE)/lib/libfreetype.a
HB_LIB  := $(HB_PRE)/lib/libharfbuzz.a

# TTF is not in the Fira Code git tree; fetch the official release zip.
FIRA_ZIP_URL ?= https://github.com/tonsky/FiraCode/releases/download/6.2/Fira_Code_v6.2.zip

APP       := font-demo
OUT_DIR   := $(BUILD_DIR)/apps/$(APP)
TARGET    := $(BUILD_DIR)/apps/$(APP).elf
FONT_FIRA := $(OUT_DIR)/FiraCode-Regular.ttf

ifeq ($(wildcard $(CROSS)),)
$(error font-demo: missing $(CROSS) — run: make musl-cross)
endif
ifeq ($(wildcard $(FT_LIB)),)
$(error font-demo: missing FreeType — run from repo root: make freetype)
endif
ifeq ($(wildcard $(HB_LIB)),)
$(error font-demo: missing HarfBuzz — run from repo root: make harfbuzz)
endif

# C++ + HarfBuzz use SSE for x86_64 float ABI.
FONT_CXXFLAGS := $(filter-out -mno-sse -mno-sse2,$(CXXFLAGS)) -msse2 \
  -I$(FT_PRE)/include/freetype2 -I$(HB_PRE)/include/harfbuzz

.PHONY: all clean fetch-fonts

all: fetch-fonts $(TARGET)

fetch-fonts: $(FONT_FIRA)

$(OUT_DIR):
	@mkdir -p $(OUT_DIR)

$(FONT_FIRA): | $(OUT_DIR)
	@set -e; Z='$(OUT_DIR)/.fira_code_dl.zip'; \
	curl -fsSL -o "$$Z" '$(FIRA_ZIP_URL)'; \
	M=$$(unzip -Z1 "$$Z" | grep -m1 -E '(^|/)FiraCode-Regular\.ttf$$'); \
	test -n "$$M"; \
	unzip -p "$$Z" "$$M" > $@; \
	rm -f "$$Z"

$(OUT_DIR)/main.o: main.cpp $(HB_LIB) $(FT_LIB) | $(OUT_DIR)
	$(CXX) $(FONT_CXXFLAGS) -c main.cpp -o $@

$(TARGET): $(OUT_DIR)/main.o $(CRT0)
	$(CXX) -nostdlib -static -T $(USER_LD) $(CRT0) $(OUT_DIR)/main.o \
	  -L$(HB_PRE)/lib -lharfbuzz -L$(FT_PRE)/lib -lfreetype -pthread -lm \
	  $(CXX_SHLIBS) -o $@
	@echo "Built $@"

clean:
	rm -rf $(OUT_DIR) $(TARGET)
	rm -f $(OUT_DIR)/.fira_code_dl.zip
