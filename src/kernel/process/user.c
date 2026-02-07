/**
 * @file src/kernel/user.c
 * @brief Ring-3 (userspace) task creation and execution.
 */

#include <alcor2/console.h>
#include <alcor2/elf.h>
#include <alcor2/gdt.h>
#include <alcor2/pmm.h>
#include <alcor2/user.h>
#include <alcor2/vmm.h>

/** @brief User stack base address. */
#define USER_STACK_ADDR 0x800000ULL
/** @brief User stack size (16 KB). */
#define USER_STACK_SIZE (16ULL * 1024)

extern void tss_set_rsp0(u64 rsp0);

/** @brief Kernel stack for syscall/interrupt handling. */
static u8 kernel_stack[8192] __attribute__((aligned(16)));

/**
 * @brief Allocate and map user stack.
 *
 * Allocates physical pages and maps them at USER_STACK_ADDR with user
 * permissions (ring 3 accessible).
 *
 * @return Pointer to top of stack (ready to use), or NULL on failure.
 */
static void *alloc_user_stack(void)
{
  u64   stack_pages = USER_STACK_SIZE / PAGE_SIZE;
  void *stack_phys  = pmm_alloc_pages(stack_pages);
  if(!stack_phys) {
    return NULL;
  }

  for(u64 i = 0; i < stack_pages; i++) {
    vmm_map(
        USER_STACK_ADDR + i * PAGE_SIZE, (u64)stack_phys + i * PAGE_SIZE,
        VMM_PRESENT | VMM_WRITE | VMM_USER
    );
  }

  return (void *)(USER_STACK_ADDR + USER_STACK_SIZE - 8);
}

/**
 * @brief Execute an ELF binary in userspace (ring 3).
 *
 * Loads the ELF file into user-accessible memory, allocates a user stack,
 * sets up the TSS kernel stack, and jumps to the ELF entry point.
 *
 * @param data Pointer to ELF file data.
 * @param size Size of ELF file in bytes.
 * @return Exit code from user program, or (u64)-1 on load failure.
 */
// cppcheck-suppress unusedFunction
u64 user_exec_elf(const void *data, u64 size)
{
  elf_info_t info;

  /* Load ELF into user memory */
  if(elf_load(data, size, &info) != 0) {
    console_print("[USER] Failed to load ELF\n");
    return (u64)-1;
  }

  /* Allocate user stack */
  void *user_rsp = alloc_user_stack();
  if(!user_rsp) {
    console_print("[USER] Failed to allocate stack\n");
    return (u64)-1;
  }

  /* Set kernel stack in TSS */
  tss_set_rsp0((u64)&kernel_stack[sizeof(kernel_stack)]);

  console_print("[USER] Entering Ring 3...\n");

  /* Execute ELF entry point */
  return user_enter((void *)info.entry, user_rsp);
}
