/**
 * @file src/drivers/mouse_ps2/mouse_ps2.c
 * @brief PS/2 mouse driver (i8042 second port).
 *
 * QEMU routes pointer motion to the emulated PS/2 mouse by default, so this is
 * the driver that actually carries the pointer. Decoded 3-byte packets feed the
 * kernel mouse broker via @ref mouse_post_event.
 */

#include <alcor2/arch/idt.h>
#include <alcor2/arch/io.h>
#include <alcor2/arch/pic.h>
#include <alcor2/drivers/console.h>
#include <alcor2/drivers/mouse.h>

#define PS2_DATA   0x60
#define PS2_STATUS 0x64
#define PS2_CMD    0x64

/* i8042 status register bits. */
#define PS2_ST_OUTPUT_FULL 0x01 /* data available to read on 0x60      */
#define PS2_ST_INPUT_FULL  0x02 /* controller input buffer busy        */
#define PS2_ST_AUX_DATA    0x20 /* output came from the AUX (mouse) port */

/* i8042 controller commands. */
#define PS2_CMD_READ_CONFIG  0x20
#define PS2_CMD_WRITE_CONFIG 0x60
#define PS2_CMD_ENABLE_AUX   0xA8
#define PS2_CMD_WRITE_AUX    0xD4

/* Config byte bits. Clock bits are active-low: 1 = port disabled. */
#define PS2_CFG_KBD_IRQ 0x01 /* first-port (keyboard) IRQ enable  */
#define PS2_CFG_AUX_IRQ 0x02 /* second-port (mouse) IRQ enable    */
#define PS2_CFG_KBD_CLK 0x10 /* first-port clock disable          */
#define PS2_CFG_AUX_CLK 0x20 /* second-port clock disable         */

/* Mouse device commands (sent through the AUX port). */
#define MOUSE_CMD_SET_DEFAULTS 0xF6
#define MOUSE_CMD_ENABLE       0xF4
#define MOUSE_ACK              0xFA

/* Packet byte 0 flag bits. */
#define PKT_BTN_LEFT   0x01
#define PKT_BTN_RIGHT  0x02
#define PKT_BTN_MIDDLE 0x04
#define PKT_X_SIGN     0x10
#define PKT_Y_SIGN     0x20
#define PKT_X_OVERFLOW 0x40
#define PKT_Y_OVERFLOW 0x80

static u8 s_packet[3];
static u8 s_phase; /* 0..2 index into s_packet */

/* The i8042 answers within a few microseconds; io_wait() is ~1us per call, so
 * this bounds every poll at ~100ms — long enough to never time out on real
 * hardware, short enough that a missing controller fails init promptly rather
 * than hanging the boot. */
#define PS2_POLL_TIMEOUT_US 100000

/** @brief Spin until the input buffer is free. @return false on timeout. */
static bool ps2_wait_input_clear(void)
{
  for(int i = 0; i < PS2_POLL_TIMEOUT_US; i++) {
    if((inb(PS2_STATUS) & PS2_ST_INPUT_FULL) == 0)
      return true;
    io_wait();
  }
  return false;
}

/** @brief Spin until a byte is available to read. @return false on timeout. */
static bool ps2_wait_output_full(void)
{
  for(int i = 0; i < PS2_POLL_TIMEOUT_US; i++) {
    if(inb(PS2_STATUS) & PS2_ST_OUTPUT_FULL)
      return true;
    io_wait();
  }
  return false;
}

/** @brief Send a command byte to the controller (port 0x64). */
static void ps2_cmd(u8 cmd)
{
  ps2_wait_input_clear();
  outb(PS2_CMD, cmd);
}

/** @brief Send a data byte to the controller (port 0x60). */
static void ps2_write_data(u8 data)
{
  ps2_wait_input_clear();
  outb(PS2_DATA, data);
}

/**
 * @brief Read one byte from the controller.
 * @return The byte, or 0xFF on timeout (never a valid ACK, so a dead
 *         controller reads as a failed command).
 */
static u8 ps2_read_data(void)
{
  if(!ps2_wait_output_full())
    return 0xFF;
  return inb(PS2_DATA);
}

