include ../common.mk

OUT_LIBDIR := $(BUILD_DIR)/lib
LIBA       := $(OUT_LIBDIR)/libgrendizer.a

.PHONY: all clean

all: $(LIBA)

$(OUT_LIBDIR):
	@mkdir -p $@

# Floating-point parsing uses SSE2 scalar doubles (matches musl's ABI).
$(OUT_LIBDIR)/grendizer.o: grendizer.c | $(OUT_LIBDIR)
	$(CC) $(CFLAGS) -msse2 -I../include -c $< -o $@

$(LIBA): $(OUT_LIBDIR)/grendizer.o
	$(AR) rcs $@ $^
	@echo "Built $@"

clean:
	rm -f $(OUT_LIBDIR)/grendizer.o $(LIBA)
