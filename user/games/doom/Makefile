include ../../common.mk

# doomgeneric submodule: git submodule update --init user/games/doom/doomgeneric
DOOMGEN_DIR := doomgeneric/doomgeneric
DOOMGEN_HDR := $(DOOMGEN_DIR)/doomgeneric.h

ifeq ($(wildcard $(DOOMGEN_HDR)),)
$(error doom: doomgeneric submodule not found — run: git submodule update --init user/games/doom/doomgeneric)
endif

MUSL_CROSS_CC := $(USER_BASE)/../thirdparty/musl-cross/bin/x86_64-linux-musl-gcc
ifeq ($(wildcard $(MUSL_CROSS_CC)),)
$(error doom: missing musl-cross toolchain — run from repo root: make musl-cross)
endif

APP     := doom
OUT_DIR := $(BUILD_DIR)/games/$(APP)
TARGET  := $(OUT_DIR)/doom

# Engine sources (from Makefile.linuxvt). No backends; i_sound → i_sound_stub.c.
DOOMGEN_NAMES := \
  dummy am_map doomdef doomstat dstrings d_event d_items d_iwad d_loop    \
  d_main d_mode d_net f_finale f_wipe g_game hu_lib hu_stuff info          \
  i_cdmus i_endoom i_joystick i_scale i_system i_timer memio               \
  m_argv m_bbox m_cheat m_config m_controls m_fixed m_menu m_misc m_random \
  p_ceilng p_doors p_enemy p_floor p_inter p_lights p_map p_maputl p_mobj  \
  p_plats p_pspr p_saveg p_setup p_sight p_spec p_switch p_telept p_tick   \
  p_user r_bsp r_data r_draw r_main r_plane r_segs r_sky r_things sha1     \
  sounds statdump st_lib st_stuff s_sound tables v_video wi_stuff          \
  w_checksum w_file w_main w_wad z_zone w_file_stdc i_input i_video        \
  doomgeneric mus2mid

DOOMGEN_SRCS := $(patsubst %,$(DOOMGEN_DIR)/%.c,$(DOOMGEN_NAMES))
OUR_SRCS     := src/dg_alcor.c src/i_sound_stub.c

ALL_SRCS := $(DOOMGEN_SRCS) $(OUR_SRCS)
ALL_OBJS := $(patsubst %.c,$(OUT_DIR)/%.o,$(ALL_SRCS))

# Userspace has SSE; strip the kernel no-FPU/freestanding flags, keep the musl ABI.
DOOM_CFLAGS := $(filter-out \
    -mno-80387 -mno-mmx -mno-sse -mno-sse2 -mno-red-zone \
    -ffreestanding -fno-stack-protector -fno-stack-check \
    -Wall -Wextra, \
    $(CFLAGS)) \
  -msse2 \
  -I$(DOOMGEN_DIR) \
  -Isrc

# RESX/RESY=320×200 forces fb_scaling=1, so DG_DrawFrame is the only upscale
# pass (vs doomgeneric's 640×400 default that double-scales). Engine sources
# build with -w (legacy warnings); ours build with full diagnostics.
DOOMGEN_CFLAGS := $(DOOM_CFLAGS) -w \
  -DDOOMGENERIC_RESX=320 -DDOOMGENERIC_RESY=200
OUR_CFLAGS     := $(DOOM_CFLAGS) -Wall -Wextra \
  -DDOOMGENERIC_RESX=320 -DDOOMGENERIC_RESY=200

# Freedoom: free drop-in IWAD. `make doom-wad` fetches it into wads/.
FREEDOOM_VERSION ?= 0.13.0
FREEDOOM_URL     ?= https://github.com/freedoom/freedoom/releases/download/v$(FREEDOOM_VERSION)/freedoom-$(FREEDOOM_VERSION).zip
FREEDOOM_WAD     := wads/freedoom1.wad

.PHONY: all clean doom-wad

doom-wad: $(FREEDOOM_WAD)

$(FREEDOOM_WAD): | wads
	@set -e; Z='wads/.freedoom_dl.zip'; \
	echo "[doom] downloading Freedoom $(FREEDOOM_VERSION) …"; \
	curl -fsSL -o "$$Z" '$(FREEDOOM_URL)'; \
	M=$$(unzip -Z1 "$$Z" | grep -m1 -iE '(^|/)freedoom1\.wad$$'); \
	test -n "$$M"; \
	unzip -p "$$Z" "$$M" > $(FREEDOOM_WAD); \
	rm -f "$$Z"; \
	echo "[doom] $(FREEDOOM_WAD) ready — run: make disk-resync"

wads:
	@mkdir -p wads

all: $(TARGET)

$(OUT_DIR):
	@mkdir -p $(OUT_DIR)

# doomgeneric engine objects (warnings suppressed).
$(OUT_DIR)/$(DOOMGEN_DIR)/%.o: $(DOOMGEN_DIR)/%.c | $(OUT_DIR)
	@mkdir -p $(dir $@)
	$(MUSL_CROSS_CC) $(DOOMGEN_CFLAGS) -c $< -o $@

# Our platform objects (full diagnostics).
$(OUT_DIR)/src/%.o: src/%.c | $(OUT_DIR)
	@mkdir -p $(dir $@)
	$(MUSL_CROSS_CC) $(OUR_CFLAGS) -c $< -o $@

$(TARGET): $(ALL_OBJS) $(CRT0)
	$(LD) $(LDFLAGS) $(CRT0) $(ALL_OBJS) $(MUSL_LIB)/libc.a -o $@
	@echo "Built $@"

clean:
	rm -rf $(OUT_DIR) $(TARGET)

-include $(ALL_OBJS:.o=.d)
