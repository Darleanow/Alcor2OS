# Host-compiled kernel unit tests: cmocka + clang source-based coverage.
# Each suite is unit/test_<unit>.c, linked against the kernel sources listed
# in SRC_<unit>. Sources build for the host so the tests actually run.

ROOT      := ..
INCLUDE   := $(ROOT)/include
SRC       := $(ROOT)/src
BUILD     := build
BIN       := $(BUILD)/bin
OBJ       := $(BUILD)/obj
COV       := $(BUILD)/coverage
SUPPORT   := support

# Coverage needs clang; gcc lacks -fprofile-instr-generate.
CC := clang

# Profile format is tied to clang's major version, so match the llvm tools to
# it (a newer llvm-profdata rejects an older profraw). Fall back to unsuffixed.
CLANG_VER := $(shell $(CC) -dumpversion 2>/dev/null | cut -d. -f1)
LLVM_PROFDATA ?= $(shell command -v llvm-profdata-$(CLANG_VER) 2>/dev/null || command -v llvm-profdata 2>/dev/null)
LLVM_COV      ?= $(shell command -v llvm-cov-$(CLANG_VER) 2>/dev/null || command -v llvm-cov 2>/dev/null)

# Kernel warning discipline, minus the bare-metal codegen flags so the code
# links and runs as a normal host program.
COV_FLAGS  := -fprofile-instr-generate -fcoverage-mapping
CFLAGS     := -std=gnu11 -g -O0 -Wall -Wextra -Werror -Wno-unused-parameter -I$(INCLUDE) -I$(SRC)
TESTCFLAGS := -std=gnu11 -g -O0 -Wall -Wextra -I$(INCLUDE) -I$(SRC) -I$(SUPPORT) -I$(ROOT)/user/include -I$(ROOT)/user/core/vega/include -I$(ROOT)/user/sdk/vega/include -I$(ROOT)/user/core/vega/include -I$(ROOT)/user/apps/shell/include
LDLIBS     := -lcmocka

# Suite registry: one entry per unit, with the sources it exercises.
SUITES := ext2_super ext2_dir vfs  ramfs  initfs  ext2_dir_block  ext2_alloc_rw  kstdlib gdt idt ktermios pixel_bpp ata keyboard klog console ext2_cache \
          mouse_ps2 pci pic pit \
          ext2_alloc ext2_path ext2_dirent \
          ext2_blockmap ext2_inode compiler_abi ext2_file pmm vmm heap proc \
          elf signal pipe \
          palette sgr sys_misc sys_mm \
          kbd_layout fb_console_caret_cell fb_console_pixel \
          grendizer echo cat ls wc bin_misc kbd \
          vega_fntab vega_expand vega_exec \
          sh_history sh_builtins \
          user_init
SRC_kstdlib   := $(SRC)/lib/kstdlib.c
SRC_fb_console_pixel := 

SRC_gdt       := $(SRC)/arch/x86_64/gdt.c
SRC_idt       := $(SRC)/arch/x86_64/idt.c
SRC_ktermios  := $(SRC)/kernel/ktermios.c $(SRC)/lib/kstdlib.c
SRC_pixel_bpp := $(SRC)/kernel/drivers/fb_console/pixel.c
SRC_ata       :=
SRC_keyboard  := $(SRC)/drivers/keyboard/keyboard.c
SRC_klog      :=
SRC_console    :=
SRC_ext2_cache := $(SRC)/fs/ext2/cache.c
SRC_mouse_ps2  :=
SRC_pci        :=
SRC_pic        :=
SRC_pit          :=
SRC_ext2_alloc   :=
SRC_ext2_path    := $(SRC)/lib/kstdlib.c
SRC_ext2_dirent  :=
SRC_ext2_blockmap :=
SRC_ext2_inode    :=
SRC_compiler_abi  :=
SRC_ext2_file     := $(SRC)/lib/kstdlib.c
SRC_ext2_super    := $(SRC)/lib/kstdlib.c
SRC_ext2_alloc_rw :=
SRC_ext2_dir_block :=
SRC_initfs :=
SRC_ramfs :=
SRC_vfs :=
SRC_vmm := $(SRC)/lib/kstdlib.c
SRC_heap := $(SRC)/lib/kstdlib.c
SRC_proc   := $(SRC)/lib/kstdlib.c
SRC_elf      :=
SRC_palette  :=
SRC_sgr      :=
SRC_sys_misc :=
SRC_sys_mm   :=
SRC_signal             :=
SRC_pipe               :=
SRC_kbd_layout         :=
SRC_fb_console_caret_cell :=
SRC_grendizer          :=
SRC_echo               :=
SRC_cat                :=
SRC_ls                 :=
SRC_wc                 :=
SRC_bin_misc           :=
SRC_kbd                :=
SRC_vega_fntab         :=
SRC_vega_expand        :=
SRC_vega_exec          :=
SRC_sh_history         :=
SRC_sh_builtins        :=
SRC_user_init          :=

