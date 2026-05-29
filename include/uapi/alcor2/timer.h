/**
 * @file include/uapi/alcor2/timer.h
 * @brief UAPI for the PIT-driven system timer.
 *
 * Userland-visible bits: tick-rate constants the kernel exposes, plus the
 * @c ALCOR2_IOC_TIMER_FAST ioctl latency-sensitive apps (Doom) use to
 * temporarily raise the tick rate. Kernel-private driver decls
 * (@c pit_init, @c pit_set_frequency, …) stay in @c <alcor2/arch/pit.h>.
 */

#ifndef ALCOR2_UAPI_TIMER_H
#define ALCOR2_UAPI_TIMER_H

/** @brief Default tick rate in Hz (idle/system rate). */
#define PIT_TICK_HZ 250u

/** @brief Elevated tick rate for latency-sensitive apps. */
#define PIT_TICK_HZ_FAST 1000u

/**
 * @brief ioctl(0, ALCOR2_IOC_TIMER_FAST, &uint32_t on)
 *
 * @c on @c != @c 0 votes for the elevated rate; @c on @c == @c 0 releases
 * the vote. Refcounted and auto-released on process exit. Mirrors musl
 * @c _IOW('T', @c 1, @c uint32_t).
 */
#define ALCOR2_IOC_TIMER_FAST ((1U << 30) | (0x54U << 8) | 1U | (4U << 16))

#endif /* ALCOR2_UAPI_TIMER_H */
