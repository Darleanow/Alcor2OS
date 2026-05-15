include ../../common.mk

OUT_DIR    := $(BUILD_DIR)/lib/vega
OUT_LIBDIR := $(BUILD_DIR)/lib
LIBA       := $(OUT_LIBDIR)/libvega.a

CFLAGS  += -I$(USER_BASE)/lib/vega/include

SRCS := \
  src/vega.c \
  src/frontend/ast.c \
  src/frontend/lexer.c \
  src/frontend/parse.c \
  src/runtime/builtin.c \
  src/runtime/exec.c \
  src/runtime/expand.c \
  src/runtime/fntab.c

OBJS := $(patsubst src/%.c,$(OUT_DIR)/%.o,$(SRCS))

HEADERS := \
  include/vega/vega.h \
  include/vega/host.h \
  include/vega/frontend/ast.h \
  include/vega/frontend/lexer.h \
  include/vega/frontend/parse.h \
  include/vega/runtime/builtin.h \
  include/vega/runtime/exec.h \
  include/vega/runtime/expand.h \
  include/vega/runtime/fntab.h

.PHONY: all clean

all: $(LIBA)

$(OUT_DIR):
	@mkdir -p $(OUT_DIR)

$(OUT_LIBDIR):
	@mkdir -p $(OUT_LIBDIR)

$(OUT_DIR)/%.o: src/%.c $(HEADERS) | $(OUT_DIR)
	@mkdir -p $(@D)
	$(CC) $(CFLAGS) -c $< -o $@

$(LIBA): $(OBJS) | $(OUT_LIBDIR)
	$(AR) rcs $@ $(OBJS)
	@echo "Built $@"

clean:
	rm -rf $(OUT_DIR) $(LIBA)
