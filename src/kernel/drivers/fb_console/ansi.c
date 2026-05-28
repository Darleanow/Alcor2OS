/**
 * @file src/kernel/drivers/fb_console/ansi.c
 * @brief ANSI/CSI escape parser, UTF-8 decoder, and SGR/colour resolution for
 * the framebuffer console.
 *
 * The entry point @ref feed_byte is the state machine: bytes coming from
 * @ref fb_console_write_raw pass through the ANSI ESC/CSI / G0-charset
 * states before falling through to UTF-8 decoding and finally
 * @ref put_cp_at_cursor. SGR colour handling lives next to the parser
 * because @c ansi16_fg / @c ansi256_to_rgb are referenced only here.
 */

#include <alcor2/types.h>
#include <kernel/drivers/fb_console/internal.h>

/**
 * @brief Translate a DEC ACS (Special Graphics) printable ASCII byte to its
 * Unicode equivalent.
 *
 * Active only while @c ESC @c ( @c 0 has selected G0=ACS. The kernel's CP437
 * bitmap lacks most of these glyphs; the userspace atlas provides them when
 * loaded. Unrecognised bytes pass through unchanged so ncurses falling back
 * to ACS still draws plain text where a translation isn't defined.
 *
 * @param b  Input byte in the printable ASCII range.
 * @return Translated codepoint, or @p b unchanged when no mapping exists.
 */
static u32 acs_to_unicode(u8 b)
{
  switch(b) {
  case '`':
    return 0x25C6u; /* ◆ */
  case 'a':
    return 0x2592u; /* ▒ */
  case 'f':
    return 0x00B0u; /* ° */
  case 'g':
    return 0x00B1u; /* ± */
  case 'j':
    return 0x2518u; /* ┘ */
  case 'k':
    return 0x2510u; /* ┐ */
  case 'l':
    return 0x250Cu; /* ┌ */
  case 'm':
    return 0x2514u; /* └ */
  case 'n':
    return 0x253Cu; /* ┼ */
  case 'q':
    return 0x2500u; /* ─ */
  case 't':
    return 0x251Cu; /* ├ */
  case 'u':
    return 0x2524u; /* ┤ */
  case 'v':
    return 0x2534u; /* ┴ */
  case 'w':
    return 0x252Cu; /* ┬ */
  case 'x':
    return 0x2502u; /* │ */
  case 'y':
    return 0x2264u; /* ≤ */
  case 'z':
    return 0x2265u; /* ≥ */
  case '|':
    return 0x2260u; /* ≠ */
  case '~':
    return 0x00B7u; /* · */
  default:
    return (u32)b;
  }
}

/**
 * @brief Apply one C0 control character to the cursor/grid state.
 *
 * Only the four characters userspace actually emits in normal output
 * (LF/CR/BS/HT) are honoured. Everything else (BEL, etc.) is dropped — no
 * audible terminal, no in-band signalling we care about.
 *
 * @param b  Control byte (< 0x20 or 0x7F).
 */
static void handle_control(u8 b)
{
  switch(b) {
  case '\n':
    fb_ctx.cx = 0;
    fb_ctx.cy++;
    if(fb_ctx.cy >= fb_ctx.rows) {
      scroll_one();
      fb_ctx.cy = fb_ctx.rows - 1;
    }
    return;
  case '\r':
    fb_ctx.cx = 0;
    return;
  case '\b':
    if(fb_ctx.cx > 0)
      fb_ctx.cx--;
    return;
  case '\t':
    fb_ctx.cx = (fb_ctx.cx + TAB_WIDTH) & ~TAB_SNAP_MASK;
    if(fb_ctx.cx >= fb_ctx.cols)
      fb_ctx.cx = fb_ctx.cols - 1;
    return;
  default:
    return;
  }
}

/** @brief First lower-case ASCII codepoint. The DEC Special Graphics set only
 * remaps the lower-case range, so the translation gate is "is this in the ACS
 * mapping range?" — anything below stays untouched. */
#define ASCII_LOWER_A 0x60u

/** @brief First printable ASCII codepoint (space). Everything below is C0
 * control which feeds @ref handle_control. */
#define ASCII_SPACE 0x20u

/** @brief ASCII DEL (0x7F). Outside the printable range but also not a
 * standard C0 control; routed through @ref handle_control with the other
 * controls so the UTF-8 lead-byte check skips it. */
#define ASCII_DEL 0x7Fu

/** @brief First non-ASCII byte. Anything @c >= this is either a UTF-8
 * continuation (rejected as stray) or a multi-byte lead. */
#define UTF8_NON_ASCII_BASE 0x80u

/** @brief Mask isolating the leading 3 bits of a UTF-8 byte (used to detect
 * 2-byte lead pattern @c 110xxxxx). */
#define UTF8_2BYTE_LEAD_MASK 0xE0u

/** @brief 2-byte lead pattern after masking. */
#define UTF8_2BYTE_LEAD_VAL 0xC0u

