/**
 * @file src/drivers/ata/ata.c
 * @brief ATA/IDE disk driver (DMA + PIO fallback, LBA28/48).
 *
 * DMA via PCI Bus Master with per-channel bounce buffer.
 * Falls back to PIO when no scheduler context or DMA unsupported.
 */

#include <alcor2/ata.h>
#include <alcor2/console.h>
#include <alcor2/cpu.h>
#include <alcor2/errno.h>
#include <alcor2/io.h>
#include <alcor2/kstdlib.h>
#include <alcor2/pci.h>
#include <alcor2/pic.h>
#include <alcor2/pit.h>
#include <alcor2/pmm.h>
#include <alcor2/sched.h>
#include <alcor2/vmm.h>

#define TIMEOUT_TICKS 500 /* 5 s at 100 Hz */
#define LBA28_LIMIT   0x10000000ULL
#define MAX_RETRIES   3
#define POLL_ITERS    500000
#define PRD_EOT       0x8000

static ata_channel_t channels[2];
static ata_drive_t   drives[4];
static void         *bounce_virt[2]; /* DMA bounce buffer (virtual) */
static u64           bounce_phys[2]; /* DMA bounce buffer (physical) */

static inline u8     reg_read(ata_channel_t *ch, u8 reg)
{
  return inb(ch->base + reg);
}
static inline void reg_write(ata_channel_t *ch, u8 reg, u8 v)
{
  outb(ch->base + reg, v);
}
static inline u8 alt_status(ata_channel_t *ch)
{
  return inb(ch->ctrl);
}

/* ~400 ns delay (ATA spec after drive select / command issue). */
static inline void delay_400ns(ata_channel_t *ch)
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
static u8 poll_bsy(ata_channel_t *ch, u32 iters)
{
  u8 s = 0xFF;
  for(u32 i = 0; i < iters; i++) {
    s = reg_read(ch, ATA_REG_STATUS);
    if(!(s & ATA_SR_BSY))
      return s;
  }
  return s;
}

/**
 * @brief Poll until DRQ asserted (BSY clear).
 * @param ch    Channel to poll.
 * @param iters Maximum iterations.
 * @return true if DRQ set, false on error/timeout.
 */
static bool poll_drq(ata_channel_t *ch, u32 iters)
{
  for(u32 i = 0; i < iters; i++) {
    u8 s = reg_read(ch, ATA_REG_STATUS);
    if(s & (ATA_SR_ERR | ATA_SR_DF))
      return false;
    if(!(s & ATA_SR_BSY) && (s & ATA_SR_DRQ))
      return true;
  }
  return false;
}

/**
 * @brief Select a drive on its channel (master or slave).
 * @param d Drive to select.
 */
static void select_drive(ata_drive_t *d)
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
static bool channel_exists(ata_channel_t *ch)
{
  return reg_read(ch, ATA_REG_STATUS) != 0xFF;
}

/**
 * @brief Run IDENTIFY and populate drive descriptor.
 * @param d Drive descriptor to fill.
 */
static void identify(ata_drive_t *d)
{
  ata_channel_t *ch = d->channel;

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
  task_t *me = (task_t *)ch->waiter;

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
    sched_block();
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
  ch->waiter = sched_current();
}

/**
 * @brief Program task-file registers for LBA28.
 * @param d     Target drive.
 * @param lba   Starting LBA.
 * @param count Sector count (0 = 256).
 */
static void setup_lba28(ata_drive_t *d, u64 lba, u8 count)
{
  ata_channel_t *ch = d->channel;
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
  ata_channel_t *ch = d->channel;
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

  if(bytes > PAGE_SIZE)
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

/**
 * @brief Read sectors from an ATA drive (DMA if available, else PIO).
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

  if(d->dma && d->channel->dma_ok && sched_current() &&
     count <= (PAGE_SIZE / ATA_SECTOR_SIZE))
    return dma_transfer(d, lba, count, buf, false);

  return pio_read(d, lba, count, buf);
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

  if(d->dma && d->channel->dma_ok && sched_current() &&
     count <= (PAGE_SIZE / ATA_SECTOR_SIZE))
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

  if(ch->waiter)
    sched_unblock((task_t *)ch->waiter);
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
    void *bounce_page = pmm_alloc();
    if(!prdt_page || !bounce_page) {
      console_print("[ATA] Failed to allocate DMA buffers\n");
      if(prdt_page)
        pmm_free(prdt_page);
      if(bounce_page)
        pmm_free(bounce_page);
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
