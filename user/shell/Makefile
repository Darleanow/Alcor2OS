# =============================================================================
# Alcor2 Shell Makefile
# =============================================================================

include ../common.mk

OUT_DIR := $(BUILD_DIR)/shell
TARGET  := $(OUT_DIR)/shell.elf
SRCS    := main.c sys.c str.c io.c parser.c builtin.c
OBJS    := $(patsubst %.c,$(OUT_DIR)/%.o,$(SRCS))

.PHONY: all clean

all: $(TARGET)

$(OUT_DIR):
	mkdir -p $(OUT_DIR)

$(OUT_DIR)/%.o: %.c shell.h | $(OUT_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(TARGET): $(OBJS) $(CRT0)
	$(LD) $(LDFLAGS) $(CRT0) $(OBJS) $(LIBS) -o $@
	@echo "Built $@"

clean:
	rm -rf $(OUT_DIR)
