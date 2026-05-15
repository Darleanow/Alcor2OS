/**
 * @file src/arch/x86_64/idt.c
 * @brief Interrupt Descriptor Table and exception handlers.
 */

#include <alcor2/arch/cpu.h>
#include <alcor2/arch/gdt.h>
#include <alcor2/arch/idt.h>
#include <alcor2/arch/pic.h>
#include <alcor2/drivers/ata.h>
#include <alcor2/drivers/console.h>
#include <alcor2/proc/proc.h>

extern void        pit_tick(void);
extern void        keyboard_irq(void);

static idt_entry_t idt[IDT_ENTRIES];
static idt_ptr_t   idtr;

extern void       *isr_stub_table[];
extern void       *irq_stub_table[];

/** Vector layout: CPU exceptions, then PIC IRQs at 32..47.
 *  @c X86_SEGMENT_RPL_MASK masks the CS/SS RPL; user ring is 3. */
enum
{
  X86_EXCEPTION_VECTOR_COUNT = 32,
  X86_VEC_PAGE_FAULT         = 14,
  X86_SEGMENT_RPL_MASK       = 3,
};

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

// cppcheck-suppress unusedFunction
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

  const proc_t *p = proc_current();
  if(p) {
    console_print(" [");
    console_print(p->name);
    console_print("]");
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

  if(user_fault) {
    console_print("Killing faulting process.\n");
    proc_exit(-11);
  }

  cpu_halt();
}

/** @brief IRQ handler callback signature. */
typedef void (*irq_handler_fn)(u8 irq);

/** @brief Descriptor for a hardware IRQ routing entry. */
typedef struct
{
  u8             irq;     /**< Hardware IRQ number (0-15) */
  const char    *name;    /**< Symbolic name for tracing */
  irq_handler_fn handler; /**< Implementation callback */
} irq_def_t;

static void irq__pit_wrapper(u8 i)
{
  (void)i;
  pit_tick();
}
static void irq__kbd_wrapper(u8 i)
{
  (void)i;
  keyboard_irq();
}
static void irq__ata0_wrapper(u8 i)
{
  (void)i;
  ata_irq(0);
}
static void irq__ata1_wrapper(u8 i)
{
  (void)i;
  ata_irq(1);
}

#define IRQ_DEF(v, n, h)                                                       \
  {                                                                            \
    (v), (n), (h)                                                              \
  }
#define IRQ_END                                                                \
  {                                                                            \
    0, NULL, NULL                                                              \
  }

/** @brief IRQ routing table, sentinel-terminated. */
static const irq_def_t irq_table[] = {
    IRQ_DEF(IRQ_TIMER, "pit", irq__pit_wrapper),
    IRQ_DEF(IRQ_KEYBOARD, "keyboard", irq__kbd_wrapper),
    IRQ_DEF(IRQ_ATA_PRIMARY, "ata0", irq__ata0_wrapper),
    IRQ_DEF(IRQ_ATA_SECONDARY, "ata1", irq__ata1_wrapper), IRQ_END
};

/* Set to 1 to trace hardware interrupts */
#define IRQ_TRACE 0

// cppcheck-suppress unusedFunction
void irq_handler(u8 irq)
{
  for(const irq_def_t *d = irq_table; d->name != NULL; d++) {
    if(d->irq == irq) {
#if IRQ_TRACE
      if(irq != IRQ_TIMER)
        console_printf("[irq] %d (%s)\n", (int)irq, d->name);
#endif
      d->handler(irq);
      break;
    }
  }

  pic_eoi(irq);
}

void idt_init(void)
{
  for(u16 i = 0; i < X86_EXCEPTION_VECTOR_COUNT; i++) {
    idt_set_gate(i, isr_stub_table[i], IDT_GATE_INT);
  }

  for(u16 i = 0; i < PIC_IRQ_LINE_COUNT; i++) {
    idt_set_gate(
        X86_EXCEPTION_VECTOR_COUNT + i, irq_stub_table[i], IDT_GATE_INT
    );
  }

  idtr.limit = sizeof(idt) - 1;
  idtr.base  = (u64)&idt;

  __asm__ volatile("lidt %0" : : "m"(idtr));
}
