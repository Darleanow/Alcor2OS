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
#include <alcor2/drivers/fb_console.h>
#include <alcor2/drivers/fb_user.h>
#include <alcor2/drivers/keyboard.h>
#include <alcor2/drivers/mouse.h>
#include <alcor2/fs/blockdev.h>
#include <alcor2/fs/ext2.h>
#include <alcor2/fs/initfs.h>
#include <alcor2/fs/vfs.h>
#include <alcor2/kstdlib.h>
#include <alcor2/limine.h>
#include <alcor2/mm/heap.h>
#include <alcor2/mm/pmm.h>
#include <alcor2/mm/vmm.h>
#include <alcor2/proc/elf.h>
#include <alcor2/proc/proc.h>
#include <alcor2/proc/sched.h>
#include <alcor2/sys/syscall.h>
#include <alcor2/types.h>

extern void ramfs_init(void);

LIMINE_BASE_REVISION(3)
LIMINE_REQUESTS_START

USED SECTION(
    ".limine_requests"
) static volatile struct limine_framebuffer_request fb_request = {
    .id       = LIMINE_FRAMEBUFFER_REQUEST_ID,
    .revision = 0,
};

USED SECTION(
    ".limine_requests"
) static volatile struct limine_memmap_request memmap_request = {
    .id       = LIMINE_MEMMAP_REQUEST_ID,
    .revision = 0,
};

USED SECTION(
    ".limine_requests"
) static volatile struct limine_hhdm_request hhdm_request = {
    .id       = LIMINE_HHDM_REQUEST_ID,
    .revision = 0,
};

USED SECTION(
    ".limine_requests"
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
  console_printf("Alcor2 OS %s\n", ALCOR2_VERSION);
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
  initfs_init();

  /* fb_console takes over the framebuffer with cell-grid + ANSI/CSI parsing.
   * Allocates via kmalloc, so it must run after heap_init. Falls back to the
   * boot logger if allocation fails. */
  if(!fb_console_init(fb->address, fb->width, fb->height, fb->pitch, fb->bpp))
    console_print("[fb_console] init failed; staying on boot logger.\n");
}

/** @brief Shell module pointer set by ::init_bin_overlay, consumed by
 * ::launch_init when starting PID 1. */
static struct limine_file *g_shell_module = NULL;

/**
 * @brief Return @c true when @p path starts with @p prefix.
 *
 * @param path    NUL-terminated path to test.
 * @param prefix  NUL-terminated prefix.
 * @return @c true if every byte of @p prefix matches the head of @p path.
 */
static bool path_has_prefix(const char *path, const char *prefix)
{
  while(*prefix) {
    if(*path++ != *prefix++)
      return false;
  }
  return true;
}

/**
 * @brief Return @c true when @p path ends with @p suffix (case-sensitive).
 *
 * @param path    NUL-terminated path to test.
 * @param suffix  NUL-terminated suffix.
 * @return @c true if the tail of @p path equals @p suffix.
 */
static bool path_has_suffix(const char *path, const char *suffix)
{
  u64 lp = kstrlen(path);
  u64 ls = kstrlen(suffix);
  if(ls > lp)
    return false;
  return kstreq(path + (lp - ls), suffix);
}

/**
 * @brief Strip a trailing @c .elf extension in-place.
 *
 * @c "ls.elf" becomes @c "ls" so initfs lookups can use unsuffixed names.
 *
 * @param name  NUL-terminated string mutated in place; no-op when it does
 *              not end in @c .elf.
 */
static void strip_elf_suffix(char *name)
{
  u64 n = kstrlen(name);
  if(n > 4 && kstreq(name + n - 4, ".elf"))
    name[n - 4] = '\0';
}

/**
 * @brief Classify boot modules into initfs entries (the /init overlay) and
 * the shell module (PID 1). After this phase, /init is a mounted initfs
 * backed by Limine-loaded buffers (vega searches it before /bin and
 * /usr/bin), and @c g_shell_module is ready for launch_init.
 */
