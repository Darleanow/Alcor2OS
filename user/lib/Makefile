include ../common.mk

OUT_LIBDIR := $(BUILD_DIR)/lib
LIBGRENDIZER := $(OUT_LIBDIR)/libgrendizer.a
LIBVEGA      := $(OUT_LIBDIR)/libvega.a

.PHONY: all clean grendizer vega

all: grendizer vega

$(OUT_LIBDIR):
	@mkdir -p $@

# Floating-point parsing uses SSE2 scalar doubles (matches musl's ABI).
$(OUT_LIBDIR)/grendizer.o: grendizer.c | $(OUT_LIBDIR)
	$(CC) $(CFLAGS) -msse2 -I../include -c $< -o $@

grendizer: $(LIBGRENDIZER)

$(LIBGRENDIZER): $(OUT_LIBDIR)/grendizer.o
	$(AR) rcs $@ $^
	@echo "Built $@"

vega:
	$(MAKE) -C vega

clean:
	rm -f $(OUT_LIBDIR)/grendizer.o $(LIBGRENDIZER)
	-$(MAKE) -C vega clean