/**
 * @brief Send a command to the mouse via the AUX port.
 * @param cmd Mouse command byte.
 * @return The mouse's response (0xFA = ACK).
 */
static u8 mouse_cmd(u8 cmd)
{
  ps2_cmd(PS2_CMD_WRITE_AUX);
  ps2_write_data(cmd);
  return ps2_read_data();
}

/**
 * @brief IRQ 12 handler: accumulate one mouse packet byte, post on completion.
 *
 * Reads a single byte and only if the controller tags it AUX. Consuming a
 * keyboard byte here would desync the keyboard stream (dropped make/break codes
 * stick keys). The i8042 raises one IRQ per byte, so one read per call is
 * right.
 */
static void mouse_ps2_irq(u8 irq)
{
  (void)irq;

  u8 status = inb(PS2_STATUS);
  if(!(status & PS2_ST_OUTPUT_FULL) || !(status & PS2_ST_AUX_DATA))
    return;

  u8 data = inb(PS2_DATA);

  if(s_phase == 0 && !(data & 0x08))
    return; /* bit 3 of byte 0 is always 1; resync on a stray byte */

  s_packet[s_phase++] = data;
  if(s_phase < 3)
    return;
  s_phase = 0;

  u8 flags = s_packet[0];

  /* Byte 0 bit 3 is always 1 in a valid packet. If it isn't, the stream has
   * slipped a byte (dx/dy would be misread as flags, swapping axes) — drop the
   * packet and resync rather than warp the cursor sideways/up. */
  if(!(flags & 0x08))
    return;

  if(flags & (PKT_X_OVERFLOW | PKT_Y_OVERFLOW))
    return; /* drop overflowed packets rather than warp the cursor */

  i32 dx = (i32)s_packet[1];
  i32 dy = (i32)s_packet[2];
  if(flags & PKT_X_SIGN)
    dx -= 256;
  if(flags & PKT_Y_SIGN)
    dy -= 256;

  /* PS/2 reports +Y upward; screen space is +Y downward. */
  dy = -dy;

  u8 buttons = 0;
  if(flags & PKT_BTN_LEFT)
    buttons |= ALCOR2_MOUSE_BTN_LEFT;
  if(flags & PKT_BTN_RIGHT)
    buttons |= ALCOR2_MOUSE_BTN_RIGHT;
  if(flags & PKT_BTN_MIDDLE)
    buttons |= ALCOR2_MOUSE_BTN_MIDDLE;

  mouse_post_event(dx, dy, 0, buttons);
}

/**
 * @brief Enable the AUX port, configure IRQs/clocks, start data reporting.
 * @return @c true if the mouse acknowledged, @c false otherwise.
 */
bool mouse_ps2_init(void)
{
  ps2_cmd(PS2_CMD_ENABLE_AUX);

  /* Update the config byte: turn on both ports' IRQs and clocks. Explicitly
   * (re)enabling the keyboard's IRQ and clock matters because ENABLE_AUX and a
   * config rewrite can otherwise leave the first port disabled, killing the
   * keyboard. */
  ps2_cmd(PS2_CMD_READ_CONFIG);
  u8 cfg = ps2_read_data();
  cfg |= PS2_CFG_KBD_IRQ | PS2_CFG_AUX_IRQ;    /* IRQ 1 + IRQ 12 */
  cfg &= ~(PS2_CFG_KBD_CLK | PS2_CFG_AUX_CLK); /* both clocks on */
  ps2_cmd(PS2_CMD_WRITE_CONFIG);
  ps2_write_data(cfg);

  /* Initialise the mouse: defaults then enable data reporting. */
  if(mouse_cmd(MOUSE_CMD_SET_DEFAULTS) != MOUSE_ACK) {
    console_print("[mouse-ps2] no ACK to SET_DEFAULTS — absent?\n");
    return false;
  }
  if(mouse_cmd(MOUSE_CMD_ENABLE) != MOUSE_ACK) {
    console_print("[mouse-ps2] no ACK to ENABLE\n");
    return false;
  }

  s_phase = 0;
  irq_register(IRQ_MOUSE, mouse_ps2_irq);
  pic_unmask(IRQ_MOUSE);

  console_print("[mouse-ps2] online (IRQ 12)\n");
  return true;
}