static void init_bin_overlay(void)
{
  static const char bin_prefix[] = "/boot/bin/";
  static const char shell_path[] = "/boot/shell.elf";
  const u64         bin_prefix_n = sizeof(bin_prefix) - 1;

  if(!module_request.response || module_request.response->module_count == 0) {
    console_print("[INIT] No boot modules — /init overlay disabled.\n");
    return;
  }

  u64 bin_count = 0;
  for(u64 i = 0; i < module_request.response->module_count; i++) {
    struct limine_file *mod = module_request.response->modules[i];
    if(!mod->path || mod->path[0] != '/')
      continue;

    if(kstreq(mod->path, shell_path)) {
      g_shell_module = mod;
      continue;
    }

    if(path_has_prefix(mod->path, bin_prefix) &&
       path_has_suffix(mod->path, ".elf")) {
      char name[VFS_NAME_MAX];
      kstrncpy(name, mod->path + bin_prefix_n, VFS_NAME_MAX);
      strip_elf_suffix(name);
      if(initfs_register(name, mod->address, mod->size) == 0) {
        bin_count++;
      } else {
        console_printf("[INIT] /init overlay: failed to add %s\n", name);
      }
    }
  }

  if(bin_count > 0) {
    if(vfs_mount(NULL, "/init", "initfs") == 0)
      console_printf(
          "[INIT] /init overlay: %d files mounted.\n", (int)bin_count
      );
    else
      console_print("[INIT] /init overlay: mount failed.\n");
  }
}

/**
 * @brief Launch first user process from the shell module identified by
 * init_bin_overlay.
 */
static void launch_init(void)
{
  if(!g_shell_module) {
    console_print("[KERNEL] No shell module (/boot/shell.elf) — halting.\n");
    return;
  }

  console_printf(
      "[KERNEL] Loading: %s (%lu bytes)\n", g_shell_module->path,
      (u64)g_shell_module->size
  );

  /* proc_start_first jumps to ring 3 and never comes back. */
  proc_start_first(
      g_shell_module->address, g_shell_module->size, "shell",
      g_shell_module->path
  );
}

/** @brief Represents a single phase of the kernel boot process. */
typedef struct
{
  const char *name;          /**< Display name for logging */
  void        (*init)(void); /**< Phase-specific initialization function */
} boot_phase_t;

/**
 * @brief Initialize interrupt controllers and drivers.
 */
static void init_interrupts(void)
{
  pic_init();
  pit_init(PIT_TICK_HZ);
  pit_enable_sched();
  console_print("PIC/PIT initialized.\n");

  keyboard_init();
  console_print("Keyboard initialized.\n");
}

static void init_input(void)
{
  mouse_init();
  if(fb_request.response && fb_request.response->framebuffer_count > 0) {
    struct limine_framebuffer *fb = fb_request.response->framebuffers[0];
    mouse_set_screen((u32)fb->width, (u32)fb->height);
  }

  if(!mouse_ps2_init())
    console_print("[INIT] No PS/2 mouse — /dev/mouse will be quiet.\n");
}

/**
 * @brief Initialize storage and filesystems.
 */
static i64 ata0_bd_read(void *ctx, u64 lba, u32 count, void *buf)
{
  return ata_read((u8)(u64)ctx, lba, count, buf);
}
static i64 ata0_bd_write(void *ctx, u64 lba, u32 count, const void *buf)
{
  return ata_write((u8)(u64)ctx, lba, count, buf);
}

static void init_storage(void)
{
  ata_init();

  static const blockdev_t ata0_dev = {ata0_bd_read, ata0_bd_write, (void *)0};
  ext2_init(&ata0_dev);

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

static const char *idt_hook_proc_name(void)
{
  const proc_t *p = proc_current();
  return p ? proc_name(p) : NULL;
}

static void idt_hook_proc_exit(i64 code)
{
  proc_exit(code);
}

static void init_idt_proc_hooks(void)
{
  idt_set_proc_hooks((idt_proc_hooks_t) {
      .current_name = idt_hook_proc_name,
      .exit         = idt_hook_proc_exit,
  });
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
    {"GDT Structure",        gdt_init           },
    {"IDT Structure",        idt_init           },
    {"SSE/FPU Support",      cpu_enable_sse     },
    {"PAT (WC memory type)", cpu_init_pat       },
    {"Syscall Interface",    syscall_init       },
    {"PIC/PIT Timers",       pic_init           },
    {"Hardware Interrupts",  init_interrupts    },
    {"VFS Orchestrator",     vfs_init           },
    {"Storage & VFS",        init_storage       },
    {"/init Overlay",        init_bin_overlay   },
    {"PS/2 Mouse",           init_input         },
    {"Process Table",        proc_init          },
    {NULL,                   init_idt_proc_hooks},
    {"Global Interrupts",    init_enable_irqs   },
    {NULL,                   NULL               }
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

  /* Launch the init process from boot module 0. */
  launch_init();

  /* Idle loop (fallback if no init) */
  for(;;) {
    cpu_enable_interrupts();
    __asm__ volatile("hlt");
  }
}
