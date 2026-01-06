# =============================================================================
# Alcor2 User Binaries Makefile
# =============================================================================

include ../common.mk

OUT_DIR := $(BUILD_DIR)/bin
BINS    := ls cat pwd mkdir touch rm
TARGETS := $(patsubst %,$(OUT_DIR)/%.elf,$(BINS))

.PHONY: all clean

all: $(TARGETS)

$(OUT_DIR):
	@mkdir -p $(OUT_DIR)

$(OUT_DIR)/%.elf: %.c $(CRT0) | $(OUT_DIR)
	$(CC) $(CFLAGS) -c $< -o $(OUT_DIR)/$*.o
	$(LD) $(LDFLAGS) $(CRT0) $(OUT_DIR)/$*.o $(LIBS) -o $@
	@echo "Built $@"

clean:
	rm -rf $(OUT_DIR)
