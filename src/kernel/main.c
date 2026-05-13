/**
 * @file src/kernel/main.c
 * @brief Kernel entry point and bring-up sequence.
 *
 * Typical order: console → PMM (Limine map) → VMM (HHDM) → GDT/IDT → PIC/PIT →
 * kernel heap → ATA disk → ext2 volume on `/` → VFS → keyboard → syscall MSRs →
 * scheduler → first user program (shell or binary from module).
 */

#include <alcor2/arch/cpu.h>
#include <alcor2/arch/gdt.h>
#include <alcor2/arch/idt.h>
#include <alcor2/arch/pic.h>
#include <alcor2/arch/pit.h>
#include <alcor2/drivers/ata.h>
#include <alcor2/drivers/console.h>
#include <alcor2/drivers/fb_user.h>
#include <alcor2/drivers/keyboard.h>
#include <alcor2/fs/ext2.h>
#include <alcor2/fs/vfs.h>
#include <alcor2/limine.h>
#include <alcor2/mm/heap.h>
#include <alcor2/mm/pmm.h>
#include <alcor2/mm/vmm.h>
#include <alcor2/proc/elf.h>
#include <alcor2/proc/proc.h>
#include <alcor2/proc/sched.h>
#include <alcor2/proc/user.h>
#include <alcor2/sys/syscall.h>
#include <alcor2/types.h>

extern void ramfs_init(void);

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
}

/**
 * @brief Initialize early boot subsystems.
 *
 * Sets up console, memory managers, and heap.
 *
 * @param fb Limine framebuffer for console output.
 */
static void init_early(
    struct limine_framebuffer *fb, struct limine_memmap_response *memmap,
    const struct limine_hhdm_response *hhdm
)
{
  /* Console */
  console_init(fb->address, fb->width, fb->height, fb->pitch, fb->bpp);
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
  pmm_init(memmap, hhdm->offset);
  console_printf(
      "PMM: %dMB total, %dMB free\n", (int)(pmm_get_total() / 1024 / 1024),
      (int)(pmm_get_free() / 1024 / 1024)
  );

  vmm_init(hhdm->offset);
  console_print("VMM initialized.\n");

  fb_user_boot_init(fb, memmap, hhdm->offset);

  heap_init();
  ramfs_init();
}

/**
 * @brief Launch first user process from boot modules.
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
  const char *ep = (mod->path && mod->path[0]) ? mod->path : "/boot/shell.elf";
  proc_start_first(mod->address, mod->size, "shell", ep);
}

/** @brief Represents a single phase of the kernel boot process. */
typedef struct
{
  const char *name;         /**< Display name for logging */
  void (*init)(void);       /**< Phase-specific initialization function */
  bool critical;            /**< Halt if this phase fails (not used yet) */
} boot_phase_t;

/**
 * @brief Initialize interrupt controllers and drivers.
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
 */
static void init_storage(void)
{
  ata_init();
  ramfs_init();
  ext2_init();

  /* Mount root filesystem */
  const ata_drive_t *hda = ata_get_drive(0);
  if(hda && hda->present) {
    if(vfs_mount("/dev/hda", "/", "ext2") == 0) {
      console_print("[INIT] Mounted /dev/hda (ext2) on /\n");
      vfs_mount(NULL, "/dev", "ramfs");
    } else {
      console_print("[INIT] Failed to mount ext2 - falling back to ramfs\n");
    }
  } else {
    console_print("[INIT] No disk found - using ramfs only\n");
  }
}

/**
 * @brief Enable interrupts and log the event.
 */
static void init_enable_irqs(void)
{
  cpu_enable_interrupts();
  console_print("[INIT] Interrupts enabled.\n\n");
}

/** @brief Table-driven bring-up sequence. */
static const boot_phase_t boot_sequence[] = {
    {"Core Scheduler", sched_init, true},
    {"GDT Structure", gdt_init, true},
    {"IDT Structure", idt_init, true},
    {"SSE/FPU Support", cpu_enable_sse, true},
    {"Syscall Interface", syscall_init, true},
    {"PIC/PIT Timers", pic_init, true},
    {"Hardware Interrupts", init_interrupts, true},
    {"VFS Orchestrator", vfs_init, true},
    {"Storage & VFS", init_storage, true},
    {"Global Interrupts", init_enable_irqs, true},
    {NULL, NULL, false}
};

/**
 * @brief Kernel main entry point.
 */
void kmain(void)
{
  /* Validate bootloader response */
  if(!LIMINE_BASE_REVISION_OK || !fb_request.response ||
     fb_request.response->framebuffer_count < 1 || !memmap_request.response ||
     !hhdm_request.response) {
    cpu_halt();
  }

  /* Early init (console, memory) — required for any output or allocation. */
  init_early(
      fb_request.response->framebuffers[0], memmap_request.response,
      hhdm_request.response
  );

  /* Execute boot sequence table */
  for(const boot_phase_t *p = boot_sequence; p->init; p++) {
    p->init();
    if(p->name)
      console_printf("[INIT] %s initialized.\n", p->name);
  }

  /* Stage 5: Launch init process */
  launch_init();

  /* Idle loop (fallback if no init) */
  for(;;) {
    cpu_enable_interrupts();
    __asm__ volatile("hlt");
  }
}
