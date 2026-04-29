KERNEL     := alcor2.elf
ISO        := alcor2.iso

BUILD      := build
SRC        := src
INCLUDE    := include

DISK       := disk.img
DISK_SIZE  := 1024M

UNAME := $(shell uname -s)
ifeq ($(UNAME), Darwin)
    CC := x86_64-elf-gcc
    LD := x86_64-elf-ld
else
    CC := gcc
    LD := ld
endif
AS := nasm

CFLAGS := -std=gnu11 -Wall -Wextra -Werror \
          -ffreestanding -fno-stack-protector -fno-stack-check \
          -fno-lto -fPIE -m64 -march=x86-64 \
          -mno-80387 -mno-mmx -mno-sse -mno-sse2 -mno-red-zone \
          -I$(INCLUDE) -MMD -MP

LDFLAGS := -nostdlib -static -pie --no-dynamic-linker \
           -z text -z max-page-size=0x1000 -T scripts/linker.ld

ASFLAGS := -f elf64

LIMINE_URL := https://github.com/limine-bootloader/limine.git
LIMINE_REV := v8.x-binary

MUSL_URL := https://git.musl-libc.org/cgit/musl/snapshot/musl-1.2.5.tar.gz
MUSL_VER := 1.2.5

TCC_URL := https://download.savannah.gnu.org/releases/tinycc/tcc-0.9.27.tar.bz2
TCC_VER := 0.9.27

# Clang: fully-static musl-linked binary so it runs natively on Alcor2.
# Requires musl-cross-make toolchain; takes 30-90 min and ~10 GB disk.
LLVM_VER       := 18.1.8
LLVM_URL       := https://github.com/llvm/llvm-project/releases/download/llvmorg-$(LLVM_VER)/llvm-project-$(LLVM_VER).src.tar.xz
MUSL_CROSS_URL := https://github.com/richfelker/musl-cross-make/archive/refs/heads/master.tar.gz

SRCS_C   := $(shell find $(SRC) -name '*.c')
SRCS_ASM := $(shell find $(SRC) -name '*.asm')
OBJS     := $(patsubst $(SRC)/%.c,$(BUILD)/%.c.o,$(SRCS_C)) \
            $(patsubst $(SRC)/%.asm,$(BUILD)/%.asm.o,$(SRCS_ASM))
DEPS     := $(OBJS:.o=.d)

.PHONY: all iso run run-disk disk disk-mount disk-umount disk-populate \
        compile_commands clean distclean user musl tcc clang format lint check analyze

all: $(BUILD)/$(KERNEL) compile_commands

# Accurate flags for clangd/IDE. Auto-generated after builds.
compile_commands: $(BUILD)/$(KERNEL)
	@echo '[' > compile_commands.json
	@for src in $(SRCS_C); do \
		echo '  {' >> compile_commands.json; \
		echo '    "directory": "$(shell pwd)",' >> compile_commands.json; \
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
	@sed -i '$$ s/,$$//' compile_commands.json  # Remove last comma
	@echo ']' >> compile_commands.json

musl:  thirdparty/musl/install/lib/libc.a
tcc:   thirdparty/tcc-install/usr/bin/tcc
clang: thirdparty/clang-install/usr/bin/clang

user: thirdparty/musl/install/lib/libc.a
	$(MAKE) -C user/crt
	$(MAKE) -C user/init
	$(MAKE) -C user/shell
	$(MAKE) -C user/bin

$(BUILD)/%.c.o: $(SRC)/%.c
	@mkdir -p $(@D)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/%.asm.o: $(SRC)/%.asm
	@mkdir -p $(@D)
	$(AS) $(ASFLAGS) $< -o $@

$(BUILD)/$(KERNEL): $(OBJS)
	@mkdir -p $(@D)
	$(LD) $(LDFLAGS) $^ -o $@

thirdparty/limine/limine:
	git clone $(LIMINE_URL) --branch=$(LIMINE_REV) --depth=1 thirdparty/limine
	$(MAKE) -C thirdparty/limine

thirdparty/musl/install/lib/libc.a:
	@echo "Downloading musl $(MUSL_VER)..."
	@mkdir -p thirdparty
	@curl -sL $(MUSL_URL) | tar xz -C thirdparty
	@mv thirdparty/musl-$(MUSL_VER) thirdparty/musl
	@echo "Configuring and building musl..."
	@cd thirdparty/musl && ./configure --prefix=$$(pwd)/install \
		--disable-shared CFLAGS='-Os -fno-stack-protector' >/dev/null
	@$(MAKE) -C thirdparty/musl -j$$(nproc) >/dev/null
	@$(MAKE) -C thirdparty/musl install >/dev/null
	@echo "musl $(MUSL_VER) installed"

