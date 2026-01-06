/**
 * @file include/alcor2/gdt.h
 * @brief Global Descriptor Table and Task State Segment setup.
 *
 * Defines segment selectors for kernel and userspace code/data,
 * plus the TSS for privilege-level transitions.
 */

#ifndef ALCOR2_GDT_H
#define ALCOR2_GDT_H

#include <alcor2/types.h>

/** @brief GDT selector for ring-0 code. */
#define GDT_KERNEL_CODE 0x28

/** @brief GDT selector for ring-0 data. */
#define GDT_KERNEL_DATA 0x30

/** @brief GDT selector for ring-3 data. */
#define GDT_USER_DATA 0x3B

/** @brief GDT selector for ring-3 code. */
#define GDT_USER_CODE 0x43

/** @brief GDT selector for the Task State Segment. */
#define GDT_TSS 0x48

/**
 * @brief 8-byte GDT entry for code/data segments.
 */
typedef struct PACKED
{
  u16 limit_low;
  u16 base_low;
  u8  base_mid;
  u8  access;
  u8  flags_limit;
  u8  base_high;
} gdt_entry_t;

/**
 * @brief 16-byte GDT entry for the TSS (x86_64).
 */
typedef struct PACKED
{
  u16 limit_low;
  u16 base_low;
  u8  base_mid;
  u8  access;
  u8  flags_limit;
  u8  base_high;
  u32 base_upper;
  u32 reserved;
} gdt_tss_entry_t;

/**
 * @brief GDT pointer structure (for LGDT instruction).
 */
typedef struct PACKED
{
  u16 limit; /**< Size of GDT minus one. */
  u64 base;  /**< Linear address of the GDT. */
} gdt_ptr_t;

/**
 * @brief Task State Segment (x86_64 format).
 */
typedef struct PACKED
{
  u32 reserved0;
  u64 rsp0; /**< Stack pointer for ring 0. */
  u64 rsp1;
  u64 rsp2;
  u64 reserved1;
  u64 ist1; /**< Interrupt Stack Table 1. */
  u64 ist2;
  u64 ist3;
  u64 ist4;
  u64 ist5;
  u64 ist6;
  u64 ist7;
  u64 reserved2;
  u16 reserved3;
  u16 iopb; /**< I/O permission bitmap offset. */
} tss_t;

/**
 * @brief Initialize the GDT and load the TSS.
 */
void gdt_init(void);

/**
 * @brief Update the TSS ring-0 stack pointer.
 * @param rsp0 New kernel stack pointer.
 */
void tss_set_rsp0(u64 rsp0);

#endif
