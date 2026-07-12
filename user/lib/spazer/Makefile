include ../../common.mk

NC_ROOT  := $(USER_BASE)/../thirdparty/ncurses-install/usr
NC_INC   := $(NC_ROOT)/include
NC_SUBD  := $(wildcard $(NC_ROOT)/include/ncurses)

ifeq ($(wildcard $(NC_ROOT)/lib/libncurses.a),)
$(warning spazer: ncurses not found — run `make ncurses` from repo root; skipping)
all clean:
	@true
else

OUT_DIR    := $(BUILD_DIR)/lib/spazer
OUT_LIBDIR := $(BUILD_DIR)/lib
LIBA       := $(OUT_LIBDIR)/libspazer.a

CFLAGS += \
  -I$(NC_INC) \
  $(if $(NC_SUBD),-I$(NC_SUBD))

SRCS    := init.c panel.c pad.c bar.c menu.c widgets.c input.c
OBJS    := $(patsubst %.c,$(OUT_DIR)/%.o,$(SRCS))
HEADERS := $(USER_BASE)/include/spazer/spazer.h internal.h

.PHONY: all clean

all: $(LIBA)

$(OUT_DIR):
	@mkdir -p $(OUT_DIR)

$(OUT_LIBDIR):
	@mkdir -p $(OUT_LIBDIR)

$(OUT_DIR)/%.o: %.c $(HEADERS) | $(OUT_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(LIBA): $(OBJS) | $(OUT_LIBDIR)
	$(AR) rcs $@ $(OBJS)
	@echo "Built $@"

clean:
	rm -rf $(OUT_DIR) $(LIBA)

endif
