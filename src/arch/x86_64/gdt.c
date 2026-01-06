/**
 * @file src/arch/x86_64/gdt.c
 * @brief Global Descriptor Table and TSS setup.
 */

#include <alcor2/gdt.h>

/** @name GDT Access Flags */
/**@{*/
#define GDT_ACCESS_PRESENT (1 << 7)
#define GDT_ACCESS_RING0   (0 << 5)
#define GDT_ACCESS_RING3   (3 << 5)
#define GDT_ACCESS_SEGMENT (1 << 4)
#define GDT_ACCESS_EXEC    (1 << 3)
#define GDT_ACCESS_RW      (1 << 1)
#define GDT_ACCESS_TSS     0x09
/**@}*/

/** @name GDT Flags */
/**@{*/
#define GDT_FLAG_LONG      (1 << 1)
#define GDT_FLAG_GRANULAR  (1 << 3)
/**@}*/

extern void gdt_load(gdt_ptr_t *gdtr);

/** @brief GDT table with kernel/user segments and TSS. */
static struct
{
  gdt_entry_t     null;
  gdt_entry_t     reserved[4];
  gdt_entry_t     kernel_code; /**< 0x28 */
  gdt_entry_t     kernel_data; /**< 0x30 */
  gdt_entry_t     user_data; /**< 0x38 - MUST precede user_code for SYSRET */
  gdt_entry_t     user_code; /**< 0x40 */
  gdt_tss_entry_t tss;       /**< 0x48 */
} PACKED         gdt;

static gdt_ptr_t gdtr;
static tss_t     tss;

/**
 * @brief Set a standard GDT entry.
 * @param entry Entry to configure.
 * @param access Access byte.
 * @param flags Flags nibble.
 */
static void      gdt_set_entry(gdt_entry_t *entry, u8 access, u8 flags)
{
  entry->limit_low   = 0xFFFF;
  entry->base_low    = 0;
  entry->base_mid    = 0;
  entry->access      = access;
  entry->flags_limit = (flags << 4) | 0x0F;
  entry->base_high   = 0;
}

/**
 * @brief Set TSS descriptor.
 * @param entry TSS entry to configure.
 * @param base TSS base address.
 */
static void gdt_set_tss(gdt_tss_entry_t *entry, u64 base)
{
  entry->limit_low   = sizeof(tss_t) - 1;
  entry->base_low    = base & 0xFFFF;
  entry->base_mid    = (base >> 16) & 0xFF;
  entry->access      = GDT_ACCESS_PRESENT | GDT_ACCESS_TSS;
  entry->flags_limit = 0;
  entry->base_high   = (base >> 24) & 0xFF;
  entry->base_upper  = (base >> 32) & 0xFFFFFFFF;
  entry->reserved    = 0;
}

/**
 * @brief Initialize the Global Descriptor Table and load the TSS.
 * 
 * Sets up kernel code/data segments, user code/data segments (with proper
 * ordering for SYSRET compatibility), and the Task State Segment. Loads
 * the GDT and switches to the new segments.
 */
void gdt_init(void)
{
  gdt_set_entry(&gdt.null, 0, 0);

  for(int i = 0; i < 4; i++) {
    gdt_set_entry(&gdt.reserved[i], 0, 0);
  }

  gdt_set_entry(
      &gdt.kernel_code,
      GDT_ACCESS_PRESENT | GDT_ACCESS_RING0 | GDT_ACCESS_SEGMENT |
          GDT_ACCESS_EXEC | GDT_ACCESS_RW,
      GDT_FLAG_LONG | GDT_FLAG_GRANULAR
  );

  gdt_set_entry(
      &gdt.kernel_data,
      GDT_ACCESS_PRESENT | GDT_ACCESS_RING0 | GDT_ACCESS_SEGMENT |
          GDT_ACCESS_RW,
      GDT_FLAG_GRANULAR
  );

  /* User data MUST come before user code for SYSRET compatibility */
  gdt_set_entry(
      &gdt.user_data,
      GDT_ACCESS_PRESENT | GDT_ACCESS_RING3 | GDT_ACCESS_SEGMENT |
          GDT_ACCESS_RW,
      GDT_FLAG_GRANULAR
  );

  gdt_set_entry(
      &gdt.user_code,
      GDT_ACCESS_PRESENT | GDT_ACCESS_RING3 | GDT_ACCESS_SEGMENT |
          GDT_ACCESS_EXEC | GDT_ACCESS_RW,
      GDT_FLAG_LONG | GDT_FLAG_GRANULAR
  );

  tss.iopb = sizeof(tss_t);
  gdt_set_tss(&gdt.tss, (u64)&tss);

  gdtr.limit = sizeof(gdt) - 1;
  gdtr.base  = (u64)&gdt;

  gdt_load(&gdtr);
}

/**
 * @brief Update the TSS ring-0 stack pointer.
 * 
 * Sets the kernel stack pointer used when transitioning from ring 3 to ring 0
 * (e.g., during system calls or interrupts). Each process has its own kernel stack.
 * 
 * @param rsp0 New kernel stack pointer (top of kernel stack).
 */
void tss_set_rsp0(u64 rsp0)
{
  tss.rsp0 = rsp0;
}
