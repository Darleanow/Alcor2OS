/**
 * @file src/drivers/ata/ata.c
 * @brief ATA/IDE PIO-mode driver implementation.
 */

#include <alcor2/ata.h>
#include <alcor2/console.h>
#include <alcor2/io.h>
#include <alcor2/errno.h>

/** @brief Drive state for up to 4 ATA devices. */
static ata_drive_t drives[4];

/**
 * @brief Short delay for ATA operations (~400ns).
 * @param port Status port to read.
 */
static void ata_delay(u16 port)
{
  inb(port);
  inb(port);
  inb(port);
  inb(port);
}

/**
 * @brief Wait for BSY flag to clear.
 * @param port Status port.
 */
static void ata_wait_bsy(u16 port)
{
  while(inb(port) & ATA_SR_BSY)
    ;
}

/**
 * @brief Wait for DRQ flag to set.
 * @param port Status port.
 */
static void __attribute__((unused)) ata_wait_drq(u16 port)
{
  while(!(inb(port) & ATA_SR_DRQ))
    ;
}

/**
 * @brief Wait for drive to be ready for data transfer.
 * @param status_port Status port.
 * @return true if ready, false on error/timeout.
 */
static bool ata_wait_ready(u16 status_port)
{
  u32 timeout = 100000;
  while(timeout--) {
    u8 status = inb(status_port);
    if(status & ATA_SR_ERR)
      return false;
    if(status & ATA_SR_DF)
      return false;
    if(!(status & ATA_SR_BSY) && (status & ATA_SR_DRQ))
      return true;
  }
  return false;
}

/**
 * @brief Trim trailing spaces from ATA string.
 * @param s String buffer (40 chars).
 */
static void str_trim(char *s)
{
  char *end = s + 40;
  while(end > s && (*(end - 1) == ' ' || *(end - 1) == '\0')) {
    end--;
  }
  *end = '\0';
}

/**
 * @brief Identify an ATA drive and read its geometry.
 * @param drv Drive structure to populate.
 */
static void ata_identify(ata_drive_t *drv)
{
  u16 base = drv->base;
  u16 ctrl = drv->ctrl;

  /* Select drive */
  outb(base + 6, drv->drive ? ATA_DRIVE_SLAVE : ATA_DRIVE_MASTER);
  ata_delay(ctrl);

  /* Clear sector count and LBA registers */
  outb(base + 2, 0);
  outb(base + 3, 0);
  outb(base + 4, 0);
  outb(base + 5, 0);

  /* Send IDENTIFY command */
  outb(base + 7, ATA_CMD_IDENTIFY);
  ata_delay(ctrl);

  /* Check if drive exists */
  u8 status = inb(base + 7);
  if(status == 0) {
    drv->present = false;
    return;
  }

  /* Wait for BSY to clear */
  ata_wait_bsy(base + 7);

  /* Check for ATAPI */
  u8 lba_mid = inb(base + 4);
  u8 lba_hi  = inb(base + 5);

  if(lba_mid == 0x14 && lba_hi == 0xEB) {
    drv->is_atapi = true;
    drv->present  = true;
    return; /* ATAPI not fully supported yet */
  }

  if(lba_mid == 0x3C && lba_hi == 0xC3) {
    drv->is_atapi = true;
    drv->present  = true;
    return;
  }

  /* Wait for DRQ or error */
  if(!ata_wait_ready(base + 7)) {
    drv->present = false;
    return;
  }

  /* Read identify data */
  u16 identify[256];
  for(int i = 0; i < 256; i++) {
    identify[i] = inw(base);
  }

  drv->present  = true;
  drv->is_atapi = false;

  /* Parse identify data */
  /* Words 60-61: Total addressable sectors (LBA28) */
  u32 lba28_sectors = identify[60] | ((u32)identify[61] << 16);

  /* Words 100-103: Total addressable sectors (LBA48) */
  u64 lba48_sectors = identify[100] | ((u64)identify[101] << 16) |
                      ((u64)identify[102] << 32) | ((u64)identify[103] << 48);

  drv->sectors = lba48_sectors ? lba48_sectors : lba28_sectors;

  /* Extract model string (words 27-46, byte-swapped) */
  for(int i = 0; i < 20; i++) {
    drv->model[i * 2]     = (identify[27 + i] >> 8) & 0xFF;
    drv->model[i * 2 + 1] = identify[27 + i] & 0xFF;
  }
  drv->model[40] = '\0';
  str_trim(drv->model);

  /* Extract serial (words 10-19, byte-swapped) */
  for(int i = 0; i < 10; i++) {
    drv->serial[i * 2]     = (identify[10 + i] >> 8) & 0xFF;
    drv->serial[i * 2 + 1] = identify[10 + i] & 0xFF;
  }
  drv->serial[20] = '\0';
  str_trim(drv->serial);
}

