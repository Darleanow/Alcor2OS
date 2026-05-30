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
TESTCFLAGS := -std=gnu11 -g -O0 -Wall -Wextra -I$(INCLUDE) -I$(SRC) -I$(SUPPORT)
LDLIBS     := -lcmocka

# Suite registry: one entry per unit, with the sources it exercises.
SUITES := kstdlib gdt idt ktermios pixel_bpp
SRC_kstdlib   := $(SRC)/lib/kstdlib.c
SRC_gdt       := $(SRC)/arch/x86_64/gdt.c
SRC_idt       := $(SRC)/arch/x86_64/idt.c
SRC_ktermios  := $(SRC)/kernel/ktermios.c $(SRC)/lib/kstdlib.c
SRC_pixel_bpp := $(SRC)/kernel/drivers/fb_console/pixel.c

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

COV_SRCS := $(foreach s,$(SUITES),$(SRC_$(s)))
# llvm-cov takes one binary positionally and the rest via -object.
COV_BIN  := $(firstword $(TEST_BINS))
COV_REST := $(foreach t,$(filter-out $(COV_BIN),$(TEST_BINS)),-object $(t))

coverage: test
	$(LLVM_PROFDATA) merge -sparse $(COV)/*.profraw -o $(COV)/merged.profdata
	$(LLVM_COV) report $(COV_BIN) $(COV_REST) \
	  -instr-profile=$(COV)/merged.profdata $(COV_SRCS) | tee $(COV)/coverage.txt
	$(LLVM_COV) show $(COV_BIN) $(COV_REST) \
	  -instr-profile=$(COV)/merged.profdata \
	  -format=html -show-line-counts-or-regions -output-dir=$(COV)/html \
	  $(COV_SRCS)
	@echo "HTML report: $(COV)/html/index.html"

clean:
	rm -rf $(BUILD)
