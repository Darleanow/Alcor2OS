# Userland, images, disk, QA

.PHONY: all help kernel user iso run disk disk-mount disk-umount \
        disk-populate disk-resync clean distclean format lint check qa

all: kernel compile_commands

help:
	@echo "Alcor2 — useful targets"
	@echo "  all (default)     kernel + compile_commands (clangd)"
	@echo "  kernel            link $(BUILD)/$(KERNEL) only"
	@echo "  user              userland (crt, init, shell, bin, apps if g++ exists)"
	@echo "  iso               Limine bootable CD image (+ kernel, shell on ISO)"
	@echo "  run               QEMU: Limine ISO + $(DISK) (IDE)"
	@echo "  disk-populate     fill $(DISK) (fuse2fs on Linux → no sudo when available); clang copy if built"
	@echo "  disk-mount / disk-umount   manual inspect of $(DISK)"
	@echo "  disk-resync       user + disk-populate"
	@echo "  make run USE_KVM=0   slower CPU emu (TCG); KVM itself does not use sudo"
	@echo "  musl | musl-cross | tcc | clang   bootstrap thirdparty/toolchains"
	@echo "  format lint check qa — static analysis / style"

kernel: $(BUILD)/$(KERNEL)

user: thirdparty/musl/install/lib/libc.a
	$(MAKE) -C user/crt
	$(MAKE) -C user/init
	$(MAKE) -C user/shell
	$(MAKE) -C user/bin
	@if [ -f thirdparty/musl-cross/bin/x86_64-linux-musl-g++ ]; then \
		$(MAKE) -C user/apps; \
	else \
		echo "[user] skipping user/apps (no musl-cross g++)"; \
	fi