thirdparty/tcc-install/usr/bin/tcc: thirdparty/musl/install/lib/libc.a
	@echo "Downloading TCC $(TCC_VER)..."
	@mkdir -p thirdparty
	@rm -rf thirdparty/tcc-src
	@curl -sL $(TCC_URL) | tar xj -C thirdparty
	@mv thirdparty/tcc-$(TCC_VER) thirdparty/tcc-src
	@echo "Building TCC $(TCC_VER)"
	@cd thirdparty/tcc-src && \
		./configure \
			--prefix=/usr \
			--cc=$(CURDIR)/thirdparty/musl/install/bin/musl-gcc \
			--extra-ldflags="-static" \
			--cpu=x86_64 \
			--config-musl \
			--sysincludepaths=$(CURDIR)/thirdparty/musl/install/include \
			>/dev/null 2>&1 && \
		make -j$$(nproc) 2>&1 | tail -5 && \
		make install DESTDIR=$(CURDIR)/thirdparty/tcc-install >/dev/null 2>&1
	@echo "TCC $(TCC_VER) built"

thirdparty/musl-cross/bin/x86_64-linux-musl-gcc:
	@echo "Building musl-cross-make toolchain"
	@mkdir -p thirdparty
	@rm -rf thirdparty/musl-cross-src
	@curl -sL $(MUSL_CROSS_URL) | tar xz -C thirdparty
	@mv thirdparty/musl-cross-make-master thirdparty/musl-cross-src
	@echo 'TARGET = x86_64-linux-musl'                                   >  thirdparty/musl-cross-src/config.mak
	@echo 'OUTPUT = $(CURDIR)/thirdparty/musl-cross'                     >> thirdparty/musl-cross-src/config.mak
	@echo 'COMMON_CONFIG += --disable-nls'                               >> thirdparty/musl-cross-src/config.mak
	@echo 'GCC_CONFIG += --enable-languages=c,c++ --disable-multilib'    >> thirdparty/musl-cross-src/config.mak
	@$(MAKE) -C thirdparty/musl-cross-src -j$$(nproc) install \
	  2>&1 | tee thirdparty/musl-cross-build.log | tail -10
	@test -f thirdparty/musl-cross/bin/x86_64-linux-musl-gcc || \
	  { echo "ERROR: musl-cross gcc not found — see thirdparty/musl-cross-build.log"; exit 1; }
	@echo "musl-cross toolchain built"

thirdparty/clang-install/usr/bin/clang: thirdparty/musl-cross/bin/x86_64-linux-musl-gcc
	@echo "Downloading LLVM $(LLVM_VER) source (~700 MB)..."
	@mkdir -p thirdparty
	@rm -rf thirdparty/llvm-src thirdparty/llvm-build
	@curl -L $(LLVM_URL) | tar xJ -C thirdparty
	@mv thirdparty/llvm-project-$(LLVM_VER).src thirdparty/llvm-src
	@echo "Configuring LLVM"
	@CROSS=$(CURDIR)/thirdparty/musl-cross/bin/x86_64-linux-musl; \
	mkdir -p thirdparty/llvm-build && \
	cmake -S thirdparty/llvm-src/llvm \
	  -B thirdparty/llvm-build \
	  -G Ninja \
	  -DCMAKE_BUILD_TYPE=MinSizeRel \
	  -DCMAKE_INSTALL_PREFIX=/usr \
	  -DCMAKE_C_COMPILER="$$CROSS-gcc" \
	  -DCMAKE_CXX_COMPILER="$$CROSS-g++" \
	  -DCMAKE_ASM_COMPILER="$$CROSS-gcc" \
	  -DCMAKE_AR="$$CROSS-ar" \
	  -DCMAKE_RANLIB="$$CROSS-ranlib" \
	  -DCMAKE_NM="$$CROSS-nm" \
	  -DCMAKE_STRIP="$$CROSS-strip" \
	  -DCMAKE_EXE_LINKER_FLAGS="-static" \
	  -DCMAKE_SHARED_LIBRARY_LINK_C_FLAGS="-static" \
	  -DLLVM_TARGETS_TO_BUILD="X86" \
	  -DLLVM_ENABLE_PROJECTS="clang;lld" \
	  -DLLVM_BUILD_STATIC=ON \
	  -DLLVM_ENABLE_ZLIB=OFF \
	  -DLLVM_ENABLE_ZSTD=OFF \
	  -DLLVM_ENABLE_LIBXML2=OFF \
	  -DLLVM_ENABLE_TERMINFO=OFF \
	  -DLLVM_ENABLE_LIBEDIT=OFF \
	  -DLLVM_ENABLE_ASSERTIONS=OFF \
	  -DLLVM_INCLUDE_TESTS=OFF \
	  -DLLVM_INCLUDE_EXAMPLES=OFF \
	  -DLLVM_INCLUDE_BENCHMARKS=OFF \
	  -DCLANG_ENABLE_ARCMT=OFF \
	  -DCLANG_ENABLE_STATIC_ANALYZER=OFF \
	  2>&1 | tail -10
	@echo "Building Clang"
	@cmake --build thirdparty/llvm-build --target clang lld -j$$(nproc) 2>&1 | tail -20
	@DESTDIR=$(CURDIR)/thirdparty/clang-install cmake --install thirdparty/llvm-build \
	  --component clang 2>&1 | tail -3
	@DESTDIR=$(CURDIR)/thirdparty/clang-install cmake --install thirdparty/llvm-build \
	  --component lld 2>&1 | tail -3
	@echo "Clang $(LLVM_VER) built for Alcor2"

