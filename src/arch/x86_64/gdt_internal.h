/* Test-only: expose the static encoding helpers for unit testing.
 * Included by gdt.c (for the prototypes) and by tests/unit/test_gdt.c. */

#ifndef ALCOR2_ARCH_X86_64_GDT_INTERNAL_H
#define ALCOR2_ARCH_X86_64_GDT_INTERNAL_H

#include <alcor2/arch/gdt.h>

void gdt_set_entry(gdt_entry_t *entry, u8 access, u8 flags);
void gdt_set_tss(gdt_tss_entry_t *entry, u64 base);

#endif
