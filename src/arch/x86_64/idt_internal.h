/* Test-only: exposes the static IDT array and asm-entry C handlers.
 * Included by idt.c (for the prototypes) and by tests/unit/test_idt.c. */

#ifndef ALCOR2_ARCH_X86_64_IDT_INTERNAL_H
#define ALCOR2_ARCH_X86_64_IDT_INTERNAL_H

#include <alcor2/arch/idt.h>

extern idt_entry_t idt[IDT_ENTRIES];

void               irq_handler(u8 irq);
void               exception_handler(interrupt_frame_t *frame);

#endif
