# Userland, images, disk, QA

.PHONY: all help kernel user iso iso-kernel run run-trace debug disk disk-mount disk-umount \
        disk-populate disk-resync disk-quick clean clean-all distclean \
        format fmt lint check qa

all: kernel compile_commands

help:
	@echo "Alcor2 $(GIT_VERSION) — useful targets"
	@echo ""
	@echo "  Build"
	@echo "    all (default)   kernel + compile_commands.json (clangd)"
	@echo "    kernel          link $(BUILD)/$(KERNEL) only"
	@echo "    iso-kernel      bootable ISO, kernel only — fastest CI check"
	@echo "    user            userland: crt, init, shell, bin, apps"
	@echo "    iso             full Limine ISO (kernel + userland)"
	@echo ""
	@echo "  Run"
	@echo "    run             build + boot in QEMU  (KVM auto-detected)"
	@echo "    run USE_KVM=0   force TCG (no acceleration)"
	@echo "    run USE_KVM=1   force KVM"
	@echo "    run-trace       build + boot with kernel log + syscall trace to host stdio"
	@echo "    debug           build + boot with GDB server on :1234 (VM paused)"
	@echo ""
	@echo "  Disk"
	@echo "    disk-populate   full disk rebuild (dd + mke2fs + stage)"
	@echo "    disk-resync     user + disk-populate"
	@echo "    disk-quick      fast sync of user binaries to existing disk (Linux, fuse2fs)"
	@echo "    disk-mount / disk-umount   manual inspect of $(DISK)"
	@echo ""
	@echo "  Toolchain"
	@echo "    toolchain       bootstrap ncurses + clang (~1 h first run)"
	@echo "    musl | musl-cross | clang | ncurses | freetype | harfbuzz"
	@echo ""
	@echo "  Quality"
	@echo "    format / fmt    clang-format all sources in-place"
	@echo "    lint            clang-tidy"
	@echo "    check           cppcheck"
	@echo "    qa              lint + check"
	@echo ""
	@echo "  Clean"
	@echo "    clean           remove build/"
	@echo "    clean-all       clean + remove all thirdparty installs"
	@echo "    distclean       clean-all + remove $(DISK)"

kernel: $(BUILD)/$(KERNEL)

user: thirdparty/musl/$(MUSL_PREFIX)/lib/libc.a
	$(MAKE) -C user/crt
	$(MAKE) -C user/lib
	$(MAKE) -C user/core/vega
	$(MAKE) -C user/sdk/vega
	$(MAKE) -C user/lib/spazer
	$(MAKE) -C user/init
	$(MAKE) -C user/apps/shell
	$(MAKE) -C user/apps/vega
	$(MAKE) -C user/bin
	@if [ -f thirdparty/musl-cross/bin/x86_64-linux-musl-g++ ]; then \
		$(MAKE) -C user/apps; \
	else \
		echo "[user] skipping user/apps (no musl-cross g++)"; \
	fi
	@if [ -f thirdparty/musl-cross/bin/x86_64-linux-musl-gcc ]; then \
		$(MAKE) -C user/games; \
	else \
		echo "[user] skipping user/games (no musl-cross gcc)"; \
	fi

# iso-kernel: fastest path used by CI on every push/PR.
# Produces a bootable ISO with the kernel only — no user/ binaries required.
iso-kernel: $(BUILD)/$(KERNEL) thirdparty/limine/limine
	@rm -rf $(BUILD)/iso
	@mkdir -p $(BUILD)/iso/boot/limine $(BUILD)/iso/EFI/BOOT
	@cp $(BUILD)/$(KERNEL) $(BUILD)/iso/boot/
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
	@echo "$(BUILD)/$(ISO) [kernel-only]"

