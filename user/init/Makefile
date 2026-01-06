# =============================================================================
# Alcor2 Init Makefile
# =============================================================================

include ../common.mk

OUT_DIR := $(BUILD_DIR)/init
TARGET  := $(OUT_DIR)/init.elf
OBJS    := $(OUT_DIR)/main.o

.PHONY: all clean

all: $(TARGET)

$(OUT_DIR):
	@mkdir -p $(OUT_DIR)

$(OUT_DIR)/%.o: %.c | $(OUT_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(TARGET): $(OBJS) $(CRT0)
	$(LD) $(LDFLAGS) $(CRT0) $(OBJS) $(LIBS) -o $@
	@echo "Built $@"

clean:
	rm -rf $(OUT_DIR)
