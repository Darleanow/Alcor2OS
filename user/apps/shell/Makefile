include ../../common.mk

OUT_DIR := $(BUILD_DIR)/apps/shell
TARGET  := $(BUILD_DIR)/apps/shell.elf
LIBVEGA := $(BUILD_DIR)/lib/libvega.a

FT_PRE := $(USER_BASE)/../thirdparty/freetype-install/usr
FT_LIB := $(FT_PRE)/lib/libfreetype.a

# Atlas submitter needs FreeType (rasterise Fira → kernel atlas). FreeType
# uses SSE2 scalar doubles, so we drop the freestanding `-mno-sse2` flag for
# atlas.c only.
HAVE_ATLAS := $(if $(wildcard $(FT_LIB)),1,)

# ncurses-driven line editor in main.c. Hard requirement: shell links libncurses
# + libtinfo for getch/keypad/KEY_*. Build via `make ncurses` from repo root.
NC_ROOT := $(USER_BASE)/../thirdparty/ncurses-install/usr
NC_LIBS := $(NC_ROOT)/lib/libncurses.a $(NC_ROOT)/lib/libtinfo.a
ifeq ($(wildcard $(NC_ROOT)/lib/libncurses.a),)
$(error shell: missing ncurses libs — run `make ncurses` from repo root)
endif
NCURSES_SUB := $(wildcard $(NC_ROOT)/include/ncurses)
NC_CFLAGS   := -I$(NC_ROOT)/include $(if $(NCURSES_SUB),-I$(NC_ROOT)/include/ncurses)

CFLAGS += -I$(USER_BASE)/apps/shell/include \
          -I$(USER_BASE)/sdk/vega/include \
          $(NC_CFLAGS)

BASE_SRCS := \
  main.c \
  platform/io.c \
  platform/sys.c \
  platform/builtins.c

ifeq ($(HAVE_ATLAS),1)
ATLAS_SRC := platform/atlas.c
ATLAS_OBJ := $(OUT_DIR)/platform/atlas.o
ATLAS_CFLAGS := $(filter-out -mno-sse -mno-sse2,$(CFLAGS)) -msse2 \
  -I$(FT_PRE)/include/freetype2
else
ATLAS_SRC := platform/atlas_stub.c
ATLAS_OBJ := $(OUT_DIR)/platform/atlas_stub.o
endif

BASE_OBJS := $(patsubst %.c,$(OUT_DIR)/%.o,$(BASE_SRCS))
OBJS      := $(BASE_OBJS) $(ATLAS_OBJ)

HEADERS := \
  include/shell/shell.h \
  include/shell/atlas.h

.PHONY: all clean

all: $(TARGET)

$(OUT_DIR):
	@mkdir -p $(OUT_DIR)

$(BASE_OBJS): $(OUT_DIR)/%.o: %.c $(HEADERS) | $(OUT_DIR)
	@mkdir -p $(@D)
	$(CC) $(CFLAGS) -c $< -o $@

$(ATLAS_OBJ): $(ATLAS_SRC) $(HEADERS) | $(OUT_DIR)
	@mkdir -p $(@D)
	$(CC) $(if $(filter 1,$(HAVE_ATLAS)),$(ATLAS_CFLAGS),$(CFLAGS)) -c $< -o $@

ifeq ($(HAVE_ATLAS),1)
$(TARGET): $(OBJS) $(LIBVEGA) $(CRT0) $(NC_LIBS)
	$(LD) $(LDFLAGS) $(CRT0) $(OBJS) $(LIBVEGA) \
	  -L$(FT_PRE)/lib -lfreetype $(NC_LIBS) $(LIBS) -o $@
	@echo "Built $@"
else
$(TARGET): $(OBJS) $(LIBVEGA) $(CRT0) $(NC_LIBS)
	$(LD) $(LDFLAGS) $(CRT0) $(OBJS) $(LIBVEGA) $(NC_LIBS) $(LIBS) -o $@
	@echo "Built $@"
endif

clean:
	rm -rf $(OUT_DIR) $(TARGET)
