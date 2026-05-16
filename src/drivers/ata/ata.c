/**
 * @file src/drivers/ata/ata.c
 * @brief ATA/IDE disk driver (DMA + PIO fallback, LBA28/48).
 *
 * DMA via PCI Bus Master with per-channel bounce buffer.
 * The bounce buffer is 64 KB (16 contiguous pages) per channel, allowing
 * up to 128 sectors (64 KB) per single DMA command instead of 8 (4 KB).
 * Falls back to PIO when no current process (early boot), DMA unavailable,
 * or the transfer is too large for the bounce buffer.
 */

#include <alcor2/arch/cpu.h>
#include <alcor2/arch/io.h>
#include <alcor2/arch/pic.h>
#include <alcor2/arch/pit.h>
#include <alcor2/drivers/ata.h>
#include <alcor2/drivers/console.h>
#include <alcor2/drivers/pci.h>
#include <alcor2/errno.h>
#include <alcor2/kstdlib.h>
#include <alcor2/mm/pmm.h>
#include <alcor2/mm/vmm.h>
#include <alcor2/proc/proc.h>

#define TIMEOUT_TICKS    500 /* 5 s at 100 Hz */
#define LBA28_LIMIT      0x10000000ULL
#define MAX_RETRIES      3
#define POLL_ITERS       500000
#define PRD_EOT          0x8000
#define DMA_BOUNCE_PAGES 16                          /* 64 KB per channel  */
#define DMA_BOUNCE_BYTES (DMA_BOUNCE_PAGES * 0x1000) /* 65536 bytes        */
#define DMA_MAX_SECTORS  (DMA_BOUNCE_BYTES / 512)    /* 128 sectors        */

static ata_channel_t channels[2];
static ata_drive_t   drives[4];
static void         *bounce_virt[2]; /* DMA bounce buffer (virtual, 64 KB) */
static u64           bounce_phys[2]; /* DMA bounce buffer (physical)        */

static inline u8     reg_read(const ata_channel_t *ch, u8 reg)
{
  return inb(ch->base + reg);
}
static inline void reg_write(const ata_channel_t *ch, u8 reg, u8 v)
{
  outb(ch->base + reg, v);
}
static inline u8 alt_status(const ata_channel_t *ch)
{
  return inb(ch->ctrl);
}

/* ~400 ns delay (ATA spec after drive select / command issue). */
static inline void delay_400ns(const ata_channel_t *ch)
{
  alt_status(ch);
  alt_status(ch);
  alt_status(ch);
  alt_status(ch);
}

/**
 * @brief Poll until BSY clears.
 * @param ch    Channel to poll.
 * @param iters Maximum iterations.
 * @return Last status byte.
 */
static u8 poll_bsy(const ata_channel_t *ch, u32 iters)
{
  u8 s = 0xFF;
  for(u32 i = 0; i < iters; i++) {
    s = reg_read(ch, ATA_REG_STATUS);
    if(!(s & ATA_SR_BSY))
      return s;
    cpu_pause();
  }
  return s;
}

/**
 * @brief Poll until DRQ asserted (BSY clear).
 * @param ch    Channel to poll.
 * @param iters Maximum iterations.
 * @return true if DRQ set, false on error/timeout.
 */
static bool poll_drq(const ata_channel_t *ch, u32 iters)
{
  for(u32 i = 0; i < iters; i++) {
    u8 s = reg_read(ch, ATA_REG_STATUS);
    if(s & (ATA_SR_ERR | ATA_SR_DF))
      return false;
    if(!(s & ATA_SR_BSY) && (s & ATA_SR_DRQ))
      return true;
    cpu_pause();
  }
  return false;
}

/**
 * @brief Select a drive on its channel (master or slave).
 * @param d Drive to select.
 */
static void select_drive(const ata_drive_t *d)
{
  reg_write(d->channel, ATA_REG_HDDEVSEL, 0xA0 | (d->slave << 4));
  delay_400ns(d->channel);
}

