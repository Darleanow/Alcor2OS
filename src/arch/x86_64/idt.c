/**
 * @file src/arch/x86_64/idt.c
 * @brief Interrupt Descriptor Table and exception handlers.
 */

#include <alcor2/console.h>
#include <alcor2/cpu.h>
#include <alcor2/idt.h>
#include <alcor2/pic.h>

extern void        pit_tick(void);
extern void        keyboard_irq(void);

static idt_entry_t idt[IDT_ENTRIES];
static idt_ptr_t   idtr;

extern void       *isr_stub_table[];
extern void       *irq_stub_table[];

/** @brief CPU exception names (vectors 0-31). */
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
    "Reserved"
};

/**
 * @brief Set an IDT entry to point to a handler.
 * 
 * Configures an interrupt gate with the specified handler address and flags.
 * Uses kernel code segment selector (0x28) and IST 0.
 * 
 * @param vector Interrupt vector number (0-255).
 * @param handler Address of the interrupt handler function.
 * @param flags Gate type and DPL (e.g., IDT_GATE_INT, IDT_GATE_TRAP).
 */
void idt_set_gate(u8 vector, void *handler, u8 flags)
{
  u64 addr = (u64)handler;

  idt[vector].offset_low  = addr & 0xFFFF;
  idt[vector].selector    = 0x28;
  idt[vector].ist         = 0;
  idt[vector].flags       = flags;
  idt[vector].offset_mid  = (addr >> 16) & 0xFFFF;
  idt[vector].offset_high = (addr >> 32) & 0xFFFFFFFF;
  idt[vector].reserved    = 0;
}

/**
 * @brief Generic CPU exception handler.
 * 
 * Displays detailed exception information and halts the system.
 * Handles all CPU exceptions (vectors 0-31) including page faults.
 * 
 * @param frame Saved interrupt frame with CPU state and exception info.
 */
__attribute__((used))
void exception_handler(interrupt_frame_t *frame)
{
  console_print("\n\n*** KERNEL PANIC ***\n\n");

  if(frame->vector < 32) {
    console_print("Exception: ");
    console_print(exception_names[frame->vector]);
  } else {
    console_print("Interrupt: ");
    console_printf("%d", (int)frame->vector);
  }

  console_print("\n\n");
  console_printf("RIP: 0x%x\n", frame->rip);
  console_printf("RSP: 0x%x\n", frame->rsp);
  console_printf("ERR: 0x%x\n", frame->error_code);

  /* For Page Fault, show CR2 (faulting address) */
  if(frame->vector == 14) {
    u64 cr2;
    __asm__ volatile("mov %%cr2, %0" : "=r"(cr2));
    console_printf("CR2: 0x%x (faulting address)\n", cr2);
  }

  cpu_halt();
}

/**
 * @brief Generic hardware IRQ handler.
 * 
 * Dispatches IRQs to their respective device handlers (timer, keyboard, etc.)
 * and sends EOI to the PIC.
 * 
 * @param irq IRQ number (0-15).
 */
__attribute__((used))
void irq_handler(u8 irq)
{
  switch(irq) {
  case IRQ_TIMER:
    pit_tick();
    break;
  case IRQ_KEYBOARD:
    keyboard_irq();
    break;
  default:
    break;
  }
  pic_eoi(irq);
}

/**
 * @brief Initialize the Interrupt Descriptor Table.
 * 
 * Installs exception handlers for vectors 0-31 and IRQ handlers for
 * vectors 32-47, then loads the IDT into the CPU.
 */
void idt_init(void)
{
  for(u16 i = 0; i < 32; i++) {
    idt_set_gate(i, isr_stub_table[i], IDT_GATE_INT);
  }

  for(u16 i = 0; i < 16; i++) {
    idt_set_gate(32 + i, irq_stub_table[i], IDT_GATE_INT);
  }

  idtr.limit = sizeof(idt) - 1;
  idtr.base  = (u64)&idt;

  __asm__ volatile("lidt %0" : : "m"(idtr));
}
