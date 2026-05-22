/**
 * @file src/arch/x86_64/idt.c
 * @brief Interrupt Descriptor Table and exception handlers.
 */

#include <alcor2/arch/cpu.h>
#include <alcor2/arch/gdt.h>
#include <alcor2/arch/idt.h>
#include <alcor2/arch/pic.h>
#include <alcor2/drivers/console.h>

static idt_entry_t idt[IDT_ENTRIES];
static idt_ptr_t   idtr;

extern void       *isr_stub_table[];
extern void       *irq_stub_table[];

enum
{
  X86_EXCEPTION_VECTOR_COUNT = 32,
  X86_VEC_PAGE_FAULT         = 14,
  X86_SEGMENT_RPL_MASK       = 3,
};

static const char *exception_names[] = {
    "Division Error",
    "Debug",
    "NMI",
    "Breakpoint",
    "Overflow",
    "Bound Range Exceeded",
    "Invalid Opcode",
    "Device Not Available",
    "Double Fault",
    "Coprocessor Segment Overrun",
    "Invalid TSS",
    "Segment Not Present",
    "Stack-Segment Fault",
    "General Protection Fault",
    "Page Fault",
    "Reserved",
    "x87 FPU Error",
    "Alignment Check",
    "Machine Check",
    "SIMD Floating-Point",
    "Virtualization",
    "Control Protection",
    "Reserved",
    "Reserved",
    "Reserved",
    "Reserved",
    "Reserved",
    "Reserved",
    "Hypervisor Injection",
    "VMM Communication",
    "Security Exception",
    "Reserved",
};

static idt_proc_hooks_t g_proc_hooks;

void                    idt_set_proc_hooks(idt_proc_hooks_t hooks)
{
  g_proc_hooks = hooks;
}

void idt_set_gate(u8 vector, void *handler, u8 flags)
{
  u64 addr = (u64)handler;

  idt[vector].offset_low  = addr & 0xFFFF;
  idt[vector].selector    = GDT_KERNEL_CODE;
  idt[vector].ist         = 0;
  idt[vector].flags       = flags;
  idt[vector].offset_mid  = (addr >> 16) & 0xFFFF;
  idt[vector].offset_high = (addr >> 32) & 0xFFFFFFFF;
  idt[vector].reserved    = 0;
}

void exception_handler(interrupt_frame_t *frame)
{
  int user_fault = (frame->cs & X86_SEGMENT_RPL_MASK) == X86_SEGMENT_RPL_MASK;

  if(!user_fault)
    console_print("\n\n*** KERNEL PANIC ***\n\n");

  if(frame->vector < X86_EXCEPTION_VECTOR_COUNT) {
    console_print("Exception: ");
    console_print(exception_names[frame->vector]);
  } else {
    console_print("Interrupt: ");
    console_printf("%d", (int)frame->vector);
  }

  if(g_proc_hooks.current_name) {
    const char *name = g_proc_hooks.current_name();
    if(name) {
      console_print(" [");
      console_print(name);
      console_print("]");
    }
  }
  console_print("\n");

  console_printf("RIP: 0x%lx\n", frame->rip);
  console_printf("RSP: 0x%lx\n", frame->rsp);
  console_printf("ERR: 0x%lx\n", frame->error_code);

  if(frame->vector == X86_VEC_PAGE_FAULT) {
    u64 cr2;
    __asm__ volatile("mov %%cr2, %0" : "=r"(cr2));
    console_printf("CR2: 0x%lx\n", cr2);
  }

  if(user_fault && g_proc_hooks.exit) {
    console_print("Killing faulting process.\n");
    g_proc_hooks.exit(-11);
  }

  cpu_halt();
}

static irq_handler_fn irq_handlers[PIC_IRQ_LINE_COUNT];

void                  irq_register(u8 irq, irq_handler_fn handler)
{
  if(irq < PIC_IRQ_LINE_COUNT)
    irq_handlers[irq] = handler;
}

#define IRQ_TRACE 0

void irq_handler(u8 irq)
{
  if(irq < PIC_IRQ_LINE_COUNT && irq_handlers[irq]) {
#if IRQ_TRACE
    if(irq != IRQ_TIMER)
      console_printf("[irq] %d\n", (int)irq);
#endif
    irq_handlers[irq](irq);
  }

  pic_eoi(irq);
}

void idt_init(void)
{
  for(u16 i = 0; i < X86_EXCEPTION_VECTOR_COUNT; i++)
    idt_set_gate(i, isr_stub_table[i], IDT_GATE_INT);

  for(u16 i = 0; i < PIC_IRQ_LINE_COUNT; i++)
    idt_set_gate(
        X86_EXCEPTION_VECTOR_COUNT + i, irq_stub_table[i], IDT_GATE_INT
    );

  idtr.limit = sizeof(idt) - 1;
  idtr.base  = (u64)&idt;

  __asm__ volatile("lidt %0" : : "m"(idtr));
}