/**
 * @brief Right-trim spaces and NULs from an ATA identify string.
 * @param s   String buffer (modified in place).
 * @param len Maximum length.
 */
static void trim_string(char *s, size_t len)
{
  char *end = s + len;
  while(end > s && (end[-1] == ' ' || end[-1] == '\0'))
    end--;
  *end = '\0';
}

/**
 * @brief Check whether a device is present (0xFF = floating bus).
 * @param ch Channel to probe.
 * @return true if device present.
 */
static bool channel_exists(const ata_channel_t *ch)
{
  return reg_read(ch, ATA_REG_STATUS) != 0xFF;
}

/**
 * @brief Run IDENTIFY and populate drive descriptor.
 * @param d Drive descriptor to fill.
 */
static void identify(ata_drive_t *d)
{
  const ata_channel_t *ch = d->channel;

  d->present = false;
  d->atapi   = false;
  d->lba48   = false;
  d->dma     = false;
  d->sectors = 0;

  if(!channel_exists(ch))
    return;

  select_drive(d);

  reg_write(ch, ATA_REG_SECCOUNT, 0);
  reg_write(ch, ATA_REG_LBA0, 0);
  reg_write(ch, ATA_REG_LBA1, 0);
  reg_write(ch, ATA_REG_LBA2, 0);
  reg_write(ch, ATA_REG_COMMAND, ATA_CMD_IDENTIFY);
  delay_400ns(ch);

  u8 status = reg_read(ch, ATA_REG_STATUS);
  if(status == 0)
    return;

  status = poll_bsy(ch, 100000);
  if(status & ATA_SR_BSY)
    return;

  /* ATAPI signature check */
  u8 mid = reg_read(ch, ATA_REG_LBA1);
  u8 hi  = reg_read(ch, ATA_REG_LBA2);
  if((mid == 0x14 && hi == 0xEB) || (mid == 0x69 && hi == 0x96)) {
    d->present = true;
    d->atapi   = true;
    return;
  }

  if(!poll_drq(ch, 100000))
    return;

  u16 id[256];
  for(int i = 0; i < 256; i++)
    id[i] = inw(ch->base);

  d->present = true;
  d->lba48   = !!(id[83] & (1 << 10));
  d->dma     = !!(id[49] & (1 << 8));

  if(d->lba48) {
    d->sectors = (u64)id[100] | ((u64)id[101] << 16) | ((u64)id[102] << 32) |
                 ((u64)id[103] << 48);
  }
  if(d->sectors == 0)
    d->sectors = (u32)id[60] | ((u32)id[61] << 16);

  /* Model (words 27-46) and serial (words 10-19), byte-swapped */
  for(int i = 0; i < 20; i++) {
    d->model[(size_t)i * 2]     = (char)(id[27 + i] >> 8);
    d->model[(size_t)i * 2 + 1] = (char)(id[27 + i] & 0xFF);
  }
  d->model[40] = '\0';
  trim_string(d->model, 40);

  for(int i = 0; i < 10; i++) {
    d->serial[(size_t)i * 2]     = (char)(id[10 + i] >> 8);
    d->serial[(size_t)i * 2 + 1] = (char)(id[10 + i] & 0xFF);
  }
  d->serial[20] = '\0';
  trim_string(d->serial, 20);
}

/**
 * @brief Wait for ATA command completion.
 *
 * With scheduler: block + wake from IRQ. Early boot: busy-poll.
 *
 * @param ch Channel with a pending command.
 * @return 0 on success, -EIO on error, -ETIMEDOUT on timeout.
 */
