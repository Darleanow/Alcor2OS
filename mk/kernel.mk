# Kernel object tree

ifeq ($(COV),1)
CFLAGS += -fprofile-instr-generate -fcoverage-mapping
LDFLAGS += --unresolved-symbols=ignore-all
endif

KERNEL_SRCS_C   := $(shell find $(SRC) -name '*.c')
KERNEL_SRCS_ASM := $(shell find $(SRC) -name '*.asm')

OBJS := $(patsubst $(SRC)/%.c,$(BUILD)/%.c.o,$(KERNEL_SRCS_C)) \
        $(patsubst $(SRC)/%.asm,$(BUILD)/%.asm.o,$(KERNEL_SRCS_ASM))
DEPS := $(OBJS:.o=.d)

# Each .c is compiled via ccache (when CCACHE=1) so that CI hits the object
# cache on unchanged files.  CCACHE_PREFIX is set in mk/config.mk.
$(BUILD)/%.c.o: $(SRC)/%.c
	@mkdir -p $(@D)
	$(CCACHE_PREFIX) $(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/%.asm.o: $(SRC)/%.asm
	@mkdir -p $(@D)
	$(AS) $(ASFLAGS) $< -o $@

$(BUILD)/$(KERNEL): $(OBJS)
	@mkdir -p $(@D)
	$(LD) $(LDFLAGS) $^ -o $@

# clangd database: wrap a full build through `bear` so the recorded flags are
# the exact ones the compilers see (kernel CC, user-space g++, ncurses include
# path, libstdc++ sysroot, ...). Forces a clean tree so every translation unit
# is recorded; partial rebuilds would miss the up-to-date .o files. Requires
# bear >= 3 on the host.
compile_commands:
	@command -v bear >/dev/null || { \
		echo >&2 "make: bear not found; install it (apt: bear) to refresh compile_commands.json"; \
		exit 1; \
	}
	$(MAKE) clean
	bear --output compile_commands.json -- $(MAKE) kernel user
