include ../../common.mk

OUT_DIR := $(BUILD_DIR)/apps/vega
TARGET  := $(BUILD_DIR)/apps/vega.elf
LIBVEGA := $(BUILD_DIR)/lib/libvega.a

CFLAGS += -I$(USER_BASE)/sdk/vega/include

SRCS := main.c host.c
OBJS := $(patsubst %.c,$(OUT_DIR)/%.o,$(SRCS))

.PHONY: all clean

all: $(TARGET)

$(OUT_DIR):
	@mkdir -p $(OUT_DIR)

$(OUT_DIR)/%.o: %.c | $(OUT_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(TARGET): $(OBJS) $(LIBVEGA) $(CRT0)
	$(LD) $(LDFLAGS) $(CRT0) $(OBJS) $(LIBVEGA) $(LIBS) -o $@
	@echo "Built $@"

clean:
	rm -rf $(OUT_DIR) $(TARGET)
