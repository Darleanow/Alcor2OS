/* Unit tests for src/arch/x86_64/idt.c.
 * Tested: idt_set_gate, irq_register, irq_handler, idt_set_proc_hooks.
 * Skipped (privileged / noreturn): idt_init (lidt), exception_handler
 * (cpu_halt + cr2). Those go to the QEMU runner. */

#include "test_common.h"

#include <alcor2/arch/gdt.h>
#include <alcor2/arch/idt.h>
#include <alcor2/arch/pic.h>
#include <arch/x86_64/idt_internal.h>

/* Link-only stubs — pulled in by code paths we never execute here. */
void *isr_stub_table[256];
void *irq_stub_table[16];
void  console_print(const char *s)
{
  (void)s;
}
void console_printf(const char *fmt, ...)
{
  (void)fmt;
}
NORETURN void cpu_halt(void)
{
  for(;;) {
  }
}

/* pic_eoi is actually called by irq_handler — record it. */
static unsigned eoi_count;
static u8       eoi_last_irq;
void            pic_eoi(u8 irq)
{
  eoi_count++;
  eoi_last_irq = irq;
}

static unsigned handler_count;
static u8       handler_last_irq;
static void     recording_handler(u8 irq)
{
  handler_count++;
  handler_last_irq = irq;
}

static int setup(void **state)
{
  (void)state;
  eoi_count = handler_count = 0;
  eoi_last_irq = handler_last_irq = 0;
  for(unsigned i = 0; i < PIC_IRQ_LINE_COUNT; i++)
    irq_register((u8)i, NULL);
  return 0;
}

static u64 gate_addr(const idt_entry_t *e)
{
  return (u64)e->offset_low | ((u64)e->offset_mid << 16) |
         ((u64)e->offset_high << 32);
}

/* idt_set_gate */

static void set_gate_encodes_handler_address(void **state)
{
  (void)state;
  void *h = (void *)0x1122334455667788ULL;
  idt_set_gate(7, h, IDT_GATE_INT);
  assert_int_equal(gate_addr(&idt[7]), (u64)h);
  assert_int_equal(idt[7].offset_low, 0x7788);
  assert_int_equal(idt[7].offset_mid, 0x5566);
  assert_int_equal(idt[7].offset_high, 0x11223344u);
}

static void set_gate_uses_kernel_code_selector(void **state)
{
  (void)state;
  idt_set_gate(0, (void *)0x1000, IDT_GATE_INT);
  assert_int_equal(idt[0].selector, GDT_KERNEL_CODE);
}

static void set_gate_clears_ist_and_reserved(void **state)
{
  (void)state;
  idt_set_gate(1, (void *)0x1000, IDT_GATE_INT);
  assert_int_equal(idt[1].ist, 0);
  assert_int_equal(idt[1].reserved, 0);
}

static void set_gate_passes_flags_through(void **state)
{
  (void)state;
  idt_set_gate(3, (void *)0x1000, IDT_GATE_INT);
  assert_int_equal(idt[3].flags, IDT_GATE_INT);
  idt_set_gate(3, (void *)0x1000, IDT_GATE_TRAP);
  assert_int_equal(idt[3].flags, IDT_GATE_TRAP);
}

static void set_gate_vector_255(void **state)
{
  (void)state;
  idt_set_gate(255, (void *)0xABCDEF01ULL, IDT_GATE_INT);
  assert_int_equal(gate_addr(&idt[255]), 0xABCDEF01ULL);
}

static void set_gate_zero_handler(void **state)
{
  (void)state;
  idt_set_gate(10, NULL, IDT_GATE_INT);
  assert_int_equal(gate_addr(&idt[10]), 0);
}

/* irq_register / irq_handler */

static void irq_handler_dispatches_registered_handler(void **state)
{
  (void)state;
  irq_register(5, recording_handler);
  irq_handler(5);
  assert_int_equal(handler_count, 1);
  assert_int_equal(handler_last_irq, 5);
}

static void irq_handler_always_eois(void **state)
{
  (void)state;
  irq_register(3, recording_handler);
  irq_handler(3);
  assert_int_equal(eoi_count, 1);
  assert_int_equal(eoi_last_irq, 3);
}

static void irq_handler_eois_without_handler(void **state)
{
  (void)state;
  irq_handler(9);
  assert_int_equal(handler_count, 0);
  assert_int_equal(eoi_count, 1);
  assert_int_equal(eoi_last_irq, 9);
}

static void irq_handler_out_of_range_still_eois(void **state)
{
  (void)state;
  irq_handler(200);
  assert_int_equal(handler_count, 0);
  assert_int_equal(eoi_count, 1);
  assert_int_equal(eoi_last_irq, 200);
}

static void irq_register_bounds_check(void **state)
{
  (void)state;
  /* PIC_IRQ_LINE_COUNT and above must be ignored — not an OOB write. */
  irq_register(PIC_IRQ_LINE_COUNT, recording_handler);
  irq_register(255, recording_handler);
  irq_handler(0);
  assert_int_equal(handler_count, 0);
}

static void irq_register_deregisters_on_null(void **state)
{
  (void)state;
  irq_register(2, recording_handler);
  irq_register(2, NULL);
  irq_handler(2);
  assert_int_equal(handler_count, 0);
  assert_int_equal(eoi_count, 1);
}

static void idt_set_proc_hooks_stores_hooks(void **state)
{
  (void)state;
  idt_proc_hooks_t h = {0};
  idt_set_proc_hooks(h);
}

int main(void)
{
  const struct CMUnitTest tests[] = {
      cmocka_unit_test(set_gate_encodes_handler_address),
      cmocka_unit_test(set_gate_uses_kernel_code_selector),
      cmocka_unit_test(set_gate_clears_ist_and_reserved),
      cmocka_unit_test(set_gate_passes_flags_through),
      cmocka_unit_test(set_gate_vector_255),
      cmocka_unit_test(set_gate_zero_handler),
      cmocka_unit_test_setup(irq_handler_dispatches_registered_handler, setup),
      cmocka_unit_test_setup(irq_handler_always_eois, setup),
      cmocka_unit_test_setup(irq_handler_eois_without_handler, setup),
      cmocka_unit_test_setup(irq_handler_out_of_range_still_eois, setup),
      cmocka_unit_test_setup(irq_register_bounds_check, setup),
      cmocka_unit_test_setup(irq_register_deregisters_on_null, setup),
      cmocka_unit_test(idt_set_proc_hooks_stores_hooks),
  };
  return cmocka_run_group_tests(tests, NULL, NULL);
}
