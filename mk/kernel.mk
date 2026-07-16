# Kernel object tree + unified compile_commands (kernel .c + user/apps .cpp)

KERNEL_SRCS_C   := $(shell find $(SRC) -name '*.c')
KERNEL_SRCS_ASM := $(shell find $(SRC) -name '*.asm')

OBJS := $(patsubst $(SRC)/%.c,$(BUILD)/%.c.o,$(KERNEL_SRCS_C)) \
        $(patsubst $(SRC)/%.asm,$(BUILD)/%.asm.o,$(KERNEL_SRCS_ASM))
DEPS := $(OBJS:.o=.d)

# clangd extras (merged into root compile_commands.json)
USER_APPS_CPPS := $(shell find user/apps \( -path '*/.cache/*' \) -prune -o -name '*.cpp' -print 2>/dev/null | LC_ALL=C sort)
MUSL_CROSS_SYS := thirdparty/musl-cross/x86_64-linux-musl
LIBCXX_HDR     := $(firstword $(wildcard $(MUSL_CROSS_SYS)/include/c++/*))

# The kernel/userland ABI contract is owned by the AlcorMusl fork and staged
# into $(BUILD)/abi so kernel code can only reach the contract files, never
# the rest of the musl tree. cp -u keeps stage mtimes fresh when the fork
# changes, so the -MMD depfiles retrigger exactly the affected objects.
ABI_BITS_GENERIC := alcor_fb.h alcor_input.h alcor_console.h alcor_timer.h
.PHONY: abi-stage
abi-stage:
	@test -f thirdparty/musl-src/Makefile || \
	  git submodule update --init thirdparty/musl-src
	@case "$$(git submodule status thirdparty/musl-src 2>/dev/null)" in \
	  "+"*) echo >&2 "WARNING: thirdparty/musl-src checkout differs from the" \
	    "committed pin (run: git submodule update thirdparty/musl-src)";; \
	esac
	@mkdir -p $(BUILD)/abi/bits
	@for h in $(ABI_BITS_GENERIC); do \
	  cp -u thirdparty/musl-src/arch/generic/bits/$$h $(BUILD)/abi/bits/$$h; \
	done
	@cp -u thirdparty/musl-src/arch/x86_64/bits/alcor_syscall.h \
	  $(BUILD)/abi/bits/alcor_syscall.h
	@cp -u thirdparty/musl-src/arch/x86_64/bits/syscall.h.in \
	  $(BUILD)/abi/bits/syscall.h

# Each .c is compiled via ccache (when CCACHE=1) so that CI hits the object
# cache on unchanged files.  CCACHE_PREFIX is set in mk/config.mk.
$(BUILD)/%.c.o: $(SRC)/%.c | abi-stage
	@mkdir -p $(@D)
	$(CCACHE_PREFIX) $(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/%.asm.o: $(SRC)/%.asm
	@mkdir -p $(@D)
	$(AS) $(ASFLAGS) $< -o $@

$(BUILD)/$(KERNEL): $(OBJS)
	@mkdir -p $(@D)
	$(LD) $(LDFLAGS) $^ -o $@

USER_CC_FLAGS := -std=gnu11 -Wall -Wextra -Os -ffreestanding \
                 -fno-stack-protector -fno-stack-check -fno-lto \
                 -fno-PIC -fno-PIE -m64 -march=x86-64 -mno-red-zone \
                 -Ithirdparty/musl/$(MUSL_PREFIX)/include \
                 -Iuser/include -I$(INCLUDE) \
                 -Iuser/sdk/spazer/include \
                 -Iuser/sdk/vega/include \
                 -Iuser/core/vega/include \
                 -Iuser/apps/shell/include \
                 -DALCOR2_VERSION=\"$(GIT_VERSION)\"

compile_commands: $(OBJS)
	@echo '[' > compile_commands.json
	@for src in $(KERNEL_SRCS_C); do \
		echo '  {' >> compile_commands.json; \
		echo '    "directory": "$(CURDIR)",' >> compile_commands.json; \
		echo '    "arguments": [' >> compile_commands.json; \
		echo '      "$(CC)",' >> compile_commands.json; \
		for flag in $(CFLAGS); do \
			echo '      "'"$$flag"'",' >> compile_commands.json; \
		done; \
		echo '      "-c",' >> compile_commands.json; \
		echo '      "'"$$src"'",' >> compile_commands.json; \
		echo '      "-o",' >> compile_commands.json; \
		obj=$$(echo $$src | sed 's|$(SRC)|$(BUILD)|; s|\.c$$|\.c\.o|'); \
		echo '      "'"$$obj"'"' >> compile_commands.json; \
		echo '    ],' >> compile_commands.json; \
		echo '    "file": "'"$$src"'"' >> compile_commands.json; \
		echo '  },' >> compile_commands.json; \
	done
	@if [ -n "$(strip $(USER_SRCS_C))" ]; then \
		for src in $(USER_SRCS_C); do \
			echo '  {' >> compile_commands.json; \
			echo '    "directory": "$(CURDIR)",' >> compile_commands.json; \
			echo '    "arguments": [' >> compile_commands.json; \
			echo '      "$(CC)",' >> compile_commands.json; \
			for flag in $(USER_CC_FLAGS); do \
				echo '      "'"$$flag"'",' >> compile_commands.json; \
			done; \
			echo '      "-c",' >> compile_commands.json; \
			echo '      "'"$$src"'"' >> compile_commands.json; \
			echo '    ],' >> compile_commands.json; \
			echo '    "file": "'"$$src"'"' >> compile_commands.json; \
			echo '  },' >> compile_commands.json; \
		done; \
	fi
	@if [ -n "$(strip $(USER_APPS_CPPS))" ]; then \
		for cpp in $(USER_APPS_CPPS); do \
			echo '  {' >> compile_commands.json; \
			echo '    "directory": "$(CURDIR)",' >> compile_commands.json; \
			echo '    "arguments": [' >> compile_commands.json; \
			echo '      "clang++",' >> compile_commands.json; \
			echo '      "-std=gnu++17",' >> compile_commands.json; \
			echo '      "--target=x86_64-linux-musl",' >> compile_commands.json; \
			echo '      "-nostdlibinc",' >> compile_commands.json; \
			echo '      "-isystem",' >> compile_commands.json; \
			echo '      "'"$(LIBCXX_HDR)"'",' >> compile_commands.json; \
			echo '      "-isystem",' >> compile_commands.json; \
			echo '      "'"$(LIBCXX_HDR)/x86_64-linux-musl"'",' >> compile_commands.json; \
			echo '      "-isystem",' >> compile_commands.json; \
			echo '      "'"$(MUSL_CROSS_SYS)/include"'",' >> compile_commands.json; \
			echo '      "-isystem",' >> compile_commands.json; \
			echo '      "'"thirdparty/musl/$(MUSL_PREFIX)/include"'",' >> compile_commands.json; \
			echo '      "-isystem",' >> compile_commands.json; \
			echo '      "'"$(INCLUDE)"'",' >> compile_commands.json; \
			echo '      "-Wall",' >> compile_commands.json; \
			echo '      "-Wextra",' >> compile_commands.json; \
			echo '      "-O2",' >> compile_commands.json; \
			echo '      "-fno-stack-protector",' >> compile_commands.json; \
			echo '      "-fno-stack-check",' >> compile_commands.json; \
			echo '      "-fno-lto",' >> compile_commands.json; \
			echo '      "-fno-PIC",' >> compile_commands.json; \
			echo '      "-fno-PIE",' >> compile_commands.json; \
			echo '      "-m64",' >> compile_commands.json; \
			echo '      "-march=x86-64",' >> compile_commands.json; \
			echo '      "-mno-red-zone",' >> compile_commands.json; \
			echo '      "-c",' >> compile_commands.json; \
			echo '      "'"$$cpp"'"' >> compile_commands.json; \
			echo '    ],' >> compile_commands.json; \
			echo '    "file": "'"$$cpp"'"' >> compile_commands.json; \
			echo '  },' >> compile_commands.json; \
		done; \
	fi
	@sed -i '$$ s/,$$//' compile_commands.json
	@echo ']' >> compile_commands.json
