/**
 * @file include/alcor2/drivers/klog.h
 * @brief Debugcon-only kernel log channel.
 *
 * Drops every byte on QEMU's @c -debugcon backend (port 0xE9). Real
 * hardware ignores the writes. Use this for high-volume / post-boot
 * debug taps (syscall traces, etc.) that must not touch the framebuffer
 * — the boot logger (::console_print) is the wrong channel for those
 * because it would draw over the fb_console terminal owned by user
 * processes.
 *
 * Supports a small printf subset: %s, %c, %d, %u, %x, %lx, %lu, %%.
 * No floats. No width / precision specifiers.
 */

#ifndef ALCOR2_DRIVERS_KLOG_H
#define ALCOR2_DRIVERS_KLOG_H

/**
 * @brief Format a message to the kernel debug log (debugcon).
 *
 * @param fmt  printf-style format string.
 */
void klogf(const char *fmt, ...);

#endif
