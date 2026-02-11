/**
 * @file src/kernel/main.c
 * @brief Kernel entry point and initialization.
 */

#include <alcor2/ata.h>
#include <alcor2/console.h>
#include <alcor2/cpu.h>
#include <alcor2/elf.h>
#include <alcor2/ext2.h>
#include <alcor2/gdt.h>
#include <alcor2/heap.h>
#include <alcor2/idt.h>
#include <alcor2/keyboard.h>
#include <alcor2/limine.h>
#include <alcor2/pic.h>
#include <alcor2/pit.h>
#include <alcor2/pmm.h>
#include <alcor2/proc.h>
#include <alcor2/sched.h>
#include <alcor2/syscall.h>
#include <alcor2/types.h>
#include <alcor2/user.h>
#include <alcor2/vfs.h>
#include <alcor2/vmm.h>

LIMINE_BASE_REVISION(3)
LIMINE_REQUESTS_START

USED SECTION(".limine_requests"
) static volatile struct limine_framebuffer_request fb_request = {
    .id       = LIMINE_FRAMEBUFFER_REQUEST_ID,
    .revision = 0,
};

USED SECTION(".limine_requests"
) static volatile struct limine_memmap_request memmap_request = {
    .id       = LIMINE_MEMMAP_REQUEST_ID,
    .revision = 0,
};

USED SECTION(".limine_requests"
) static volatile struct limine_hhdm_request hhdm_request = {
    .id       = LIMINE_HHDM_REQUEST_ID,
    .revision = 0,
};

USED SECTION(".limine_requests"
) static volatile struct limine_module_request module_request = {
    .id       = LIMINE_MODULE_REQUEST_ID,
    .revision = 0,
};

LIMINE_REQUESTS_END

/** @brief Print boot banner. */
static void print_banner(void)
{
  console_print("\n");
  console_print("    ___    __                ___\n");
  console_print("   /   |  / /________  _____/__ \\\n");
  console_print("  / /| | / / ___/ __ \\/ ___/_/ /\n");
  console_print(" / ___ |/ / /__/ /_/ / /   / __/\n");
  console_print("/_/  |_/_/\\___/\\____/_/   /____/\n");
  console_print("\n");
  console_print("Alcor2 OS v0.1.0\n");
  console_print("----------------\n\n");
}

/**
 * @brief Initialize early boot subsystems.
 *
 * Sets up console, memory managers, and heap.
 *
 * @param fb Limine framebuffer for console output.
 */
static void init_early(struct limine_framebuffer *fb)
{
  /* Console */
  console_init(fb->address, fb->width, fb->height, fb->pitch);
  console_set_theme((console_theme_t) {
      .foreground = 0xA6A6A6,
      .background = 0x000000,
  });
  console_clear();
  print_banner();

  console_printf(
      "Framebuffer: %dx%d @ %dbpp\n", (int)fb->width, (int)fb->height,
      (int)fb->bpp
  );

  /* Memory management */
  pmm_init(memmap_request.response, hhdm_request.response->offset);
  console_printf(
      "PMM: %dMB total, %dMB free\n", (int)(pmm_get_total() / 1024 / 1024),
      (int)(pmm_get_free() / 1024 / 1024)
  );

  vmm_init(hhdm_request.response->offset);
  console_print("VMM initialized.\n");

  heap_init();
}

/**
 * @brief Initialize CPU and core kernel structures.
 *
 * Sets up GDT, IDT, scheduler, SSE, and syscall interface.
 */
static void init_core(void)
{
  sched_init();

  gdt_init();
  console_print("GDT loaded.\n");

  idt_init();
  console_print("IDT loaded.\n");

  cpu_enable_sse();
  syscall_init();
}

/**
 * @brief Initialize interrupt controllers and drivers.
 *
 * Sets up PIC, PIT timer, and keyboard.
 */
static void init_interrupts(void)
{
  pic_init();
  pit_init(100);
  pic_unmask(IRQ_TIMER);
  pit_enable_sched();
  console_print("PIC/PIT initialized (100Hz).\n");

  keyboard_init();
  console_print("Keyboard initialized.\n");
}

/**
 * @brief Initialize storage and filesystems.
 *
 * Sets up ATA driver, VFS, ext2, and mounts root filesystem.
 */
static void init_storage(void)
{
  ata_init();
  vfs_init();
  ext2_init();

  /* Mount root filesystem */
  const ata_drive_t *hda = ata_get_drive(0);
  if(hda && hda->present) {
    if(vfs_mount("/dev/hda", "/", "ext2") == 0) {
      console_print("Mounted /dev/hda (ext2) on /\n");
      vfs_mount(NULL, "/dev", "ramfs");
    } else {
      console_print("Failed to mount ext2 - falling back to ramfs\n");
    }
  } else {
    console_print("No disk found - using ramfs only\n");
  }
}

/**
 * @brief Launch first user process from boot modules.
 *
 * Loads the first Limine module as the init/shell process.
 * Never returns on success.
 */
static void launch_init(void)
{
  proc_init();

  if(!module_request.response || module_request.response->module_count == 0) {
    console_print("[KERNEL] No modules found, halting.\n");
    return;
  }

  struct limine_file *mod = module_request.response->modules[0];
  console_printf(
      "[KERNEL] Loading: %s (%d bytes)\n", mod->path, (int)mod->size
  );

  /* Start first process - never returns */
  proc_start_first(mod->address, mod->size, "shell");
}

/**
 * @brief Kernel main entry point.
 *
 * Initializes all kernel subsystems in order, then starts the first
 * user process. Never returns
 */
// cppcheck-suppress unusedFunction
void kmain(void)
{
  /* Validate bootloader response */
  if(!LIMINE_BASE_REVISION_OK)
    cpu_halt();
  if(!fb_request.response || fb_request.response->framebuffer_count < 1)
    cpu_halt();
  if(!memmap_request.response || !hhdm_request.response)
    cpu_halt();

  /* Early init (console, memory) */
  init_early(fb_request.response->framebuffers[0]);

  /* Core kernel (GDT, IDT, scheduler) */
  init_core();

  /* Interrupts and drivers */
  init_interrupts();

  /* Storage and filesystems */
  init_storage();

  /* Enable interrupts */
  cpu_enable_interrupts();
  console_print("Interrupts enabled.\n\n");

  /* Stage 5: Launch init process */
  launch_init();

  /* Idle loop (fallback if no init) */
  for(;;) {
    cpu_enable_interrupts();
    __asm__ volatile("hlt");
  }
}
