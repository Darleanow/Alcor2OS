/**
 * @file include/alcor2/io.h
 * @brief x86 I/O port access functions.
 *
 * Inline wrappers for `in`/`out` instructions.
 */

#ifndef ALCOR2_IO_H
#define ALCOR2_IO_H

#include <alcor2/types.h>

/**
 * @brief Write a byte to an I/O port.
 * @param port I/O port number.
 * @param val Value to write.
 */
static inline void outb(u16 port, u8 val)
{
  __asm__ volatile("outb %0, %1" : : "a"(val), "Nd"(port));
}

/**
 * @brief Read a byte from an I/O port.
 * @param port I/O port number.
 * @return Value read from port.
 */
static inline u8 inb(u16 port)
{
  u8 val;
  __asm__ volatile("inb %1, %0" : "=a"(val) : "Nd"(port));
  return val;
}

/**
 * @brief Write a word to an I/O port.
 * @param port I/O port number.
 * @param val Value to write.
 */
static inline void outw(u16 port, u16 val)
{
  __asm__ volatile("outw %0, %1" : : "a"(val), "Nd"(port));
}

/**
 * @brief Read a word from an I/O port.
 * @param port I/O port number.
 * @return Value read from port.
 */
static inline u16 inw(u16 port)
{
  u16 val;
  __asm__ volatile("inw %1, %0" : "=a"(val) : "Nd"(port));
  return val;
}

/**
 * @brief Short I/O delay (write to port 0x80).
 */
static inline void io_wait(void)
{
  outb(0x80, 0);
}

#endif