/** @brief Payload bits carried by a 2-byte lead (lower 5 bits). */
#define UTF8_2BYTE_PAYLOAD_MASK 0x1Fu

/** @brief Mask isolating the leading 4 bits of a UTF-8 byte (used to detect
 * 3-byte lead pattern @c 1110xxxx). */
#define UTF8_3BYTE_LEAD_MASK 0xF0u

/** @brief 3-byte lead pattern after masking. */
#define UTF8_3BYTE_LEAD_VAL 0xE0u

/** @brief Payload bits carried by a 3-byte lead (lower 4 bits). */
#define UTF8_3BYTE_PAYLOAD_MASK 0x0Fu

/** @brief Mask isolating the leading 5 bits of a UTF-8 byte (used to detect
 * 4-byte lead pattern @c 11110xxx). */
#define UTF8_4BYTE_LEAD_MASK 0xF8u

/** @brief 4-byte lead pattern after masking. */
#define UTF8_4BYTE_LEAD_VAL 0xF0u

/** @brief Payload bits carried by a 4-byte lead (lower 3 bits). */
#define UTF8_4BYTE_PAYLOAD_MASK 0x07u

/** @brief Continuation bytes after a 2-byte lead. */
#define UTF8_2BYTE_TAIL 1u

/** @brief Continuation bytes after a 3-byte lead. */
#define UTF8_3BYTE_TAIL 2u

/** @brief Continuation bytes after a 4-byte lead. */
#define UTF8_4BYTE_TAIL 3u

/** @brief Mask isolating the high 2 bits of a UTF-8 continuation byte. */
#define UTF8_CONT_LEAD_MASK 0xC0u

/** @brief Required pattern of a continuation byte after masking
 * (@c 10xxxxxx). */
#define UTF8_CONT_LEAD_VAL 0x80u

/** @brief Payload bits carried by a UTF-8 continuation byte (lower 6 bits). */
#define UTF8_CONT_PAYLOAD_MASK 0x3Fu

/** @brief Bit-shift width of one UTF-8 continuation byte's payload. */
#define UTF8_CONT_PAYLOAD_BITS 6u

/** @brief Highest valid Unicode codepoint (Plane 16's last slot). Anything
 * above gets replaced with @c '?' rather than rendered as a phantom glyph. */
#define UNICODE_MAX 0x10FFFFu

/**
 * @brief Emit a single ASCII byte to the cursor, applying DEC ACS translation
 *        when the G0 set is the special-graphics one.
 *
 * @param b  Byte in the 0x20..0x7E printable range.
 */
static void emit_ascii(u8 b)
{
  if(fb_ctx.g0_acs && b >= ASCII_LOWER_A) {
    put_cp_at_cursor(acs_to_unicode(b));
    return;
  }
  put_cp_at_cursor((u32)b);
}

/**
 * @brief Begin a multi-byte UTF-8 sequence: store the lead bits and remaining
 *        continuation-byte count, then wait for more input.
 *
 * @param lead_bits   Payload bits carried by the lead byte.
 * @param remaining   Continuation bytes still expected.
 */
static void utf8_begin(u32 lead_bits, u8 remaining)
{
  fb_ctx.utf8_partial = lead_bits;
  fb_ctx.utf8_rem     = remaining;
}

/**
 * @brief Try to start a UTF-8 multi-byte sequence based on the high bits of
 *        @p b.
 *
 * @param b  Lead byte (≥ 0x80).
 * @return @c true if @p b is a valid 2/3/4-byte lead and the decoder is now
 *         armed; @c false if @p b is a stray continuation or invalid lead.
 */
static bool utf8_try_start(u8 b)
{
  if((b & UTF8_2BYTE_LEAD_MASK) == UTF8_2BYTE_LEAD_VAL) {
    utf8_begin((u32)(b & UTF8_2BYTE_PAYLOAD_MASK), UTF8_2BYTE_TAIL);
    return true;
  }
  if((b & UTF8_3BYTE_LEAD_MASK) == UTF8_3BYTE_LEAD_VAL) {
    utf8_begin((u32)(b & UTF8_3BYTE_PAYLOAD_MASK), UTF8_3BYTE_TAIL);
    return true;
  }
  if((b & UTF8_4BYTE_LEAD_MASK) == UTF8_4BYTE_LEAD_VAL) {
    utf8_begin((u32)(b & UTF8_4BYTE_PAYLOAD_MASK), UTF8_4BYTE_TAIL);
    return true;
  }
  return false;
}

/**
 * @brief Stream one byte through the UTF-8 decoder; emit a codepoint when a
 *        sequence completes.
 *
 * Self-restarting on broken sequences (stray continuation, invalid lead):
 * emits a @c ? and replays the offending byte fresh. This keeps a corrupt
 * input stream from desyncing the parser forever, at the cost of one
 * placeholder glyph. ANSI/CSI sequences are stripped upstream by
 * @ref feed_byte so this only ever sees printable / control bytes.
 *
 * @param b  Input byte.
 */