static i64 wait_irq(ata_channel_t *ch)
{
  proc_t *me = ch->waiter;

  if(!me) {
    cpu_enable_interrupts();
    for(int i = 0; i < POLL_ITERS; i++) {
      u8 s = alt_status(ch);
      if(s & (ATA_SR_ERR | ATA_SR_DF))
        return -EIO;
      if(!(s & ATA_SR_BSY))
        return 0;
    }
    return -ETIMEDOUT;
  }

  u64 deadline = pit_get_ticks() + TIMEOUT_TICKS;
  while(ch->state == ATA_STATE_PENDING) {
    if(pit_get_ticks() >= deadline) {
      if(ch->dma_ok)
        outb(ch->bmi + BMI_CMD, 0);
      ch->state  = ATA_STATE_IDLE;
      ch->waiter = NULL;
      cpu_enable_interrupts();
      return -ETIMEDOUT;
    }
    me->state = PROC_STATE_BLOCKED;
    proc_schedule();
    cpu_disable_interrupts();
  }

  ch->waiter = NULL;
  cpu_enable_interrupts();
  return (ch->status & (ATA_SR_ERR | ATA_SR_DF)) ? -EIO : 0;
}

/**
 * @brief Prepare a channel for IRQ-driven command completion.
 * @param ch Channel to prepare.
 */
static void prepare_irq_wait(ata_channel_t *ch)
{
  cpu_disable_interrupts();
  ch->state  = ATA_STATE_PENDING;
  ch->waiter = proc_current();
}

/**
 * @brief Program task-file registers for LBA28.
 * @param d     Target drive.
 * @param lba   Starting LBA.
 * @param count Sector count (0 = 256).
 */
static void setup_lba28(ata_drive_t *d, u64 lba, u8 count)
{
  const ata_channel_t *ch = d->channel;
  reg_write(
      ch, ATA_REG_HDDEVSEL, 0xE0 | (d->slave << 4) | ((lba >> 24) & 0x0F)
  );
  delay_400ns(ch);
  reg_write(ch, ATA_REG_SECCOUNT, count);
  reg_write(ch, ATA_REG_LBA0, (u8)(lba));
  reg_write(ch, ATA_REG_LBA1, (u8)(lba >> 8));
  reg_write(ch, ATA_REG_LBA2, (u8)(lba >> 16));
}

/**
 * @brief Program task-file registers for LBA48 (HOB first).
 * @param d     Target drive.
 * @param lba   Starting LBA.
 * @param count Sector count (0 = 65536).
 */
static void setup_lba48(ata_drive_t *d, u64 lba, u16 count)
{
  const ata_channel_t *ch = d->channel;
  reg_write(ch, ATA_REG_HDDEVSEL, 0x40 | (d->slave << 4));
  delay_400ns(ch);
  reg_write(ch, ATA_REG_SECCOUNT, (u8)(count >> 8));
  reg_write(ch, ATA_REG_LBA0, (u8)(lba >> 24));
  reg_write(ch, ATA_REG_LBA1, (u8)(lba >> 32));
  reg_write(ch, ATA_REG_LBA2, (u8)(lba >> 40));
  reg_write(ch, ATA_REG_SECCOUNT, (u8)(count));
  reg_write(ch, ATA_REG_LBA0, (u8)(lba));
  reg_write(ch, ATA_REG_LBA1, (u8)(lba >> 8));
  reg_write(ch, ATA_REG_LBA2, (u8)(lba >> 16));
}

/**
 * @brief Load PRDT with a single bounce-buffer entry.
 * @param ch    Channel whose PRDT to program.
 * @param phys  Physical address of data buffer.
 * @param bytes Transfer size in bytes.
 */
static void setup_prdt(ata_channel_t *ch, u64 phys, u32 bytes)
{
  ch->prdt[0].phys_addr  = (u32)phys;
  ch->prdt[0].byte_count = (u16)(bytes & 0xFFFF);
  ch->prdt[0].flags      = PRD_EOT;
  outl(ch->bmi + BMI_PRDT, (u32)ch->prdt_phys);
}

/**
 * @brief DMA read/write through bounce buffer with retries.
 * @param d     Target drive.
 * @param lba   Starting sector.
 * @param count Sector count (must fit in one page).
 * @param buf   Caller's buffer.
 * @param write true = write, false = read.
 * @return 0 on success, negative errno on failure.
 */
