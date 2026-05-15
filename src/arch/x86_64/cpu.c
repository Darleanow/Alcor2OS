/**
 * @file src/arch/x86_64/cpu.c
 * @brief CPU control and management implementation.
 */

#include <alcor2/arch/cpu.h>
#include <alcor2/drivers/console.h>

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
// cppcheck-suppress unusedFunction
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
 * operations. Initializes the FPU state. Must be called during kernel
 * initialization.
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

  /* Check CPU features */
  u32 ecx1 = 0, ebx7 = 0, unused;
  __asm__ volatile("cpuid"
                   : "=a"(unused), "=b"(unused), "=c"(ecx1), "=d"(unused)
                   : "a"(1), "c"(0));
  __asm__ volatile("cpuid"
                   : "=a"(unused), "=b"(ebx7), "=c"(unused), "=d"(unused)
                   : "a"(7), "c"(0));

  cr4 |= (1UL << 9);  /* OSFXSR */
  cr4 |= (1UL << 10); /* OSXMMEXCPT */
  if(ebx7 & (1UL << 0))
    cr4 |= (1UL << 16); /* FSGSBASE */
  if(ecx1 & (1UL << 26))
    cr4 |= (1UL << 18); /* OSXSAVE */

  /* Write CR4 */
  __asm__ volatile("mov %0, %%cr4" ::"r"(cr4));

  /* Initialize FPU */
  __asm__ volatile("fninit");

  if(ecx1 & (1UL << 26)) {
    u64 xcr0 = (1UL << 0) | (1UL << 1); /* x87 + SSE */
    if(ecx1 & (1UL << 28))
      xcr0 |= (1UL << 2); /* AVX */
    __asm__ volatile("xsetbv" ::"a"((u32)xcr0), "d"((u32)(xcr0 >> 32)), "c"(0));
  }

  /* Capture a known-good FPU state to copy into each fresh proc on alloc. */
  extern void proc_capture_default_fpu(void);
  proc_capture_default_fpu();

  console_print("[CPU] SSE/AVX/FPU enabled\n");
}
