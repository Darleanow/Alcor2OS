/**
 * @file src/drivers/keyboard/keyboard.c
 * @brief PS/2 keyboard driver with scancode translation.
 */

#include <alcor2/io.h>
#include <alcor2/keyboard.h>
#include <alcor2/pic.h>

#define KB_DATA_PORT 0x60
#define KB_CMD_PORT  0x64

static key_state_t        state        = {0};
static keyboard_handler_t user_handler = 0;

/** @brief Circular input buffer size. */
#define KB_BUFFER_SIZE 256

static char         kb_buffer[KB_BUFFER_SIZE];
static volatile u32 kb_read_pos  = 0;
static volatile u32 kb_write_pos = 0;

/**
 * @brief Push character to input buffer.
 * @param c Character to push.
 */
static void         kb_buffer_push(char c)
{
  u32 next = (kb_write_pos + 1) % KB_BUFFER_SIZE;
  if(next != kb_read_pos) { /* Buffer not full */
    kb_buffer[kb_write_pos] = c;
    kb_write_pos            = next;
  }
}

/**
 * @brief Read buffered keyboard input.
 * 
 * Reads up to count characters from the keyboard input buffer.
 * Non-blocking - returns immediately with available data.
 * 
 * @param buf Destination buffer.
 * @param count Maximum number of bytes to read.
 * @return Number of bytes actually read.
 */
u64 keyboard_read(char *buf, u64 count)
{
  u64 read = 0;
  while(read < count && kb_read_pos != kb_write_pos) {
    buf[read++] = kb_buffer[kb_read_pos];
    kb_read_pos = (kb_read_pos + 1) % KB_BUFFER_SIZE;
  }
  return read;
}

/**
 * @brief Check if keyboard buffer has data available.
 * @return true if data is available, false otherwise.
 */
bool keyboard_has_data(void)
{
  return kb_read_pos != kb_write_pos;
}

/** @brief Scancode to ASCII lookup table (unshifted). */
static const char scancode_to_ascii[128] = {
    0,   0,    '1',  '2', '3',  '4', '5', '6', '7', '8', '9', '0', '-',
    '=', '\b', '\t', 'q', 'w',  'e', 'r', 't', 'y', 'u', 'i', 'o', 'p',
    '[', ']',  '\n', 0,   'a',  's', 'd', 'f', 'g', 'h', 'j', 'k', 'l',
    ';', '\'', '`',  0,   '\\', 'z', 'x', 'c', 'v', 'b', 'n', 'm', ',',
    '.', '/',  0,    '*', 0,    ' ', 0,   0,   0,   0,   0,   0,   0,
    0,   0,    0,    0,   0,    0,   '7', '8', '9', '-', '4', '5', '6',
    '+', '1',  '2',  '3', '0',  '.', 0,   0,   0,   0,   0,   0,   0,
    0,   0,    0,    0,   0,    0,   0,   0,   0,   0,   0,   0,   0,
    0,   0,    0,    0,   0,    0,   0,   0,   0,   0,   0,   0,   0,
    0,   0,    0,    0,   0,    0,   0,   0,   0,   0,   0
};

static const char scancode_to_ascii_shift[128] = {
    0,   0,    '!',  '@', '#', '$', '%', '^', '&', '*', '(', ')', '_',
    '+', '\b', '\t', 'Q', 'W', 'E', 'R', 'T', 'Y', 'U', 'I', 'O', 'P',
    '{', '}',  '\n', 0,   'A', 'S', 'D', 'F', 'G', 'H', 'J', 'K', 'L',
    ':', '"',  '~',  0,   '|', 'Z', 'X', 'C', 'V', 'B', 'N', 'M', '<',
    '>', '?',  0,    '*', 0,   ' ', 0,   0,   0,   0,   0,   0,   0,
    0,   0,    0,    0,   0,   0,   '7', '8', '9', '-', '4', '5', '6',
    '+', '1',  '2',  '3', '0', '.', 0,   0,   0,   0,   0,   0,   0,
    0,   0,    0,    0,   0,   0,   0,   0,   0,   0,   0,   0,   0,
    0,   0,    0,    0,   0,   0,   0,   0,   0,   0,   0,   0,   0,
    0,   0,    0,    0,   0,   0,   0,   0,   0,   0,   0
};

/**
 * @brief Convert scancode to ASCII character
 * @param scancode PS/2 keyboard scancode
 * @param shift true if shift is pressed
 * @return ASCII character, or 0 if not printable
 */
char keyboard_scancode_to_char(u8 scancode, bool shift)
{
  if(scancode >= 128)
    return 0;
  return shift ? scancode_to_ascii_shift[scancode]
               : scancode_to_ascii[scancode];
}

/**
 * @brief IRQ handler for keyboard interrupts.
 * 
 * Reads scancodes from the keyboard, updates modifier state, converts
 * to ASCII, and pushes to the input buffer.
 */
void keyboard_irq(void)
{
  u8   scancode = inb(KB_DATA_PORT);
  bool released = (scancode & KEY_RELEASE) != 0;
  u8   key      = scancode & ~KEY_RELEASE;

  switch(key) {
  case KEY_LSHIFT:
  case KEY_RSHIFT:
    state.shift = !released;
    break;
  case KEY_LCTRL:
    state.ctrl = !released;
    break;
  case KEY_LALT:
    state.alt = !released;
    break;
  case KEY_CAPSLOCK:
    if(!released)
      state.capslock = !state.capslock;
    break;
  default:
    if(!released) {
      bool use_shift = state.shift ^ state.capslock;
      char c         = keyboard_scancode_to_char(key, use_shift);

      /* Push to buffer for SYS_READ */
      if(c) {
        kb_buffer_push(c);
      }

      /* Call user handler if set */
      if(user_handler) {
        user_handler(c, key, state);
      }
    }
    break;
  }
}

/**
 * @brief Initialize the PS/2 keyboard driver.
 * 
 * Flushes the keyboard buffer and unmasks the keyboard IRQ.
 */
void keyboard_init(void)
{
  while(inb(KB_CMD_PORT) & 0x01)
    inb(KB_DATA_PORT);

  pic_unmask(IRQ_KEYBOARD);
}

/**
 * @brief Set a custom keyboard event handler.
 * 
 * Allows registering a callback function that will be called on each
 * key press event (in addition to buffering).
 * 
 * @param handler Callback function, or NULL to disable.
 */
void keyboard_set_handler(keyboard_handler_t handler)
{
  user_handler = handler;
}
