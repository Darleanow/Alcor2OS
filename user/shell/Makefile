include ../common.mk

OUT_DIR := $(BUILD_DIR)/shell
TARGET  := $(OUT_DIR)/shell.elf

CFLAGS  += -I$(USER_BASE)/shell/include

SRCS := \
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

OBJS := $(patsubst %.c,$(OUT_DIR)/%.o,$(SRCS))

HEADERS := \
  include/vega/shell.h \
  include/vega/vega.h \
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

$(OUT_DIR)/%.o: %.c $(HEADERS) | $(OUT_DIR)
	@mkdir -p $(@D)
	$(CC) $(CFLAGS) -c $< -o $@

$(TARGET): $(OBJS) $(CRT0)
	$(LD) $(LDFLAGS) $(CRT0) $(OBJS) $(LIBS) -o $@
	@echo "Built $@"

clean:
	rm -rf $(OUT_DIR)
