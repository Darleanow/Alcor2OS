/**
 * @file include/alcor2/idt.h
 * @brief Interrupt Descriptor Table setup.
 *
 * Structures and functions for managing the x86_64 IDT and interrupt handling.
 */

#ifndef ALCOR2_IDT_H
#define ALCOR2_IDT_H

#include <alcor2/types.h>

/** @brief Number of IDT entries. */
#define IDT_ENTRIES 256

/** @brief Interrupt gate (clears IF). */
#define IDT_GATE_INT 0x8E

/** @brief Trap gate (preserves IF). */
#define IDT_GATE_TRAP 0x8F

/**
 * @brief IDT entry (16 bytes on x86_64).
 */
typedef struct PACKED
{
  u16 offset_low;
  u16 selector;
  u8  ist;
  u8  flags;
  u16 offset_mid;
  u32 offset_high;
  u32 reserved;
} idt_entry_t;

/**
 * @brief IDT pointer (for LIDT instruction).
 */
typedef struct PACKED
{
  u16 limit; /**< Size of IDT minus one. */
  u64 base;  /**< Linear address of the IDT. */
} idt_ptr_t;

/**
 * @brief Saved interrupt frame (pushed by ISR stub + CPU).
 */
typedef struct PACKED
{
  u64 r15, r14, r13, r12, r11, r10, r9, r8;
  u64 rdi, rsi, rbp, rdx, rcx, rbx, rax;
  u64 vector;
  u64 error_code;
  u64 rip;
  u64 cs;
  u64 rflags;
  u64 rsp;
  u64 ss;
} interrupt_frame_t;

/**
 * @brief Initialize the IDT and install default handlers.
 */
void idt_init(void);

/**
 * @brief Set an IDT entry.
 * @param vector Interrupt vector number (0-255).
 * @param handler Address of the handler function.
 * @param flags Gate type and DPL.
 */
void idt_set_gate(u8 vector, void *handler, u8 flags);

#endif
