# =============================================================================
# Alcor2 User CRT - Makefile
# =============================================================================

BUILD_DIR := ../build/crt

.PHONY: all clean

all: $(BUILD_DIR)/crt0.o

$(BUILD_DIR)/crt0.o: crt0.asm | $(BUILD_DIR)
	nasm -f elf64 $< -o $@

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

clean:
	rm -rf $(BUILD_DIR)