TEST_BINS := $(foreach s,$(SUITES),$(BIN)/test_$(s))

.PHONY: all test coverage clean
all: test

# obj path for a kernel source: ../src/lib/x.c -> build/obj/src/lib/x.o
src_obj = $(patsubst $(SRC)/%.c,$(OBJ)/src/%.o,$(1))

# Generate per-suite link + object rules.
define SUITE_rule
$(BIN)/test_$(1): $(OBJ)/test_$(1).o $(call src_obj,$(SRC_$(1))) | $(BIN)
	$(CC) $(COV_FLAGS) $$^ $(LDLIBS) -o $$@
endef
$(foreach s,$(SUITES),$(eval $(call SUITE_rule,$(s))))

$(OBJ)/test_%.o: unit/test_%.c $(SUPPORT)/test_common.h | $(OBJ)
	$(CC) $(TESTCFLAGS) $(COV_FLAGS) -c $< -o $@

$(OBJ)/src/%.o: $(SRC)/%.c | $(OBJ)
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(COV_FLAGS) -c $< -o $@

$(BIN) $(OBJ) $(COV):
	@mkdir -p $@

test: $(TEST_BINS) | $(COV)
	@fail=0; \
	for t in $(TEST_BINS); do \
	  echo "--- $$(basename $$t) ---"; \
	  LLVM_PROFILE_FILE=$(COV)/$$(basename $$t).profraw $$t || fail=1; \
	done; \
	exit $$fail

COV_KERNEL_SRCS := $(shell find $(SRC) -name '*.c')
COV_USER_SRCS   := $(shell find $(ROOT)/user -name '*.c' ! -path '*/doomgeneric/*' ! -path '*/ncurses-hello/*' ! -path '*/thirdparty/*')
COV_BIN         := ../build/alcor2.elf
COV_TEST_BINS   := $(firstword $(TEST_BINS)) $(foreach t,$(filter-out $(firstword $(TEST_BINS)),$(TEST_BINS)),-object $(t))

../build/alcor2.elf:
	$(MAKE) -C .. build/alcor2.elf COV=1 CC=clang

coverage: test ../build/alcor2.elf
	$(LLVM_PROFDATA) merge -sparse $(COV)/*.profraw -o $(COV)/merged.profdata
	@echo ""
	@echo "==========================================================="
	@echo "               GLOBAL COVERAGE (KERNEL + USER)             "
	@echo "==========================================================="
	$(LLVM_COV) report $(COV_BIN) $(COV_TEST_BINS) \
	  -instr-profile=$(COV)/merged.profdata $(COV_KERNEL_SRCS) $(COV_USER_SRCS) | tee $(COV)/coverage.txt
	$(LLVM_COV) show $(COV_BIN) $(COV_TEST_BINS) \
	  -instr-profile=$(COV)/merged.profdata \
	  -format=html -show-line-counts-or-regions -output-dir=$(COV)/html \
	  $(COV_KERNEL_SRCS) $(COV_USER_SRCS)
	@echo ""
	@echo "HTML Report generated: $(COV)/html/index.html"

clean:
	rm -rf $(BUILD)
