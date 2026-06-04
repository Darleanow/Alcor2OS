/* Unit tests for src/arch/x86_64/idt.c.
 * Tested: idt_set_gate, irq_register, irq_handler, idt_set_proc_hooks.
 * Skipped (privileged / noreturn): idt_init (lidt), exception_handler
 * (cpu_halt + cr2). Those go to the QEMU runner. */

#include "test_common.h"

#include <alcor2/arch/gdt.h>
#include <alcor2/arch/idt.h>
#include <alcor2/arch/pic.h>
#include <arch/x86_64/idt_internal.h>

#include <setjmp.h>
#include <string.h>

/* Link-only stubs — pulled in by code paths we never execute here. */
void *isr_stub_table[256];
void *irq_stub_table[16];

static char g_print_buf[4096];
static int  g_print_pos;
void        console_print(const char *s)
{
  if(!s)
    return;
  int len = 0;
  while(s[len])
    len++;
  if(g_print_pos + len < (int)sizeof(g_print_buf) - 1) {
    for(int i = 0; i < len; i++)
      g_print_buf[g_print_pos++] = s[i];
    g_print_buf[g_print_pos] = '\0';
  }
}
void console_printf(const char *fmt, ...)
{
  (void)fmt;
}

/* cpu_halt: use longjmp to escape so we can test paths that call it. */
static jmp_buf g_halt_jmp;
static int     g_halt_called;
NORETURN void  cpu_halt(void)
{
  g_halt_called = 1;
  longjmp(g_halt_jmp, 1);
  __builtin_unreachable();
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
  g_halt_called = 0;
  g_print_pos   = 0;
  g_print_buf[0] = '\0';
  idt_set_proc_hooks((idt_proc_hooks_t){0});
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

/* exception_handler tests — escape cpu_halt via longjmp */

/* Kernel exception (cs RPL=0): prints KERNEL PANIC and halts */
static void exception_handler_kernel_fault_panics(void **state)
{
  (void)state;
  interrupt_frame_t frame = {0};
  frame.vector            = 13; /* General Protection Fault */
  frame.cs                = 0x08; /* kernel CS, RPL=0 */
  frame.rip               = 0xDEAD;
  frame.rsp               = 0xBEEF;

  if(setjmp(g_halt_jmp) == 0)
    exception_handler(&frame);

  assert_int_equal(g_halt_called, 1);
  assert_non_null(strstr(g_print_buf, "KERNEL PANIC"));
  assert_non_null(strstr(g_print_buf, "General Protection Fault"));
}

/* Named exception in range */
static void exception_handler_prints_exception_name(void **state)
{
  (void)state;
  interrupt_frame_t frame = {0};
  frame.vector            = 6; /* Invalid Opcode */
  frame.cs                = 0x08;

  if(setjmp(g_halt_jmp) == 0)
    exception_handler(&frame);

  assert_non_null(strstr(g_print_buf, "Invalid Opcode"));
}

/* Unknown vector (>= 32): prints "Interrupt: <n>" */
static void exception_handler_unknown_vector(void **state)
{
  (void)state;
  interrupt_frame_t frame = {0};
  frame.vector            = 50;
  frame.cs                = 0x08;

  if(setjmp(g_halt_jmp) == 0)
    exception_handler(&frame);

  assert_non_null(strstr(g_print_buf, "Interrupt:"));
}

/* User fault (cs RPL=3) without exit hook: prints message, halts */
static void exception_handler_user_fault_no_hook_halts(void **state)
{
  (void)state;
  interrupt_frame_t frame = {0};
  frame.vector            = 0; /* Division Error */
  frame.cs                = 0x1B; /* user CS, RPL=3 */

  if(setjmp(g_halt_jmp) == 0)
    exception_handler(&frame);

  assert_int_equal(g_halt_called, 1);
  assert_non_null(strstr(g_print_buf, "Division Error"));
}

/* proc_hooks.current_name returning a name adds it to output */
static const char *hook_proc_name(void)
{
  return "test_proc";
}
static void exception_handler_prints_proc_name(void **state)
{
  (void)state;
  idt_set_proc_hooks((idt_proc_hooks_t){.current_name = hook_proc_name});
  interrupt_frame_t frame = {0};
  frame.vector            = 8; /* Double Fault */
  frame.cs                = 0x08;

  if(setjmp(g_halt_jmp) == 0)
    exception_handler(&frame);

  assert_non_null(strstr(g_print_buf, "test_proc"));
}

/* proc_hooks.current_name returning NULL: no brackets added */
static const char *hook_proc_name_null(void)
{
  return NULL;
}
static void exception_handler_null_proc_name_skipped(void **state)
{
  (void)state;
  idt_set_proc_hooks((idt_proc_hooks_t){.current_name = hook_proc_name_null});
  interrupt_frame_t frame = {0};
  frame.vector            = 1;
  frame.cs                = 0x08;

  if(setjmp(g_halt_jmp) == 0)
    exception_handler(&frame);

  assert_int_equal(g_halt_called, 1);
}

/* User fault with exit hook: hook is called, then cpu_halt */
static i64  g_exit_code;
static void hook_exit(i64 code)
{
  g_exit_code = code;
  /* don't longjmp here — let cpu_halt do it */
}
static void exception_handler_user_fault_calls_exit_hook(void **state)
{
  (void)state;
  g_exit_code = 0;
  idt_set_proc_hooks((idt_proc_hooks_t){.exit = hook_exit});
  interrupt_frame_t frame = {0};
  frame.vector            = 6; /* Invalid Opcode */
  frame.cs                = 0x1B; /* user RPL=3 */

  if(setjmp(g_halt_jmp) == 0)
    exception_handler(&frame);

  assert_int_equal(g_exit_code, -11);
  assert_non_null(strstr(g_print_buf, "Killing faulting process"));
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
      /* exception_handler */
      cmocka_unit_test_setup(exception_handler_kernel_fault_panics, setup),
      cmocka_unit_test_setup(exception_handler_prints_exception_name, setup),
      cmocka_unit_test_setup(exception_handler_unknown_vector, setup),
      cmocka_unit_test_setup(exception_handler_user_fault_no_hook_halts, setup),
      cmocka_unit_test_setup(exception_handler_prints_proc_name, setup),
      cmocka_unit_test_setup(exception_handler_null_proc_name_skipped, setup),
      cmocka_unit_test_setup(
          exception_handler_user_fault_calls_exit_hook, setup
      ),
  };
  return cmocka_run_group_tests(tests, NULL, NULL);
}
