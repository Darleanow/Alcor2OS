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

$(BUILD)/%.c.o: $(SRC)/%.c
	@mkdir -p $(@D)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/%.asm.o: $(SRC)/%.asm
	@mkdir -p $(@D)
	$(AS) $(ASFLAGS) $< -o $@

$(BUILD)/$(KERNEL): $(OBJS)
	@mkdir -p $(@D)
	$(LD) $(LDFLAGS) $^ -o $@

# Regenerate IDE DB after compiling sources.
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
			echo '      "-Os",' >> compile_commands.json; \
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
