/**
 * @file src/drivers/keyboard/keyboard.c
 * @brief PS/2 keyboard driver — raw scancode bytes only (no layout).
 */

#include <alcor2/arch/cpu.h>
#include <alcor2/arch/io.h>
#include <alcor2/arch/pic.h>
#include <alcor2/drivers/keyboard.h>

#define KB_DATA_PORT 0x60
#define KB_CMD_PORT  0x64

/** @brief Circular input buffer size. */
#define KB_BUFFER_SIZE 256

static u8           kb_buffer[KB_BUFFER_SIZE];
static volatile u32 kb_read_pos  = 0;
static volatile u32 kb_write_pos = 0;

static void kb_push(u8 b)
{
  u32 next = (kb_write_pos + 1) % KB_BUFFER_SIZE;
  if(next != kb_read_pos)
    kb_buffer[kb_write_pos] = b, kb_write_pos = next;
}

bool keyboard_raw_available(void)
{
  return kb_read_pos != kb_write_pos;
}

u8 keyboard_raw_pop(void)
{
  cpu_disable_interrupts();
  u8 b      = kb_buffer[kb_read_pos];
  kb_read_pos = (kb_read_pos + 1) % KB_BUFFER_SIZE;
  cpu_enable_interrupts();
  return b;
}

/**
 * @brief Hardware interrupt: enqueue one raw PS/2 byte as received (set 1, incl. 0xE0 / release bit).
 */
void keyboard_irq(void)
{
  u8 scancode = inb(KB_DATA_PORT);
  kb_push(scancode);
}

void keyboard_init(void)
{
  while(inb(KB_CMD_PORT) & 0x01)
    inb(KB_DATA_PORT);

  pic_unmask(IRQ_KEYBOARD);
}
