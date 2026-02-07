/**
 * @file src/arch/x86_64/syscall_arch.c
 * @brief Architecture-specific syscall setup (MSRs, etc).
 */

#include <alcor2/console.h>
#include <alcor2/cpu.h>
#include <alcor2/kstdlib.h>
#include <alcor2/proc.h>
#include <alcor2/syscall.h>

// MSR Definitions (duplicated here or from header, better to rely on header if
// possible) But syscall.h has them.

/**
 * @brief Read Model Specific Register
 */
static inline u64 rdmsr(u32 msr)
{
  u32 lo, hi;
  __asm__ volatile("rdmsr" : "=a"(lo), "=d"(hi) : "c"(msr));
  return ((u64)hi << 32) | lo;
}

/**
 * @brief Write Model Specific Register
 */
static inline void wrmsr(u32 msr, u64 value)
{
  __asm__ volatile(
      "wrmsr" ::"a"((u32)value), "d"((u32)(value >> 32)), "c"(msr)
  );
}

// arch_prctl Codes
#define ARCH_SET_GS 0x1001
#define ARCH_SET_FS 0x1002
#define ARCH_GET_FS 0x1003
#define ARCH_GET_GS 0x1004

/**
 * @brief Set architecture-specific thread state
 */
u64 sys_arch_prctl(u64 code, u64 addr, u64 a3, u64 a4, u64 a5, u64 a6)
{
  (void)a3;
  (void)a4;
  (void)a5;
  (void)a6;

  proc_t *p = proc_current();

  switch(code) {
  case ARCH_SET_FS:
    wrmsr(MSR_FS_BASE, addr);
    if(p)
      p->fs_base = addr;
    return 0;

  case ARCH_SET_GS:
    wrmsr(MSR_GS_BASE, addr);
    return 0;

  case ARCH_GET_FS:
    if(!addr)
      return (u64)-14; // -EFAULT
    *(u64 *)addr = rdmsr(MSR_FS_BASE);
    return 0;

  case ARCH_GET_GS:
    if(!addr)
      return (u64)-14; // -EFAULT
    *(u64 *)addr = rdmsr(MSR_GS_BASE);
    return 0;

  default:
    return (u64)-22; // -EINVAL
  }
}

extern void syscall_entry(void);

/**
 * @brief Initialize syscall mechanism
 */
void syscall_init(void)
{
  /* Enable syscall extension */
  u64 efer = rdmsr(MSR_EFER);
  efer |= EFER_SCE;
  wrmsr(MSR_EFER, efer);

  /* Set up segment selectors */
  u64 star = ((u64)0x28 << 32) | ((u64)0x30 << 48);
  wrmsr(MSR_STAR, star);

  /* Set syscall entry point */
  wrmsr(MSR_LSTAR, (u64)syscall_entry);

  /* Clear IF (interrupt flag) on syscall entry */
  wrmsr(MSR_SFMASK, 0x200);

  console_print("[SYSCALL] Initialized\n");
}
