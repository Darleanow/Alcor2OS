include ../../common.mk

OUT_DIR := $(BUILD_DIR)/core/vega

CFLAGS  += -I$(USER_BASE)/core/vega/include

SRCS := ast.c lexer.c parse.c
OBJS := $(patsubst %.c,$(OUT_DIR)/%.o,$(SRCS))

HEADERS := \
  include/vega/ast.h \
  include/vega/parse.h \
  include/vega/internal/lexer.h

.PHONY: all clean

all: $(OBJS)

$(OUT_DIR):
	@mkdir -p $(OUT_DIR)

$(OUT_DIR)/%.o: %.c $(HEADERS) | $(OUT_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -rf $(OUT_DIR)
