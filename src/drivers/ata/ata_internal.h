/* Test-only: expose ATA cache and trim helpers for unit testing.
 * Included by ata.c (for prototypes) and by tests/unit/test_ata.c. */

#ifndef ALCOR2_DRIVERS_ATA_INTERNAL_H
#define ALCOR2_DRIVERS_ATA_INTERNAL_H

#include <alcor2/drivers/ata.h>
#include <alcor2/types.h>

#define CACHE_BLOCK_SECTORS 8u
#define CACHE_BLOCK_BYTES   ((u64)CACHE_BLOCK_SECTORS * 512u)
#define CACHE_NUM_ENTRIES   1024
#define CACHE_INVALID_LBA   ((u64) - 1)

typedef struct
{
  u64 block_lba;
  u64 last_used;
  u8  drive;
  u8  data[CACHE_BLOCK_BYTES] __attribute__((aligned(8)));
} ata_cache_entry_t;

extern ata_cache_entry_t g_ata_cache[CACHE_NUM_ENTRIES];
extern u64               g_cache_counter;
extern int               g_cache_inited;

void                     trim_string(char *s, size_t len);
void                     cache_init_once(void);
ata_cache_entry_t       *cache_lookup(u8 drive, u64 block_lba);
ata_cache_entry_t       *cache_alloc(void);
void                     cache_invalidate_range(u8 drive, u64 lba, u32 count);

#endif
