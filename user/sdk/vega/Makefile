include ../../common.mk

OUT_DIR    := $(BUILD_DIR)/sdk/vega
OUT_LIBDIR := $(BUILD_DIR)/lib
LIBA       := $(OUT_LIBDIR)/libvega.a

CORE_DIR   := $(BUILD_DIR)/core/vega
CORE_OBJS  := $(CORE_DIR)/ast.o $(CORE_DIR)/lexer.o $(CORE_DIR)/parse.o

CFLAGS  += -I$(USER_BASE)/sdk/vega/include \
           -I$(USER_BASE)/core/vega/include

SRCS := vega.c exec.c expand.c fntab.c builtin.c
OBJS := $(patsubst %.c,$(OUT_DIR)/%.o,$(SRCS))

HEADERS := \
  include/vega/vega.h \
  exec.h expand.h fntab.h builtin.h

.PHONY: all clean

all: $(LIBA)

$(OUT_DIR):
	@mkdir -p $(OUT_DIR)

$(OUT_LIBDIR):
	@mkdir -p $(OUT_LIBDIR)

$(OUT_DIR)/%.o: %.c $(HEADERS) | $(OUT_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

# Pulls core/vega objects into libvega.a — core/vega must be built first
# (mk/repo.mk's user target invokes them in order).
$(LIBA): $(OBJS) $(CORE_OBJS) | $(OUT_LIBDIR)
	$(AR) rcs $@ $(OBJS) $(CORE_OBJS)
	@echo "Built $@"

clean:
	rm -rf $(OUT_DIR) $(LIBA)