/**
 * @brief Initialize ATA driver and detect all drives
 *
 * Sets up drive structures for primary and secondary channels (master/slave).
 * Identifies each drive and prints detected drives with model and size.
 */
void ata_init(void)
{
  /* Initialize drive structures */
  drives[0].base  = ATA_PRIMARY_DATA;
  drives[0].ctrl  = ATA_PRIMARY_CTRL;
  drives[0].drive = 0;

  drives[1].base  = ATA_PRIMARY_DATA;
  drives[1].ctrl  = ATA_PRIMARY_CTRL;
  drives[1].drive = 1;

  drives[2].base  = ATA_SECONDARY_DATA;
  drives[2].ctrl  = ATA_SECONDARY_CTRL;
  drives[2].drive = 0;

  drives[3].base  = ATA_SECONDARY_DATA;
  drives[3].ctrl  = ATA_SECONDARY_CTRL;
  drives[3].drive = 1;

  /* Detect drives */
  for(int i = 0; i < 4; i++) {
    ata_identify(&drives[i]);

    if(drives[i].present && !drives[i].is_atapi) {
      console_printf(
          "[ATA] Drive %d: %s (%d MB)\n", i, drives[i].model,
          (int)(drives[i].sectors / 2048)
      );
    }
  }

  console_print("[ATA] Initialized\n");
}

/**
 * @brief Get drive structure by index
 * @param drive Drive index (0-3)
 * @return Pointer to drive structure, or NULL if invalid
 */
ata_drive_t *ata_get_drive(u8 drive)
{
  if(drive >= 4)
    return NULL;
  return &drives[drive];
}

/**
 * @brief Read sectors from ATA drive
 * @param drive_idx Drive index (0-3)
 * @param lba Starting logical block address
 * @param count Number of sectors to read
 * @param buffer Buffer to store read data (must hold count * 512 bytes)
 * @return 0 on success, -1 on error
 */
i64 ata_read(u8 drive_idx, u64 lba, u32 count, void *buffer)
{
  if(drive_idx >= 4)
    return -EINVAL;

  ata_drive_t *drv = &drives[drive_idx];
  if(!drv->present || drv->is_atapi)
    return -ENODEV;

  u16  base = drv->base;
  u16 *buf  = (u16 *)buffer;

  for(u32 sector = 0; sector < count; sector++) {
    u64 current_lba = lba + sector;

    /* Wait for drive to be ready */
    ata_wait_bsy(base + 7);

    /* Select drive with LBA mode */
    outb(base + 6, 0xE0 | (drv->drive << 4) | ((current_lba >> 24) & 0x0F));

    /* Send sector count and LBA */
    outb(base + 2, 1); /* Read 1 sector */
    outb(base + 3, (u8)(current_lba));
    outb(base + 4, (u8)(current_lba >> 8));
    outb(base + 5, (u8)(current_lba >> 16));

    /* Send read command */
    outb(base + 7, ATA_CMD_READ_PIO);

    /* Wait for data */
    if(!ata_wait_ready(base + 7)) {
      return -EIO;
    }

    /* Read data (256 words = 512 bytes) */
    for(int i = 0; i < 256; i++) {
      buf[sector * 256 + i] = inw(base);
    }
  }

  return 0;
}

/**
 * @brief Write sectors to ATA drive
 * @param drive_idx Drive index (0-3)
 * @param lba Starting logical block address
 * @param count Number of sectors to write
 * @param buffer Buffer containing data to write (must hold count * 512 bytes)
 * @return 0 on success, -1 on error
 */
i64 ata_write(u8 drive_idx, u64 lba, u32 count, const void *buffer)
{
  if(drive_idx >= 4)
    return -EINVAL;

  ata_drive_t *drv = &drives[drive_idx];
  if(!drv->present || drv->is_atapi)
    return -ENODEV;

  u16        base = drv->base;
  const u16 *buf  = (const u16 *)buffer;

  for(u32 sector = 0; sector < count; sector++) {
    u64 current_lba = lba + sector;

    /* Wait for drive to be ready */
    ata_wait_bsy(base + 7);

    /* Select drive with LBA mode */
    outb(base + 6, 0xE0 | (drv->drive << 4) | ((current_lba >> 24) & 0x0F));

    /* Send sector count and LBA */
    outb(base + 2, 1);
    outb(base + 3, (u8)(current_lba));
    outb(base + 4, (u8)(current_lba >> 8));
    outb(base + 5, (u8)(current_lba >> 16));

    /* Send write command */
    outb(base + 7, ATA_CMD_WRITE_PIO);

    /* Wait for DRQ */
    if(!ata_wait_ready(base + 7)) {
      return -EIO;
    }

    /* Write data */
    for(int i = 0; i < 256; i++) {
      outw(base, buf[sector * 256 + i]);
    }

    /* Flush cache */
    outb(base + 7, ATA_CMD_CACHE_FLUSH);
    ata_wait_bsy(base + 7);
  }

  return 0;
}