static i64
    dma_transfer(ata_drive_t *d, u64 lba, u32 count, void *buf, bool write)
{
  ata_channel_t *ch    = d->channel;
  int            cidx  = (ch == &channels[0]) ? 0 : 1;
  bool           ext   = d->lba48 && (lba + count) >= LBA28_LIMIT;
  u32            bytes = count * ATA_SECTOR_SIZE;

  if(bytes > DMA_BOUNCE_BYTES)
    return -EINVAL;

  void *bounce = bounce_virt[cidx];
  u64   bphys  = bounce_phys[cidx];
  if(!bounce)
    return -ENOMEM;

  if(write)
    kmemcpy(bounce, buf, bytes);

  for(int retry = 0; retry < MAX_RETRIES; retry++) {
    outb(ch->bmi + BMI_CMD, 0);
    outb(ch->bmi + BMI_STATUS, BMI_STATUS_IRQ | BMI_STATUS_ERR);

    setup_prdt(ch, bphys, bytes);
    prepare_irq_wait(ch);

    if(ext) {
      setup_lba48(d, lba, (u16)count);
      reg_write(
          ch, ATA_REG_COMMAND,
          write ? ATA_CMD_WRITE_DMA_EXT : ATA_CMD_READ_DMA_EXT
      );
    } else {
      setup_lba28(d, lba, (u8)count);
      reg_write(
          ch, ATA_REG_COMMAND, write ? ATA_CMD_WRITE_DMA : ATA_CMD_READ_DMA
      );
    }

    outb(ch->bmi + BMI_CMD, BMI_CMD_START | (write ? 0 : BMI_CMD_READ));
    i64 r = wait_irq(ch);
    outb(ch->bmi + BMI_CMD, 0);

    if(r == 0 && !(ch->bmi_status & BMI_STATUS_ERR)) {
      if(!write)
        kmemcpy(buf, bounce, bytes);
      return 0;
    }
  }

  return -EIO;
}

/**
 * @brief Read sectors using PIO.
 * @param d     Target drive.
 * @param lba   Starting sector.
 * @param count Number of sectors.
 * @param buf   Output buffer.
 * @return 0 on success, negative errno on failure.
 */
static i64 pio_read(ata_drive_t *d, u64 lba, u32 count, void *buf)
{
  ata_channel_t *ch  = d->channel;
  u16           *out = (u16 *)buf;

  for(u32 s = 0; s < count; s++) {
    u64  cur = lba + s;
    bool ext = d->lba48 && cur >= LBA28_LIMIT;
    i64  r   = -EIO;

    for(int retry = 0; retry < MAX_RETRIES && r < 0; retry++) {
      prepare_irq_wait(ch);
      if(ext) {
        setup_lba48(d, cur, 1);
        reg_write(ch, ATA_REG_COMMAND, ATA_CMD_READ_PIO_EXT);
      } else {
        setup_lba28(d, cur, 1);
        reg_write(ch, ATA_REG_COMMAND, ATA_CMD_READ_PIO);
      }
      r = wait_irq(ch);
    }

    if(r < 0)
      return r;

    for(int i = 0; i < 256; i++)
      out[s * 256 + i] = inw(ch->base);
  }

  return 0;
}

/**
 * @brief Write sectors using PIO with cache flush.
 * @param d     Target drive.
 * @param lba   Starting sector.
 * @param count Number of sectors.
 * @param buf   Input buffer.
 * @return 0 on success, negative errno on failure.
 */
