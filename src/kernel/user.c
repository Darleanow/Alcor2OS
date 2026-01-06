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
#define USER_STACK_SIZE (16 * 1024)

extern u8 user_test_code[];
extern u8 user_test_code_end[];
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

/**
 * @brief Create and run a user-mode task from bytecode.
 * 
 * Fallback mechanism when no ELF module is available. Maps bytecode at
 * a fixed address, copies it to user memory, and executes it.
 * 
 * @param name Task name (currently unused).
 * @param entry Entry point function (currently unused, uses bytecode instead).
 * @return Exit code from user task.
 */
u64 user_task_create(const char *name, void (*entry)(void))
{
  (void)name;
  (void)entry;

  /* Map bytecode at fixed address */
  void *code_phys = pmm_alloc();
  if(!code_phys) {
    console_print("[USER] Failed to allocate code page\n");
    return 0;
  }

  u64 code_addr = 0x400000ULL;
  vmm_map(code_addr, (u64)code_phys, VMM_PRESENT | VMM_WRITE | VMM_USER);

  /* Copy bytecode */
  u8 *code_dst  = (u8 *)((u64)code_phys + vmm_get_hhdm());
  u64 code_size = (u64)(user_test_code_end - user_test_code);

  for(u64 i = 0; i < code_size && i < PAGE_SIZE; i++) {
    code_dst[i] = user_test_code[i];
  }

  console_printf(
      "[USER] Loaded %d bytes (bytecode fallback)\n", (int)code_size
  );

  /* Allocate stack and run */
  void *user_rsp = alloc_user_stack();
  if(!user_rsp) {
    console_print("[USER] Failed to allocate stack\n");
    return 0;
  }

  tss_set_rsp0((u64)&kernel_stack[sizeof(kernel_stack)]);

  console_print("[USER] Entering Ring 3...\n");

  return user_enter((void *)code_addr, user_rsp);
}