iso: toolchain $(BUILD)/$(KERNEL) thirdparty/limine/limine user
	@rm -rf $(BUILD)/iso
	@mkdir -p $(BUILD)/iso/boot/limine $(BUILD)/iso/boot/bin $(BUILD)/iso/EFI/BOOT
	@cp $(BUILD)/$(KERNEL) $(BUILD)/iso/boot/
	@cp user/build/apps/shell.elf $(BUILD)/iso/boot/ 2>/dev/null || true
	@cp user/build/bin/*.elf $(BUILD)/iso/boot/bin/ 2>/dev/null || true
	@cp scripts/limine.conf $(BUILD)/iso/boot/limine/
	@printf '\n' >> $(BUILD)/iso/boot/limine/limine.conf
	@for f in $(BUILD)/iso/boot/bin/*.elf; do \
		[ -f "$$f" ] || continue; \
		echo "    module_path: boot():/boot/bin/$$(basename $$f)" \
			>> $(BUILD)/iso/boot/limine/limine.conf; \
	done
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
	@if mountpoint -q mnt 2>/dev/null; then \
	  echo "[disk] stale mount on mnt — unmounting before format"; \
	  fusermount -u mnt 2>/dev/null || umount mnt 2>/dev/null || true; \
	  rmdir mnt 2>/dev/null || true; \
	fi
	@dd if=/dev/zero of=$(DISK) bs=1M count=$$(echo $(DISK_SIZE) | sed 's/M//') 2>/dev/null
	@$(MKE2FS) -F -t ext2 -E root_owner=$$(id -u):$$(id -g) -L ALCOR2 -q $(DISK)

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
		$(DISK_ROOT)/usr/bin \
		$(DISK_ROOT)/usr/include $(DISK_ROOT)/usr/lib
	@sh scripts/macos-disk-preserve.sh $(DISK) $(DISK_ROOT) || true
	@# user/build/bin/*.elf ride along in the ISO as Limine modules → /init
	@# overlay. Only the heavier apps live on the persistent disk.
	@cp user/build/apps/*.elf $(DISK_ROOT)/bin/ 2>/dev/null || true
	@for f in $(DISK_ROOT)/bin/*.elf; do [ -f "$$f" ] && mv "$$f" "$${f%.elf}"; done

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
		if [ -d thirdparty/ncurses-install/usr/share/terminfo ] && \
		   find thirdparty/ncurses-install/usr/share/terminfo -type f -print -quit 2>/dev/null | grep -q .; then \
			cp -r thirdparty/ncurses-install/usr/share/terminfo/. $(DISK_ROOT)/usr/share/terminfo/; \
		else \
			echo "[disk-root] WARN: thirdparty/ncurses-install/usr/share/terminfo empty — use disk-populate.sh or: rm -rf thirdparty/ncurses-install && make ncurses"; \
		fi; \
		for n in xterm-256color vt100; do \
		  if ! find $(DISK_ROOT)/usr/share/terminfo -name "$$n" -print -quit 2>/dev/null | grep -q .; then \
		    f=$$(find /usr/share/terminfo /lib/terminfo -name "$$n" -type f 2>/dev/null | head -1); \
		    if [ -n "$$f" ]; then \
		      b=$$(basename $$(dirname "$$f")); \
		      mkdir -p "$(DISK_ROOT)/usr/share/terminfo/$$b"; \
		      cp "$$f" "$(DISK_ROOT)/usr/share/terminfo/$$b/"; \
		      echo "[disk-root] terminfo fallback: $$n from $$f"; \
		    fi; \
		  fi; \
		done; \
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
	@echo 'export TERM="${TERM:-xterm-256color}"' > $(DISK_ROOT)/etc/profile
	@mkdir -p $(BUILD)
	@echo "ext2 $(DISK_SIZE) → $(DISK) (populated)"
	@rm -f $(DISK)
	@dd if=/dev/zero of=$(DISK) bs=1M count=$$(echo $(DISK_SIZE) | sed 's/M//') 2>/dev/null
	@$(MKE2FS) -t ext2 -E root_owner=$$(id -u):$$(id -g) -L ALCOR2 -d $(DISK_ROOT) -q $(DISK)
else
# Linux: stage into build/disk-root, then mke2fs -d — no fuse2fs, no mount.
DISK_ROOT := $(BUILD)/disk-root

disk-populate: user
	@echo "staging → $(DISK_ROOT)"
	@rm -rf $(DISK_ROOT)
	@mkdir -p \
		$(DISK_ROOT)/bin          $(DISK_ROOT)/etc          $(DISK_ROOT)/tmp \
		$(DISK_ROOT)/home         $(DISK_ROOT)/usr/bin      \
		$(DISK_ROOT)/usr/include  $(DISK_ROOT)/usr/lib      \
		$(DISK_ROOT)/usr/lib/clang
	@sh scripts/disk-populate.sh "$(DISK_ROOT)" ""
	@echo "ext2 $(DISK_SIZE) → $(DISK) (populated)"
	@rm -f $(DISK)
	@dd if=/dev/zero of=$(DISK) bs=1M count=$$(echo $(DISK_SIZE) | sed 's/M//') 2>/dev/null
	@$(MKE2FS) -F -t ext2 -E root_owner=$$(id -u):$$(id -g) -L ALCOR2 -d $(DISK_ROOT) -q $(DISK)
endif

disk-resync: user disk-populate

# Mouse is the emulated i8042 PS/2 controller (always present). Grab with
# Ctrl+Alt+G or fullscreen.

# Flipping SYS_TRACE only changes a -D in CFLAGS, which make's timestamp
# logic doesn't notice. Force sys_dispatch.c to be reconsidered on every
# run/run-trace so the build matches the requested mode.
SYS_TRACE_FORCE := -W $(SRC)/kernel/sys/sys_dispatch.c

run:
	@$(MAKE) $(SYS_TRACE_FORCE) iso SYS_TRACE=0
	@if [ ! -f $(DISK) ]; then \
		echo "[run] $(DISK) missing — staging first-run disk."; \
		$(MAKE) disk-populate; \
	fi
	$(QEMU_ENV) $(QEMU) -cdrom $(BUILD)/$(ISO) \
		-drive file=$(DISK),format=raw,if=ide,cache=writeback \
		-boot order=d -m $(QEMU_RAM) $(QEMU_KVM) \
		$(if $(QEMU_DISPLAY),-display $(QEMU_DISPLAY)) \
		$(QEMU_EXTRA)

# Rebuilds the kernel with SYS_TRACE=1 and boots with QEMU's debugcon backend
# wired to host stdio. The host terminal gets both the boot logger (kernel's
# [INIT] / [ELF] / [PROC] lines via console_print mirroring) AND a per-syscall
# trace (name, args, return value via klogf). The framebuffer stays clean for
# the shell — syscall traces never touch it.
run-trace:
	@$(MAKE) $(SYS_TRACE_FORCE) iso SYS_TRACE=1
	@if [ ! -f $(DISK) ]; then \
		echo "[run-trace] $(DISK) missing — staging first-run disk."; \
		$(MAKE) disk-populate; \
	fi
	$(QEMU_ENV) $(QEMU) -cdrom $(BUILD)/$(ISO) \
		-drive file=$(DISK),format=raw,if=ide,cache=writeback \
		-boot order=d -m $(QEMU_RAM) $(QEMU_KVM) \
		$(if $(QEMU_DISPLAY),-display $(QEMU_DISPLAY)) \
		-debugcon stdio \
		$(QEMU_EXTRA)

debug: iso
	@if [ ! -f $(DISK) ]; then $(MAKE) disk-populate; fi
	@echo ""
	@echo "  QEMU GDB server → :1234  (VM paused at first instruction)"
	@echo "  Connect: gdb -ex 'target remote :1234' -ex 'symbol-file $(BUILD)/$(KERNEL)'"
	@echo ""
	$(QEMU_ENV) $(QEMU) -cdrom $(BUILD)/$(ISO) \
		-drive file=$(DISK),format=raw,if=ide,cache=writeback \
		-boot order=d -m $(QEMU_RAM) $(QEMU_KVM) \
		$(if $(QEMU_DISPLAY),-display $(QEMU_DISPLAY)) \
		$(QEMU_EXTRA) \
		-s -S

disk-quick: user
ifeq ($(UNAME),Linux)
	@if ! command -v fuse2fs >/dev/null 2>&1; then \
		echo "disk-quick: fuse2fs not found — install e2fsprogs or use disk-resync"; exit 1; \
	fi
	@test -f $(DISK) || { echo "disk-quick: $(DISK) missing — run disk-populate first"; exit 1; }
	@mkdir -p mnt
	@fuse2fs $(DISK) mnt
	@# user/build/bin/*.elf live in the ISO (initfs /init overlay); only sync apps.
	@cp user/build/apps/*.elf mnt/bin/ 2>/dev/null; \
	 for f in mnt/bin/*.elf; do [ -f "$$f" ] && mv "$$f" "$${f%.elf}"; done; true
	@fusermount -u mnt
	@rmdir mnt 2>/dev/null || true
	@echo "disk synced (fast)"
else
	@echo "disk-quick: Linux only (needs fuse2fs) — use disk-resync on $(UNAME)"
endif

clean:
	rm -rf $(BUILD)
	-$(MAKE) -C user/crt clean
	-$(MAKE) -C user/lib clean
	-$(MAKE) -C user/core/vega clean
	-$(MAKE) -C user/sdk/vega clean
	-$(MAKE) -C user/init clean
	-$(MAKE) -C user/apps/shell clean
	-$(MAKE) -C user/apps/vega clean
	-$(MAKE) -C user/bin clean
	-$(MAKE) -C user/apps clean
	-$(MAKE) -C user/games clean
	-$(MAKE) -C user/lib/spazer clean

clean-all: clean
	rm -rf thirdparty/musl/install thirdparty/musl/_install \
	       thirdparty/musl-cross \
	       thirdparty/limine \
	       thirdparty/freetype-install \
	       thirdparty/harfbuzz-install \
	       thirdparty/ncurses-install \
	       thirdparty/clang-install

distclean: clean-all
	rm -rf thirdparty $(DISK)

format fmt:
	@find src include user \
	  -type f \
	  \( -name '*.c' -o -name '*.h' -o -name '*.cpp' \) \
	  ! -path '*/thirdparty/*' \
	  ! -path '*/.cache/*' \
	  ! -path '*/doomgeneric/*' \
	  -print0 | xargs -0 clang-format -i

lint:
	clang-tidy \
	  --header-filter='^(src|include|user)/(?!games/doom/doomgeneric/).*' \
	  $(KERNEL_SRCS_C) $(USER_SRCS_C) \
	  -- -I$(INCLUDE) \
	     -I$(SRC) \
	     -Iuser/sdk/vega/include \
	     -Iuser/core/vega/include \
	     -Iuser/apps/shell/include \
	     -Iuser/include \
	     -Iuser/games/doom/doomgeneric/doomgeneric \
	     -Ithirdparty/musl/$(MUSL_PREFIX)/include \
	     -Ithirdparty/freetype-install/usr/include/freetype2 \
	     -Ithirdparty/harfbuzz-install/usr/include/harfbuzz \
	     -DALCOR2_VERSION=\"qa\" \
	     -DSYS_TRACE=0 \
	     -std=gnu11

check:
	cppcheck \
	  --enable=all \
	  --suppress=missingIncludeSystem \
	  --suppress=unusedFunction \
	  --suppress=checkersReport \
	  --inline-suppr \
	  --inconclusive \
	  --quiet \
	  -DVEGA_VERSION=\"qa\" \
	  -i user/games/doom/doomgeneric \
	  -I$(INCLUDE) \
	  -Iuser/include \
	  -Iuser/sdk/vega/include \
	  -Iuser/core/vega/include \
	  -Iuser/apps/shell/include \
	  -Iuser/games/doom/doomgeneric/doomgeneric \
	  $(SRC) user

qa: lint check
