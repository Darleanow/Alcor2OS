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
	@echo "  musl | musl-cross | tcc | clang | ncurses   bootstrap thirdparty/toolchains"
	@echo "  format lint check qa — static analysis / style"

kernel: $(BUILD)/$(KERNEL)

user: thirdparty/musl/$(MUSL_PREFIX)/lib/libc.a
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
	@$(MKE2FS) -t ext2 -L ALCOR2 -q $(DISK)

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

ifeq ($(UNAME), Darwin)
# Darwin: no loop-mount, no FUSE. Stage files in build/disk-root then
# format-and-populate the image in one mke2fs -d call. tcc & clang optional.
DISK_ROOT := $(BUILD)/disk-root

disk-populate: user
	@echo "stage → $(DISK_ROOT)"
	@rm -rf $(DISK_ROOT)
	@mkdir -p $(DISK_ROOT)/bin $(DISK_ROOT)/etc $(DISK_ROOT)/tmp $(DISK_ROOT)/home \
		$(DISK_ROOT)/usr/bin $(DISK_ROOT)/usr/lib/tcc \
		$(DISK_ROOT)/usr/include $(DISK_ROOT)/usr/lib
	@sh scripts/macos-disk-preserve.sh $(DISK) $(DISK_ROOT) || true
	@cp user/build/bin/*.elf  $(DISK_ROOT)/bin/ 2>/dev/null || true
	@cp user/build/apps/*.elf $(DISK_ROOT)/bin/ 2>/dev/null || true
	@for f in $(DISK_ROOT)/bin/*.elf; do [ -f "$$f" ] && mv "$$f" "$${f%.elf}"; done
	@if [ -f thirdparty/tcc-install/usr/bin/tcc ]; then \
		cp thirdparty/tcc-install/usr/bin/tcc $(DISK_ROOT)/bin/tcc; \
		cp -r thirdparty/tcc-install/usr/lib/tcc/. $(DISK_ROOT)/usr/lib/tcc/; \
	else \
		echo "[disk] no tcc on disk (optional: make tcc)"; \
	fi
	@cp -r thirdparty/musl/$(MUSL_PREFIX)/include/. $(DISK_ROOT)/usr/include/
	@cp thirdparty/musl/$(MUSL_PREFIX)/lib/libc.a $(DISK_ROOT)/usr/lib/libc.a
	@cp thirdparty/musl/$(MUSL_PREFIX)/lib/crt1.o $(DISK_ROOT)/usr/lib/crt1.o
	@cp thirdparty/musl/$(MUSL_PREFIX)/lib/crti.o $(DISK_ROOT)/usr/lib/crti.o
	@cp thirdparty/musl/$(MUSL_PREFIX)/lib/crtn.o $(DISK_ROOT)/usr/lib/crtn.o
	@if [ -f thirdparty/ncurses-install/usr/lib/libncurses.a ]; then \
		echo "[disk-root] ncurses"; \
		cp thirdparty/ncurses-install/usr/lib/libncurses.a $(DISK_ROOT)/usr/lib/; \
		cp thirdparty/ncurses-install/usr/lib/libtinfo.a $(DISK_ROOT)/usr/lib/; \
		cp -r thirdparty/ncurses-install/usr/include/. $(DISK_ROOT)/usr/include/; \
		mkdir -p $(DISK_ROOT)/usr/share/terminfo; \
		cp -r thirdparty/ncurses-install/usr/share/terminfo/. $(DISK_ROOT)/usr/share/terminfo/; \
	fi
	@if [ -n "$(strip $(CLANG_BIN))" ] && [ -f "$(CLANG_BIN)" ]; then \
		echo "installing Clang from $(CLANG_BIN)"; \
		MUSL_SYSROOT=$(CURDIR)/thirdparty/musl-cross/x86_64-linux-musl; \
		mkdir -p $(DISK_ROOT)/usr/lib/clang; \
		cp "$(CLANG_BIN)" $(DISK_ROOT)/bin/clang; \
		cp thirdparty/clang-install/usr/bin/lld $(DISK_ROOT)/bin/lld 2>/dev/null || true; \
		cp -r thirdparty/clang-install/usr/lib/clang/. $(DISK_ROOT)/usr/lib/clang/ 2>/dev/null || true; \
		if [ -d "$$MUSL_SYSROOT" ]; then \
			mkdir -p $(DISK_ROOT)/usr/include/c++; \
			cp -r $$MUSL_SYSROOT/include/c++/. $(DISK_ROOT)/usr/include/c++/ 2>/dev/null || true; \
			find $$MUSL_SYSROOT/lib -maxdepth 3 -name 'libstdc++.a'   -exec cp {} $(DISK_ROOT)/usr/lib/libstdc++.a \;   2>/dev/null || true; \
			find $$MUSL_SYSROOT/lib -maxdepth 3 -name 'libstdc++fs.a' -exec cp {} $(DISK_ROOT)/usr/lib/libstdc++fs.a \; 2>/dev/null || true; \
			find $$MUSL_SYSROOT/lib -maxdepth 3 -name 'libsupc++.a'   -exec cp {} $(DISK_ROOT)/usr/lib/libsupc++.a \;   2>/dev/null || true; \
		fi; \
	else \
		echo "[disk] no static clang (optional: make clang)"; \
	fi
	@echo "Welcome to Alcor2!" > $(DISK_ROOT)/etc/motd
	@echo "  tip : . /etc/profile   # TERM;  ncurses: cc ui.c -lncurses -ltinfo" >> $(DISK_ROOT)/etc/motd
	@echo 'export TERM="$${TERM:-xterm-256color}"' > $(DISK_ROOT)/etc/profile
	@mkdir -p $(BUILD)
	@echo "ext2 $(DISK_SIZE) → $(DISK) (populated)"
	@rm -f $(DISK)
	@dd if=/dev/zero of=$(DISK) bs=1M count=$$(echo $(DISK_SIZE) | sed 's/M//') 2>/dev/null
	@$(MKE2FS) -t ext2 -L ALCOR2 -d $(DISK_ROOT) -q $(DISK)
else
# Linux: single source of truth — scripts/disk-populate.sh installs
# /bin/clang.real + cc wrapper (see user/bin/cc.c). Do not duplicate logic here.
disk-populate: $(DISK) user thirdparty/tcc-install/usr/bin/tcc
	@set -e; mkdir -p mnt; \
	if command -v fuse2fs >/dev/null 2>&1 && [ "$$(uname -s)" = Linux ]; then \
	  fuse2fs "$(CURDIR)/$(DISK)" mnt; S=''; \
	else \
	  sudo mount -o loop "$(CURDIR)/$(DISK)" mnt; S=sudo; \
	fi; \
	trap 'fusermount -u mnt 2>/dev/null || sudo umount mnt 2>/dev/null; rmdir mnt 2>/dev/null' EXIT; \
	sh scripts/disk-populate.sh mnt "$$S"; \
	$$S sync
endif

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
	clang-tidy $(KERNEL_SRCS_C) $(USER_SRCS_C) -- -I$(INCLUDE) -Ithirdparty/musl/$(MUSL_PREFIX)/include -std=gnu11

check:
	cppcheck --enable=all --suppress=missingIncludeSystem --inline-suppr --inconclusive --quiet \
		-I$(INCLUDE) $(SRC) user

qa: lint check
