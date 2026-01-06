/**
 * @file src/drivers/pic/pic.c
 * @brief 8259 PIC driver for IRQ management.
 */

#include <alcor2/io.h>
#include <alcor2/pic.h>

#define PIC1_CMD  0x20
#define PIC1_DATA 0x21
#define PIC2_CMD  0xA0
#define PIC2_DATA 0xA1

#define ICW1_INIT 0x10
#define ICW1_ICW4 0x01
#define ICW4_8086 0x01

#define PIC_EOI   0x20

/**
 * @brief Initialize and remap the 8259 PIC.
 * 
 * Remaps the PIC IRQs from 0-15 to 32-47 to avoid conflicts with CPU exceptions.
 * Initially masks all IRQs (they must be explicitly unmasked).
 */
void pic_init(void)
{
  u8 mask1 = inb(PIC1_DATA);
  u8 mask2 = inb(PIC2_DATA);

  outb(PIC1_CMD, ICW1_INIT | ICW1_ICW4);
  io_wait();
  outb(PIC2_CMD, ICW1_INIT | ICW1_ICW4);
  io_wait();

  outb(PIC1_DATA, 0x20);
  io_wait();
  outb(PIC2_DATA, 0x28);
  io_wait();

  outb(PIC1_DATA, 0x04);
  io_wait();
  outb(PIC2_DATA, 0x02);
  io_wait();

  outb(PIC1_DATA, ICW4_8086);
  io_wait();
  outb(PIC2_DATA, ICW4_8086);
  io_wait();

  outb(PIC1_DATA, mask1);
  outb(PIC2_DATA, mask2);

  outb(PIC1_DATA, 0xFF);
  outb(PIC2_DATA, 0xFF);
}

/**
 * @brief Send End-Of-Interrupt signal to the PIC.
 * 
 * Must be called at the end of an IRQ handler to acknowledge the interrupt.
 * Automatically handles both master and slave PIC if needed.
 * 
 * @param irq IRQ number (0-15).
 */
void pic_eoi(u8 irq)
{
  if(irq >= 8)
    outb(PIC2_CMD, PIC_EOI);
  outb(PIC1_CMD, PIC_EOI);
}

/**
 * @brief Mask (disable) an IRQ line.
 * 
 * Prevents the specified IRQ from triggering interrupts.
 * 
 * @param irq IRQ number (0-15).
 */
void pic_mask(u8 irq)
{
  u16 port = (irq < 8) ? PIC1_DATA : PIC2_DATA;
  u8  line = (irq < 8) ? irq : irq - 8;
  outb(port, inb(port) | (1 << line));
}

/**
 * @brief Unmask (enable) an IRQ line.
 * 
 * Allows the specified IRQ to trigger interrupts.
 * 
 * @param irq IRQ number (0-15).
 */
void pic_unmask(u8 irq)
{
  u16 port = (irq < 8) ? PIC1_DATA : PIC2_DATA;
  u8  line = (irq < 8) ? irq : irq - 8;
  outb(port, inb(port) & ~(1 << line));
}
