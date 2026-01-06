/**
 * @file src/arch/x86_64/cpu.c
 * @brief CPU control and management implementation.
 */

#include <alcor2/console.h>
#include <alcor2/cpu.h>

/** @brief MSR register for FS base (thread-local storage). */
#define MSR_FS_BASE 0xC0000100

/**
 * @brief Halt the CPU indefinitely.
 * 
 * Disables interrupts and enters an infinite HLT loop. Used for
 * unrecoverable errors or system shutdown. Never returns.
 */
NORETURN void cpu_halt(void)
{
  cpu_disable_interrupts();
  for(;;)
    __asm__ volatile("hlt");
}

/**
 * @brief CPU pause instruction for spinlock optimization.
 * 
 * Hints to the CPU that we're in a spinlock loop, improving performance
 * and reducing power consumption.
 */
void cpu_pause(void)
{
  __asm__ volatile("pause");
}

/**
 * @brief Disable hardware interrupts (CLI instruction).
 * 
 * Prevents the CPU from handling external interrupts. Used for
 * critical sections that must be atomic.
 */
void cpu_disable_interrupts(void)
{
  __asm__ volatile("cli");
}

/**
 * @brief Enable hardware interrupts (STI instruction).
 * 
 * Allows the CPU to handle external interrupts. Must be called after
 * cpu_disable_interrupts() to restore normal interrupt handling.
 */
void cpu_enable_interrupts(void)
{
  __asm__ volatile("sti");
}

/**
 * @brief Set the FS base MSR for thread-local storage.
 * 
 * Sets the base address for the FS segment register, typically used
 * for thread-local storage in user applications.
 * 
 * @param addr Linear address for FS segment base.
 */
void cpu_set_fs_base(u64 addr)
{
  u32 lo = (u32)addr;
  u32 hi = (u32)(addr >> 32);
  __asm__ volatile("wrmsr" ::"a"(lo), "d"(hi), "c"(MSR_FS_BASE));
}

/**
 * @brief Get the current FS base MSR value.
 * 
 * Reads the base address of the FS segment register.
 * 
 * @return FS base address.
 */
u64 cpu_get_fs_base(void)
{
  u32 lo, hi;
  __asm__ volatile("rdmsr" : "=a"(lo), "=d"(hi) : "c"(MSR_FS_BASE));
  return ((u64)hi << 32) | lo;
}

/**
 * @brief Enable SSE and FPU instructions.
 * 
 * Configures CR0 and CR4 to enable SSE/SSE2 instructions and floating-point
 * operations. Initializes the FPU state. Must be called during kernel initialization.
 */
void cpu_enable_sse(void)
{
  u64 cr0, cr4;

  /* Read CR0 */
  __asm__ volatile("mov %%cr0, %0" : "=r"(cr0));

  /* Clear EM (bit 2) - no x87 emulation */
  /* Set MP (bit 1) - monitor coprocessor */
  cr0 &= ~(1UL << 2); /* Clear EM */
  cr0 |= (1UL << 1);  /* Set MP */

  /* Write CR0 */
  __asm__ volatile("mov %0, %%cr0" ::"r"(cr0));

  /* Read CR4 */
  __asm__ volatile("mov %%cr4, %0" : "=r"(cr4));

  /* Set OSFXSR (bit 9) - enable SSE instructions */
  /* Set OSXMMEXCPT (bit 10) - enable SSE exceptions */
  cr4 |= (1UL << 9);  /* OSFXSR */
  cr4 |= (1UL << 10); /* OSXMMEXCPT */

  /* Write CR4 */
  __asm__ volatile("mov %0, %%cr4" ::"r"(cr4));

  /* Initialize FPU */
  __asm__ volatile("fninit");

  console_print("[CPU] SSE/FPU enabled\n");
}
