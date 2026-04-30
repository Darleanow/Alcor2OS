include ../common.mk

CRT_BUILD := $(BUILD_DIR)/crt

.PHONY: all clean

all: $(CRT_BUILD)/crt0.o $(CRT_BUILD)/alcor2_stdio_tty.o

$(CRT_BUILD)/crt0.o: crt0.asm | $(CRT_BUILD)
	$(AS) $(ASFLAGS) $< -o $@

$(CRT_BUILD)/alcor2_stdio_tty.o: alcor2_stdio_tty.c | $(CRT_BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(CRT_BUILD):
	mkdir -p $@

clean:
	rm -rf $(CRT_BUILD)
