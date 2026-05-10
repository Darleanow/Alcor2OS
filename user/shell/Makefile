include ../common.mk

OUT_DIR := $(BUILD_DIR)/shell
TARGET  := $(OUT_DIR)/shell.elf

FT_PRE := $(USER_BASE)/../thirdparty/freetype-install/usr
HB_PRE := $(USER_BASE)/../thirdparty/harfbuzz-install/usr
FT_LIB := $(FT_PRE)/lib/libfreetype.a
HB_LIB := $(HB_PRE)/lib/libharfbuzz.a

# Fb tty needs HarfBuzz (often static + C++ deps) and FreeType; match font-demo.
HAVE_FB_TTY := $(if $(and $(strip $(CXX)),$(wildcard $(FT_LIB)),$(wildcard $(HB_LIB))),1,)

CFLAGS += -I$(USER_BASE)/shell/include

BASE_SRCS := \
  main.c \
  vega.c \
  frontend/ast.c \
  frontend/lexer.c \
  frontend/parse.c \
  runtime/builtin.c \
  runtime/exec.c \
  runtime/expand.c \
  runtime/fntab.c \
  platform/io.c \
  platform/str.c \
  platform/sys.c

ifeq ($(HAVE_FB_TTY),1)
FB_TTY_SRC := platform/fb_tty.c
FB_TTY_OBJ := $(OUT_DIR)/platform/fb_tty.o
FONT_CFLAGS := $(filter-out -mno-sse -mno-sse2,$(CFLAGS)) -msse2 \
  -I$(FT_PRE)/include/freetype2 -I$(HB_PRE)/include/harfbuzz
else
FB_TTY_SRC := platform/fb_tty_stub.c
FB_TTY_OBJ := $(OUT_DIR)/platform/fb_tty_stub.o
endif

BASE_OBJS := $(patsubst %.c,$(OUT_DIR)/%.o,$(BASE_SRCS))
OBJS      := $(BASE_OBJS) $(FB_TTY_OBJ)

HEADERS := \
  include/vega/shell.h \
  include/vega/vega.h \
  include/vega/fb_tty.h \
  include/vega/frontend/ast.h \
  include/vega/frontend/lexer.h \
  include/vega/frontend/parse.h \
  include/vega/runtime/exec.h \
  include/vega/runtime/expand.h \
  include/vega/runtime/fntab.h

.PHONY: all clean

all: $(TARGET)

$(OUT_DIR):
	mkdir -p $(OUT_DIR)

$(BASE_OBJS): $(OUT_DIR)/%.o: %.c $(HEADERS) | $(OUT_DIR)
	@mkdir -p $(@D)
	$(CC) $(CFLAGS) -c $< -o $@

$(FB_TTY_OBJ): $(FB_TTY_SRC) $(HEADERS) | $(OUT_DIR)
	@mkdir -p $(@D)
	$(CC) $(if $(filter 1,$(HAVE_FB_TTY)),$(FONT_CFLAGS),$(CFLAGS)) -c $< -o $@

ifeq ($(HAVE_FB_TTY),1)
$(TARGET): $(OBJS) $(CRT0)
	$(CXX) -nostdlib -static -T $(USER_LD) $(CRT0) $(OBJS) \
	  -L$(HB_PRE)/lib -lharfbuzz -L$(FT_PRE)/lib -lfreetype -pthread -lm \
	  $(CXX_SHLIBS) -o $@
	@echo "Built $@"
else
$(TARGET): $(OBJS) $(CRT0)
	$(LD) $(LDFLAGS) $(CRT0) $(OBJS) $(LIBS) -o $@
	@echo "Built $@"
endif

clean:
	rm -rf $(OUT_DIR)
