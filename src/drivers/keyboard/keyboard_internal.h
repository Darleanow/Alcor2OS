/* Test-only: expose kb_push and the ring-buffer state.
 * Included by keyboard.c and by tests/unit/test_keyboard.c. */

#ifndef ALCOR2_DRIVERS_KEYBOARD_INTERNAL_H
#define ALCOR2_DRIVERS_KEYBOARD_INTERNAL_H

#include <alcor2/types.h>

#define KB_BUFFER_SIZE 256

extern u8           kb_buffer[KB_BUFFER_SIZE];
extern volatile u32 kb_read_pos;
extern volatile u32 kb_write_pos;
extern u32          kb_drop_count;

void                kb_push(u8 b);

#endif