static i64 pio_write(ata_drive_t *d, u64 lba, u32 count, const void *buf)
{
  ata_channel_t *ch  = d->channel;
  const u16     *src = (const u16 *)buf;

  for(u32 s = 0; s < count; s++) {
    u64  cur = lba + s;
    bool ext = d->lba48 && cur >= LBA28_LIMIT;
    i64  r   = -EIO;

    for(int retry = 0; retry < MAX_RETRIES && r < 0; retry++) {
      if(ext) {
        setup_lba48(d, cur, 1);
        reg_write(ch, ATA_REG_COMMAND, ATA_CMD_WRITE_PIO_EXT);
      } else {
        setup_lba28(d, cur, 1);
        reg_write(ch, ATA_REG_COMMAND, ATA_CMD_WRITE_PIO);
      }

      delay_400ns(ch);
      delay_400ns(ch);

      if(!(reg_read(ch, ATA_REG_STATUS) & ATA_SR_DRQ)) {
        r = -EIO;
        continue;
      }

      for(int i = 0; i < 256; i++)
        outw(ch->base, src[s * 256 + i]);

      prepare_irq_wait(ch);
      reg_write(
          ch, ATA_REG_COMMAND,
          ext ? ATA_CMD_CACHE_FLUSH_EXT : ATA_CMD_CACHE_FLUSH
      );
      r = wait_irq(ch);
    }

    if(r < 0)
      return r;
  }

  return 0;
}

/*
 * Block cache (read-side, write-through-with-invalidate).
 *
 * 4 KB blocks (8 sectors), 1024 entries = 4 MB total. LRU eviction by
 * monotonic counter. Hits (the common case after warm-up) skip DMA/PIO
 * entirely — clang's repeated ELF page reads now cost a memcpy. Misses
 * fetch a full 4 KB block so adjacent reads land hot.
 */

#define CACHE_BLOCK_SECTORS 8u
#define CACHE_BLOCK_BYTES   ((u64)CACHE_BLOCK_SECTORS * 512u)
#define CACHE_NUM_ENTRIES   1024
#define CACHE_INVALID_LBA   ((u64) - 1)

// cppcheck-suppress unusedStructMember
typedef struct
{
  u64 block_lba; /* aligned, CACHE_INVALID_LBA = free slot */
  u64 last_used;
  u8  drive;
  // cppcheck-suppress unusedStructMember
  u8 pad[7];
  u8 data[CACHE_BLOCK_BYTES];
} ata_cache_entry_t;

static ata_cache_entry_t g_ata_cache[CACHE_NUM_ENTRIES];
static u64               g_cache_counter = 0;
static int               g_cache_inited  = 0;

static void              cache_init_once(void)
{
  if(g_cache_inited)
    return;
  for(int i = 0; i < CACHE_NUM_ENTRIES; i++)
    g_ata_cache[i].block_lba = CACHE_INVALID_LBA;
  g_cache_inited = 1;
}

static ata_cache_entry_t *cache_lookup(u8 drive, u64 block_lba)
{
  for(int i = 0; i < CACHE_NUM_ENTRIES; i++) {
    if(g_ata_cache[i].block_lba == block_lba && g_ata_cache[i].drive == drive) {
      g_ata_cache[i].last_used = ++g_cache_counter;
      return &g_ata_cache[i];
    }
  }
  return NULL;
}

static ata_cache_entry_t *cache_alloc(void)
{
  /* Prefer free slot; else evict LRU. */
  int idx    = 0;
  u64 oldest = (u64)-1;
  for(int i = 0; i < CACHE_NUM_ENTRIES; i++) {
    if(g_ata_cache[i].block_lba == CACHE_INVALID_LBA)
      return &g_ata_cache[i];
    if(g_ata_cache[i].last_used < oldest) {
      oldest = g_ata_cache[i].last_used;
      idx    = i;
    }
  }
  return &g_ata_cache[idx];
}

static void cache_invalidate_range(u8 drive, u64 lba, u32 count)
{
  u64 end = lba + count;
  for(int i = 0; i < CACHE_NUM_ENTRIES; i++) {
    if(g_ata_cache[i].block_lba == CACHE_INVALID_LBA)
      continue;
    if(g_ata_cache[i].drive != drive)
      continue;
    u64 b_start = g_ata_cache[i].block_lba;
    u64 b_end   = b_start + CACHE_BLOCK_SECTORS;
    if(b_start < end && b_end > lba)
      g_ata_cache[i].block_lba = CACHE_INVALID_LBA;
  }
}

