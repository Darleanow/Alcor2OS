/**
 * @file include/alcor2/pic.h
 * @brief 8259 PIC (Programmable Interrupt Controller) driver.
 *
 * Functions to initialize, mask/unmask, and acknowledge IRQs.
 */

#ifndef ALCOR2_PIC_H
#define ALCOR2_PIC_H

#include <alcor2/types.h>

/** @brief IRQ number for PIT timer. */
#define IRQ_TIMER 0

/** @brief IRQ number for PS/2 keyboard. */
#define IRQ_KEYBOARD 1

/**
 * @brief Initialize and remap the PIC.
 */
void pic_init(void);

/**
 * @brief Send End-Of-Interrupt signal.
 * @param irq IRQ number (0-15).
 */
void pic_eoi(u8 irq);

/**
 * @brief Mask (disable) an IRQ line.
 * @param irq IRQ number (0-15).
 */
void pic_mask(u8 irq);

/**
 * @brief Unmask (enable) an IRQ line.
 * @param irq IRQ number (0-15).
 */
void pic_unmask(u8 irq);

#endif
