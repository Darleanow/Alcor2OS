KERNEL      := alcor2.elf
ISO         := alcor2.iso

BUILD       := build
SRC         := src
INCLUDE     := include

DISK        := disk.img
DISK_SIZE   := 32M

CC          := gcc
LD          := ld
AS          := nasm

CFLAGS      := -std=gnu11 -Wall -Wextra -Werror \
               -ffreestanding -fno-stack-protector -fno-stack-check \
               -fno-lto -fPIE -m64 -march=x86-64 \
               -mno-80387 -mno-mmx -mno-sse -mno-sse2 -mno-red-zone \
               -I$(INCLUDE) -MMD -MP

LDFLAGS     := -nostdlib -static -pie --no-dynamic-linker \
               -z text -z max-page-size=0x1000 -T scripts/linker.ld

ASFLAGS     := -f elf64

LIMINE_URL  := https://github.com/limine-bootloader/limine.git
LIMINE_REV  := v8.x-binary

SRCS_C      := $(shell find $(SRC) -name '*.c')
SRCS_ASM    := $(shell find $(SRC) -name '*.asm')
OBJS        := $(patsubst $(SRC)/%.c,$(BUILD)/%.c.o,$(SRCS_C)) \
               $(patsubst $(SRC)/%.asm,$(BUILD)/%.asm.o,$(SRCS_ASM))
DEPS        := $(OBJS:.o=.d)

.PHONY: all iso run run-disk disk disk-mount disk-umount clean distclean user

all: iso

user:
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

iso: $(BUILD)/$(KERNEL) thirdparty/limine/limine user
	@rm -rf $(BUILD)/iso
	@mkdir -p $(BUILD)/iso/boot/limine $(BUILD)/iso/EFI/BOOT $(BUILD)/iso/bin
	@cp $(BUILD)/$(KERNEL) $(BUILD)/iso/boot/
	@cp user/build/shell/shell.elf $(BUILD)/iso/boot/
	@cp user/build/bin/*.elf $(BUILD)/iso/bin/ 2>/dev/null || true
	@cp scripts/limine.conf $(BUILD)/iso/boot/limine/
	@cp thirdparty/limine/limine-bios.sys $(BUILD)/iso/boot/limine/
	@cp thirdparty/limine/limine-bios-cd.bin $(BUILD)/iso/boot/limine/
	@cp thirdparty/limine/limine-uefi-cd.bin $(BUILD)/iso/boot/limine/
	@cp thirdparty/limine/BOOTX64.EFI $(BUILD)/iso/EFI/BOOT/
	@cp thirdparty/limine/BOOTIA32.EFI $(BUILD)/iso/EFI/BOOT/
	@xorriso -as mkisofs -b boot/limine/limine-bios-cd.bin \
		-no-emul-boot -boot-load-size 4 -boot-info-table \
		--efi-boot boot/limine/limine-uefi-cd.bin \
		-efi-boot-part --efi-boot-image --protective-msdos-label \
		$(BUILD)/iso -o $(BUILD)/$(ISO) 2>/dev/null
	@thirdparty/limine/limine bios-install $(BUILD)/$(ISO) 2>/dev/null
	@echo "Created $(BUILD)/$(ISO)"

run: iso
	qemu-system-x86_64 -cdrom $(BUILD)/$(ISO) -m 256M

# ============================================================================
# Disk Image Management (FAT32)
# ============================================================================

# Create a FAT32 disk image
disk: $(DISK)

$(DISK):
	@mkdir -p $(BUILD)
	@echo "Creating $(DISK_SIZE) FAT32 disk image..."
	@dd if=/dev/zero of=$(DISK) bs=1M count=$$(echo $(DISK_SIZE) | sed 's/M//') 2>/dev/null
	@mkfs.fat -F 32 -n "ALCOR2" $(DISK) >/dev/null
	@echo "Created $(DISK)"

# Run with disk attached (hda = /dev/hda in guest)
run-disk: iso disk-populate
	qemu-system-x86_64 -cdrom $(BUILD)/$(ISO) -drive file=$(DISK),format=raw,if=ide,cache=writethrough -boot d -m 256M

# Mount disk image to ./mnt for adding files (requires sudo)
disk-mount: $(DISK)
	@mkdir -p mnt
	@sudo mount -o loop $(DISK) mnt
	@echo "Disk mounted at ./mnt - use 'make disk-umount' when done"

# Unmount disk image
disk-umount:
	@sudo umount mnt 2>/dev/null || true
	@rmdir mnt 2>/dev/null || true
	@echo "Disk unmounted"

# Populate disk with user binaries
disk-populate: $(DISK) user
	@mkdir -p mnt
	@sudo mount -o loop $(DISK) mnt
	@sudo mkdir -p mnt/bin mnt/etc mnt/tmp mnt/home
	@sudo cp user/build/bin/*.elf mnt/bin/ 2>/dev/null || true
	@for f in mnt/bin/*.elf; do \
		if [ -f "$$f" ]; then \
			newname=$$(echo "$$f" | sed 's/\.elf$$//'); \
			sudo mv "$$f" "$$newname"; \
		fi; \
	done
	@echo "Welcome to Alcor2!" | sudo tee mnt/etc/motd > /dev/null
	@sudo umount mnt
	@rmdir mnt
	@echo "Disk populated with user binaries"

# Quick helper: add a file to disk
# Usage: make disk-add FILE=myfile.txt
disk-add: $(DISK)
	@if [ -z "$(FILE)" ]; then echo "Usage: make disk-add FILE=<path>"; exit 1; fi
	@mkdir -p mnt
	@sudo mount -o loop $(DISK) mnt
	@sudo cp "$(FILE)" mnt/
	@sudo umount mnt
	@rmdir mnt
	@echo "Added $(FILE) to disk"

clean:
	rm -rf $(BUILD)
	$(MAKE) -C user/init clean
	$(MAKE) -C user/shell clean

distclean: clean
	rm -rf thirdparty

-include $(DEPS)