static i64 ata_read_raw(ata_drive_t *d, u64 lba, u32 count, void *buf)
{
  if(d->dma && d->channel->dma_ok && proc_current() &&
     count <= DMA_MAX_SECTORS)
    return dma_transfer(d, lba, count, buf, false);
  return pio_read(d, lba, count, buf);
}

/**
 * @brief Read sectors from an ATA drive (cache + DMA/PIO fallback).
 * @param drive Drive index (0-3).
 * @param lba   Starting sector.
 * @param count Number of sectors.
 * @param buf   Output buffer.
 * @return 0 on success, negative errno on failure.
 */
i64 ata_read(u8 drive, u64 lba, u32 count, void *buf)
{
  if(drive >= 4 || !buf || count == 0)
    return -EINVAL;

  ata_drive_t *d = &drives[drive];
  if(!d->present || d->atapi)
    return -ENODEV;
  if(lba + count > d->sectors)
    return -EINVAL;

  cache_init_once();

  u64 cur = lba;
  u64 end = lba + count;
  u8 *out = (u8 *)buf;

  while(cur < end) {
    u64 block_lba     = cur & ~(u64)(CACHE_BLOCK_SECTORS - 1);
    u64 in_block      = cur - block_lba;
    u64 left_in_block = CACHE_BLOCK_SECTORS - in_block;
    u64 left_total    = end - cur;
    u64 take          = left_in_block < left_total ? left_in_block : left_total;

    ata_cache_entry_t *e = cache_lookup(drive, block_lba);
    if(!e) {
      e              = cache_alloc();
      u64 block_size = CACHE_BLOCK_SECTORS;
      if(block_lba + block_size > d->sectors)
        block_size = d->sectors - block_lba;

      i64 r = ata_read_raw(d, block_lba, (u32)block_size, e->data);
      if(r < 0) {
        e->block_lba = CACHE_INVALID_LBA;
        return r;
      }
      /* Zero unread tail (partial block at disk end). */
      for(u64 i = block_size * 512; i < CACHE_BLOCK_BYTES; i++)
        e->data[i] = 0;

      e->block_lba = block_lba;
      e->drive     = drive;
      e->last_used = ++g_cache_counter;
    }

    kmemcpy(out, &e->data[in_block * 512], take * 512);
    out += take * 512;
    cur += take;
  }

  return 0;
}

/**
 * @brief Write sectors to an ATA drive (DMA if available, else PIO).
 * @param drive Drive index (0-3).
 * @param lba   Starting sector.
 * @param count Number of sectors.
 * @param buf   Input buffer.
 * @return 0 on success, negative errno on failure.
 */
i64 ata_write(u8 drive, u64 lba, u32 count, const void *buf)
{
  if(drive >= 4 || !buf || count == 0)
    return -EINVAL;

  ata_drive_t *d = &drives[drive];
  if(!d->present || d->atapi)
    return -ENODEV;
  if(lba + count > d->sectors)
    return -EINVAL;

  /* Write-through: invalidate any cached blocks overlapping this range so
   * the next read sees fresh data. Simpler and safer than mutating cache
   * entries in place (handles unaligned writes too). */
  cache_init_once();
  cache_invalidate_range(drive, lba, count);

  if(d->dma && d->channel->dma_ok && proc_current() &&
     count <= DMA_MAX_SECTORS)
    return dma_transfer(d, lba, count, (void *)buf, true);

  return pio_write(d, lba, count, buf);
}

/**
 * @brief ATA IRQ handler — reading status clears the device's IRQ line.
 * @param channel Channel index (0 = primary, 1 = secondary).
 */
void ata_irq(u8 channel)
{
  if(channel >= 2)
    return;

  ata_channel_t *ch = &channels[channel];

  ch->status = reg_read(ch, ATA_REG_STATUS);
  ch->error  = (ch->status & ATA_SR_ERR) ? reg_read(ch, ATA_REG_ERROR) : 0;

  if(ch->dma_ok) {
    ch->bmi_status = inb(ch->bmi + BMI_STATUS);
    outb(ch->bmi + BMI_STATUS, BMI_STATUS_IRQ | BMI_STATUS_ERR);
  }

  ch->state = ATA_STATE_IDLE;

  if(ch->waiter) {
    if(ch->waiter->state == PROC_STATE_BLOCKED)
      ch->waiter->state = PROC_STATE_READY;
    ch->waiter = NULL;
  }
}

