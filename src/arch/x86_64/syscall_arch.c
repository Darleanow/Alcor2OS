/**
 * @file src/arch/x86_64/syscall_arch.c
 * @brief Syscall hardware setup (STAR/LSTAR/SFMASK MSRs, `syscall_init`).
 *
 * Shared MSR constants live in `syscall.h`; this file only provides local
 * `rdmsr` / `wrmsr` and programs the MSRs for 64-bit SYSRET.
 */

#include <alcor2/arch/cpu.h>
#include <alcor2/drivers/console.h>

static inline u64 rdmsr(u32 msr)
{
  u32 lo, hi;
  __asm__ volatile("rdmsr" : "=a"(lo), "=d"(hi) : "c"(msr));
  return ((u64)hi << 32) | lo;
}

static inline void wrmsr(u32 msr, u64 value)
{
  __asm__ volatile(
      "wrmsr" ::"a"((u32)value), "d"((u32)(value >> 32)), "c"(msr)
  );
}

extern void syscall_entry(void);

void        syscall_init(void)
{
  u64 efer = rdmsr(MSR_EFER);
  efer |= EFER_SCE;
  wrmsr(MSR_EFER, efer);

  /* STAR[47:32]=0x28: SYSCALL → kernel CS 0x28, SS 0x30.
   * STAR[63:48]=0x33: SYSRET → user CS base+16, SS base+8. The base carries
   * RPL 3 because SYSRET does not OR it into the selector — a base of 0x30
   * would yield SS 0x38 (RPL 0), faulting the next iretq that revalidates it. */
  u64 star = ((u64)0x28 << 32) | ((u64)0x33 << 48);
  wrmsr(MSR_STAR, star);

  wrmsr(MSR_LSTAR, (u64)syscall_entry);
  wrmsr(MSR_SFMASK, 0x200);

  console_print("[SYSCALL] Initialized\n");
}