static void feed_utf8(u8 b)
{
  if(fb_ctx.utf8_rem == 0) {
    if(b < ASCII_SPACE || b == ASCII_DEL) {
      handle_control(b);
      return;
    }
    if(b < UTF8_NON_ASCII_BASE) {
      emit_ascii(b);
      return;
    }
    if(!utf8_try_start(b))
      put_cp_at_cursor((u32)'?');
    return;
  }
  if((b & UTF8_CONT_LEAD_MASK) != UTF8_CONT_LEAD_VAL) {
    /* Broken sequence; recover by replaying this byte fresh. */
    fb_ctx.utf8_rem = 0;
    put_cp_at_cursor((u32)'?');
    feed_utf8(b);
    return;
  }
  fb_ctx.utf8_partial = (fb_ctx.utf8_partial << UTF8_CONT_PAYLOAD_BITS) |
                        (u32)(b & UTF8_CONT_PAYLOAD_MASK);
  fb_ctx.utf8_rem--;
  if(fb_ctx.utf8_rem == 0) {
    u32 cp = fb_ctx.utf8_partial;
    put_cp_at_cursor((cp <= UNICODE_MAX) ? cp : (u32)'?');
  }
}

/** @brief ASCII ESC byte, 0x1B — opens the ANSI ESC/CSI state machine. */
#define ASCII_ESC 0x1Bu

/**
 * @brief Handle one byte while the parser is in the @c ESC-was-just-seen state.
 *
 * Recognises the next byte's role (CSI introducer, DEC save/restore, charset
 * designator) and transitions or commits accordingly. Unrecognised bytes drop
 * back to normal so a stray escape (DECKPAM @c =, DECKPNM @c >, etc.) does not
 * poison subsequent output.
 *
 * @param b  Byte following ESC.
 */
static void feed_byte_esc(u8 b)
{
  if(b == '[') {
    fb_ctx.esc_state = 2;
    fb_ctx.esc_len   = 0;
    return;
  }
  if(b == '7') {
    fb_ctx.saved_cx  = fb_ctx.cx;
    fb_ctx.saved_cy  = fb_ctx.cy;
    fb_ctx.esc_state = 0;
    return;
  }
  if(b == '8') {
    fb_ctx.cx        = fb_ctx.saved_cx;
    fb_ctx.cy        = fb_ctx.saved_cy;
    fb_ctx.esc_state = 0;
    return;
  }
  if(b == '(' || b == ')') {
    fb_ctx.esc_state = 3;
    return;
  }
  fb_ctx.esc_state = 0;
}

/**
 * @brief Handle one byte while the parser is accumulating a CSI sequence.
 *
 * Parameter bytes (digits, @c ;, @c ?) append to @c esc_buf; anything else is
 * the final byte, which gets appended too and triggers @ref handle_csi. The
 * @c esc_buf overflow guard silently truncates — overlong sequences would
 * have to be hostile, not real terminal output.
 *
 * @param b  Byte received inside the CSI sequence.
 */
static void feed_byte_csi(u8 b)
{
  bool is_param = ((b >= '0' && b <= '9') || b == ';' || b == '?');
  if(fb_ctx.esc_len < (u8)(sizeof fb_ctx.esc_buf - 1))
    fb_ctx.esc_buf[fb_ctx.esc_len++] = (char)b;
  if(is_param)
    return;
  handle_csi();
  fb_ctx.esc_state = 0;
}

/**
 * @brief Handle one byte while the parser is waiting for a G0 charset
 *        designator after @c ESC@c (/@c ).
 *
 * Only @c 0 (DEC Special Graphics) and the @c B/A/U/1/2 family (US ASCII /
 * UK / line-drawing reset) are recognised; any other byte still exits the
 * state to avoid wedging the parser on an unknown designator.
 *
 * @param b  Designator byte.
 */
static void feed_byte_charset(u8 b)
{
  if(b == '0')
    fb_ctx.g0_acs = 1;
  else if(b == 'B' || b == 'A' || b == 'U' || b == '1' || b == '2')
    fb_ctx.g0_acs = 0;
  fb_ctx.esc_state = 0;
}

/**
 * @brief Top-level byte sink: drive the ESC/CSI state machine, fall through
 * to UTF-8 on plain bytes.
 *
 * Four-state machine — 0 normal, 1 saw @c ESC, 2 inside CSI, 3 inside
 * charset designator — implemented as a switch + per-state helper so each
 * branch fits in one screen.
 *
 * @param b  Input byte.
 */
void feed_byte(u8 b)
{
  switch(fb_ctx.esc_state) {
  case 1:
    feed_byte_esc(b);
    return;
  case 2:
    feed_byte_csi(b);
    return;
  case 3:
    feed_byte_charset(b);
    return;
  default:
    break;
  }
  if(b == ASCII_ESC) {
    fb_ctx.esc_state = 1;
    fb_ctx.utf8_rem  = 0;
    return;
  }
  feed_utf8(b);
}