/** @brief Detect and configure PCI IDE Bus Master for DMA. */
static void init_dma(void)
{
  pci_device_t ide;
  if(!pci_find_device(PCI_CLASS_STORAGE, PCI_SUBCLASS_IDE, &ide)) {
    console_print("[ATA] No IDE controller found, DMA disabled\n");
    return;
  }

  pci_enable_bus_master(&ide);

  u16 bar4 = ide.bar[4] & 0xFFFC;
  if(bar4 == 0) {
    console_print("[ATA] BAR4 invalid, DMA disabled\n");
    return;
  }

  for(int i = 0; i < 2; i++) {
    channels[i].bmi = bar4 + (i * 8);

    void *prdt_page   = pmm_alloc();
    void *bounce_page = pmm_alloc_pages(DMA_BOUNCE_PAGES);
    if(!prdt_page || !bounce_page) {
      console_print("[ATA] Failed to allocate DMA buffers\n");
      if(prdt_page)
        pmm_free(prdt_page);
      if(bounce_page)
        pmm_free_pages(bounce_page, DMA_BOUNCE_PAGES);
      continue;
    }

    channels[i].prdt_phys = (u64)prdt_page;
    channels[i].prdt      = phys_to_virt((u64)prdt_page);
    channels[i].dma_ok    = true;

    bounce_phys[i] = (u64)bounce_page;
    bounce_virt[i] = phys_to_virt((u64)bounce_page);
  }

  console_print("[ATA] DMA enabled\n");
}

/** @brief Initialize the ATA subsystem (channels, drives, IRQs, DMA). */
void ata_init(void)
{
  channels[0] = (ata_channel_t
  ) {.base  = ATA_PRIMARY_DATA,
     .ctrl  = ATA_PRIMARY_CTRL,
     .irq   = IRQ_ATA_PRIMARY,
     .state = ATA_STATE_IDLE};
  channels[1] = (ata_channel_t
  ) {.base  = ATA_SECONDARY_DATA,
     .ctrl  = ATA_SECONDARY_CTRL,
     .irq   = IRQ_ATA_SECONDARY,
     .state = ATA_STATE_IDLE};

  for(int i = 0; i < 4; i++) {
    drives[i].channel = &channels[i / 2];
    drives[i].slave   = i % 2;
  }

  for(int i = 0; i < 4; i++) {
    identify(&drives[i]);
    if(drives[i].present && !drives[i].atapi) {
      u32 mb = (u32)(drives[i].sectors / 2048);
      console_printf(
          "[ATA] Drive %d: %s (%d MB, %s%s)\n", i, drives[i].model, mb,
          drives[i].lba48 ? "LBA48" : "LBA28", drives[i].dma ? ", DMA" : ""
      );
    }
  }

  /* Enable device interrupts (nIEN = 0) and drain stale IRQs from IDENTIFY */
  outb(ATA_PRIMARY_CTRL, 0x00);
  outb(ATA_SECONDARY_CTRL, 0x00);
  (void)reg_read(&channels[0], ATA_REG_STATUS);
  (void)reg_read(&channels[1], ATA_REG_STATUS);

  pic_unmask(IRQ_ATA_PRIMARY);
  pic_unmask(IRQ_ATA_SECONDARY);

  init_dma();
  console_print("[ATA] Ready\n");
}

/**
 * @brief Get drive descriptor by index.
 * @param idx Drive index (0-3).
 * @return Drive pointer, or NULL if not present.
 */
ata_drive_t *ata_get_drive(u8 idx)
{
  if(idx >= 4 || !drives[idx].present)
    return NULL;
  return &drives[idx];
}