iso: $(BUILD)/$(KERNEL) thirdparty/limine/limine user
	@rm -rf $(BUILD)/iso
	@mkdir -p $(BUILD)/iso/boot/limine $(BUILD)/iso/EFI/BOOT $(BUILD)/iso/bin
	@cp $(BUILD)/$(KERNEL) $(BUILD)/iso/boot/
	@cp user/build/shell/shell.elf $(BUILD)/iso/boot/
	@cp user/build/bin/*.elf $(BUILD)/iso/bin/ 2>/dev/null || true
	@cp user/build/apps/*.elf $(BUILD)/iso/bin/ 2>/dev/null || true
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
	@echo "$(BUILD)/$(ISO)"

disk: $(DISK)

$(DISK):
	@mkdir -p $(BUILD)
	@echo "ext2 $(DISK_SIZE) → $(DISK)"
	@dd if=/dev/zero of=$(DISK) bs=1M count=$$(echo $(DISK_SIZE) | sed 's/M//') 2>/dev/null
	@mke2fs -t ext2 -L ALCOR2 -q $(DISK)

disk-mount: $(DISK)
	@mkdir -p mnt
	@if mountpoint -q mnt 2>/dev/null; then \
	  echo >&2 "$(DISK) already mounted on mnt"; exit 1; \
	fi
	@img="$(CURDIR)/$(DISK)"; \
	if command -v fuse2fs >/dev/null 2>&1 && [ "$$(uname -s)" = Linux ]; then \
	  fuse2fs "$$img" mnt && echo "mnt ← fuse2fs $$img"; \
	else \
	  sudo mount -o loop "$$img" mnt && echo "mnt ← loop (needs sudo)"; \
	fi

disk-umount:
	@-fusermount -u mnt 2>/dev/null || sudo umount mnt 2>/dev/null || true
	@-rmdir mnt 2>/dev/null || true

disk-populate: $(DISK) user thirdparty/tcc-install/usr/bin/tcc
	@disk="$(CURDIR)/$(DISK)"; set -e; mkdir -p mnt; \
	if command -v fuse2fs >/dev/null 2>&1 && [ "$$(uname -s)" = Linux ]; then \
	  fuse2fs "$$disk" mnt; S=''; \
	else \
	  sudo mount -o loop "$$disk" mnt; S=sudo; \
	fi; \
	$$S mkdir -p mnt/bin mnt/etc mnt/tmp mnt/home \
		mnt/usr/bin mnt/usr/lib/tcc mnt/usr/include mnt/usr/lib; \
	rm -rf mnt/bin/*; \
	cp user/build/bin/*.elf mnt/bin/ 2>/dev/null || true; \
	cp user/build/apps/*.elf mnt/bin/ 2>/dev/null || true; \
	for f in mnt/bin/*.elf; do [ -f "$$f" ] && mv "$$f" "$${f%.elf}"; done; \
	$$S cp thirdparty/tcc-install/usr/bin/tcc mnt/bin/tcc; \
	$$S cp -r thirdparty/tcc-install/usr/lib/tcc/. mnt/usr/lib/tcc/; \
	$$S cp -r thirdparty/musl/install/include/. mnt/usr/include/; \
	$$S cp thirdparty/musl/install/lib/libc.a      mnt/usr/lib/libc.a; \
	$$S cp thirdparty/musl/install/lib/crt1.o      mnt/usr/lib/crt1.o; \
	$$S cp thirdparty/musl/install/lib/crti.o      mnt/usr/lib/crti.o; \
	$$S cp thirdparty/musl/install/lib/crtn.o      mnt/usr/lib/crtn.o; \
	if [ -n "$(strip $(CLANG_BIN))" ] && [ -f "$(CLANG_BIN)" ]; then \
		echo "installing Clang from $(CLANG_BIN)"; \
		MUSL_SYSROOT=$(CURDIR)/thirdparty/musl-cross/x86_64-linux-musl; \
		$$S mkdir -p mnt/usr/bin mnt/usr/lib/clang; \
		$$S cp "$(CLANG_BIN)" mnt/bin/clang; \
		$$S strip mnt/bin/clang 2>/dev/null || true; \
		$$S cp thirdparty/clang-install/usr/bin/lld mnt/bin/lld 2>/dev/null || true; \
		$$S strip mnt/bin/lld 2>/dev/null || true; \
		$$S cp -r thirdparty/clang-install/usr/lib/clang/. mnt/usr/lib/clang/ 2>/dev/null || true; \
		if [ -d "$$MUSL_SYSROOT" ]; then \
			$$S mkdir -p mnt/usr/include/c++ mnt/usr/lib; \
			$$S cp -r $$MUSL_SYSROOT/include/c++/. mnt/usr/include/c++/ 2>/dev/null || true; \
			$$S find $$MUSL_SYSROOT/lib -maxdepth 3 -name 'libstdc++.a'   -exec $$S cp {} mnt/usr/lib/libstdc++.a \;   2>/dev/null || true; \
			$$S find $$MUSL_SYSROOT/lib -maxdepth 3 -name 'libstdc++fs.a' -exec $$S cp {} mnt/usr/lib/libstdc++fs.a \; 2>/dev/null || true; \
			$$S find $$MUSL_SYSROOT/lib -maxdepth 3 -name 'libsupc++.a'   -exec $$S cp {} mnt/usr/lib/libsupc++.a \;   2>/dev/null || true; \
		fi; \
	else \
		echo "[disk] no static clang (optional: make clang)"; \
	fi; \
	echo "Welcome to Alcor2!" | $$S tee mnt/etc/motd >/dev/null; \
	$$S sync; \
	fusermount -u mnt 2>/dev/null || sudo umount mnt; \
	rmdir mnt

disk-resync: user disk-populate

run: iso disk-populate
	$(QEMU) -cdrom $(BUILD)/$(ISO) \
		-drive file=$(DISK),format=raw,if=ide,cache=writeback \
		-boot order=d -m $(QEMU_RAM) $(QEMU_KVM)

clean:
	rm -rf $(BUILD)
	-$(MAKE) -C user/init clean
	-$(MAKE) -C user/shell clean
	-$(MAKE) -C user/bin clean
	-$(MAKE) -C user/apps clean

distclean: clean
	rm -rf thirdparty $(DISK)

format:
	@find src include user \( -name '*.c' -o -name '*.h' \) ! -path '*/.cache/*' -print0 | xargs -0 clang-format -i

lint:
	clang-tidy $(KERNEL_SRCS_C) $(USER_SRCS_C) -- -I$(INCLUDE) -Ithirdparty/musl/install/include -std=gnu11

check:
	cppcheck --enable=all --suppress=missingIncludeSystem --inline-suppr --inconclusive --quiet \
		-I$(INCLUDE) $(SRC) user

qa: lint check