iso: $(BUILD)/$(KERNEL) thirdparty/limine/limine user compile_commands
	@rm -rf $(BUILD)/iso
	@mkdir -p $(BUILD)/iso/boot/limine $(BUILD)/iso/EFI/BOOT $(BUILD)/iso/bin
	@cp $(BUILD)/$(KERNEL) $(BUILD)/iso/boot/
	@cp user/build/shell/shell.elf $(BUILD)/iso/boot/
	@cp user/build/bin/*.elf $(BUILD)/iso/bin/ 2>/dev/null || true
	@cp scripts/limine.conf $(BUILD)/iso/boot/limine/
	@cp thirdparty/limine/limine-bios.sys      $(BUILD)/iso/boot/limine/
	@cp thirdparty/limine/limine-bios-cd.bin   $(BUILD)/iso/boot/limine/
	@cp thirdparty/limine/limine-uefi-cd.bin   $(BUILD)/iso/boot/limine/
	@cp thirdparty/limine/BOOTX64.EFI          $(BUILD)/iso/EFI/BOOT/
	@cp thirdparty/limine/BOOTIA32.EFI         $(BUILD)/iso/EFI/BOOT/
	@xorriso -as mkisofs -b boot/limine/limine-bios-cd.bin \
		-no-emul-boot -boot-load-size 4 -boot-info-table \
		--efi-boot boot/limine/limine-uefi-cd.bin \
		-efi-boot-part --efi-boot-image --protective-msdos-label \
		$(BUILD)/iso -o $(BUILD)/$(ISO) 2>/dev/null
	@thirdparty/limine/limine bios-install $(BUILD)/$(ISO) 2>/dev/null
	@echo "Created $(BUILD)/$(ISO)"

run: iso
	qemu-system-x86_64 -cdrom $(BUILD)/$(ISO) -m 256M

# KVM requires root on most Linux systems (including WSL2).
run-disk: iso disk-populate
	sudo qemu-system-x86_64 \
		-cdrom $(BUILD)/$(ISO) \
		-drive file=$(DISK),format=raw,if=ide,cache=writeback \
		-boot d \
		-m 2G \
		-enable-kvm \
		-cpu host

disk: $(DISK)

$(DISK):
	@mkdir -p $(BUILD)
	@echo "Creating $(DISK_SIZE) ext2 disk image..."
	@dd if=/dev/zero of=$(DISK) bs=1M count=$$(echo $(DISK_SIZE) | sed 's/M//') 2>/dev/null
	@mke2fs -t ext2 -L "ALCOR2" -q $(DISK)
	@echo "Created $(DISK)"

disk-mount: $(DISK)
	@mkdir -p mnt
	@if mountpoint -q mnt 2>/dev/null; then \
	  echo >&2 "$(DISK) already mounted at ./mnt (run 'make disk-umount' when done)"; \
	else \
	  sudo mount -o loop $(DISK) mnt && \
	  echo "Mounted at ./mnt  —  run 'make disk-umount' when done"; \
	fi

disk-umount:
	@sudo umount mnt 2>/dev/null || true
	@rmdir mnt 2>/dev/null || true

disk-populate: $(DISK) user thirdparty/tcc-install/usr/bin/tcc
	@mkdir -p mnt
	@if mountpoint -q mnt 2>/dev/null; then \
	  : reused mount at ./mnt; \
	else \
	  sudo mount -o loop $(DISK) mnt; \
	fi
	@sudo mkdir -p mnt/bin mnt/etc mnt/tmp mnt/home \
		mnt/usr/bin mnt/usr/lib/tcc mnt/usr/include mnt/usr/lib
	@sudo cp user/build/bin/*.elf mnt/bin/ 2>/dev/null || true
	@for f in mnt/bin/*.elf; do \
		[ -f "$$f" ] && sudo mv "$$f" "$${f%.elf}"; \
	done
	@sudo cp thirdparty/tcc-install/usr/bin/tcc      mnt/bin/tcc
	@sudo cp -r thirdparty/tcc-install/usr/lib/tcc/. mnt/usr/lib/tcc/
	@sudo cp -r thirdparty/musl/install/include/.    mnt/usr/include/
	@sudo cp thirdparty/musl/install/lib/libc.a      mnt/usr/lib/libc.a
	@sudo cp thirdparty/musl/install/lib/crt1.o      mnt/usr/lib/crt1.o
	@sudo cp thirdparty/musl/install/lib/crti.o      mnt/usr/lib/crti.o
	@sudo cp thirdparty/musl/install/lib/crtn.o      mnt/usr/lib/crtn.o
	@if [ -f thirdparty/clang-install/usr/bin/clang ]; then \
		echo "Installing Clang $(LLVM_VER) onto disk..."; \
		MUSL_SYSROOT=$(CURDIR)/thirdparty/musl-cross/x86_64-linux-musl; \
		sudo mkdir -p mnt/usr/bin mnt/usr/lib/clang; \
		sudo cp thirdparty/clang-install/usr/bin/clang-18 mnt/bin/clang; \
		sudo strip mnt/bin/clang 2>/dev/null || true; \
		sudo cp thirdparty/clang-install/usr/bin/lld mnt/bin/lld 2>/dev/null || true; \
		sudo strip mnt/bin/lld 2>/dev/null || true; \
		sudo cp -r thirdparty/clang-install/usr/lib/clang/. mnt/usr/lib/clang/ 2>/dev/null || true; \
		if [ -d "$$MUSL_SYSROOT" ]; then \
			echo "Installing libstdc++ and C++ headers from musl-cross toolchain..."; \
			sudo mkdir -p mnt/usr/include/c++ mnt/usr/lib; \
			sudo cp -r $$MUSL_SYSROOT/include/c++/. mnt/usr/include/c++/ 2>/dev/null || true; \
			sudo find $$MUSL_SYSROOT/lib -name 'libstdc++.a'   -exec sudo cp {} mnt/usr/lib/libstdc++.a \;   2>/dev/null || true; \
			sudo find $$MUSL_SYSROOT/lib -name 'libstdc++fs.a' -exec sudo cp {} mnt/usr/lib/libstdc++fs.a \; 2>/dev/null || true; \
			sudo find $$MUSL_SYSROOT/lib -name 'libsupc++.a'   -exec sudo cp {} mnt/usr/lib/libsupc++.a \;   2>/dev/null || true; \
		fi; \
		echo "Clang $(LLVM_VER) + libstdc++ installed"; \
	else \
		echo "Clang not built — run 'make clang' first"; \
	fi
	@echo "Welcome to Alcor2!" | sudo tee mnt/etc/motd > /dev/null
	@sudo umount mnt
	@rmdir mnt
	@echo "Disk populated"

clean:
	rm -rf $(BUILD)
	$(MAKE) -C user/init clean
	$(MAKE) -C user/shell clean

distclean: clean
	rm -rf thirdparty disk.img

format:
	@find src include user -name '*.c' -o -name '*.h' | xargs clang-format -i

lint:
	clang-tidy $(SRCS_C) $(USER_SRCS_C) -- -I$(INCLUDE) -Ithirdparty/musl/install/include -std=gnu11

check:
	cppcheck --enable=all --suppress=missingIncludeSystem --inline-suppr --inconclusive --quiet -I$(INCLUDE) $(SRC) user

analyze: lint check

-include $(DEPS)
