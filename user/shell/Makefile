include ../common.mk

OUT_DIR := $(BUILD_DIR)/shell
TARGET  := $(OUT_DIR)/shell.elf
SRCS    := main.c sys.c str.c io.c builtin.c vega.c lexer.c ast.c parse.c exec.c expand.c fntab.c
OBJS    := $(patsubst %.c,$(OUT_DIR)/%.o,$(SRCS))
HEADERS := shell.h vega.h lexer.h ast.h parse.h exec.h expand.h fntab.h

.PHONY: all clean

all: $(TARGET)

$(OUT_DIR):
	mkdir -p $(OUT_DIR)

$(OUT_DIR)/%.o: %.c $(HEADERS) | $(OUT_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(TARGET): $(OBJS) $(CRT0)
	$(LD) $(LDFLAGS) $(CRT0) $(OBJS) $(LIBS) -o $@
	@echo "Built $@"

clean:
	rm -rf $(OUT_DIR)
