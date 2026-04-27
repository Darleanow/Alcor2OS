/**
 * @file ata.h
 * @brief ATA/IDE DMA driver.
 *
 * Provides sector-level read/write access to ATA hard drives using DMA.
 * Falls back to PIO during early boot (before PCI/DMA setup).
 * Supports both LBA28 and LBA48 addressing modes.
 */

#ifndef ALCOR2_ATA_H
#define ALCOR2_ATA_H

#include <alcor2/types.h>

/* I/O port bases */
#define ATA_PRIMARY_DATA   0x1F0
#define ATA_PRIMARY_CTRL   0x3F6
#define ATA_SECONDARY_DATA 0x170
#define ATA_SECONDARY_CTRL 0x376

/* Register offsets (from base) */
#define ATA_REG_DATA     0
#define ATA_REG_ERROR    1
#define ATA_REG_FEATURES 1
#define ATA_REG_SECCOUNT 2
#define ATA_REG_LBA0     3
#define ATA_REG_LBA1     4
#define ATA_REG_LBA2     5
#define ATA_REG_HDDEVSEL 6
#define ATA_REG_COMMAND  7
#define ATA_REG_STATUS   7

/* Status register bits */
#define ATA_SR_BSY  0x80 /* Busy */
#define ATA_SR_DRDY 0x40 /* Device ready */
#define ATA_SR_DF   0x20 /* Device fault */
#define ATA_SR_DRQ  0x08 /* Data request */
#define ATA_SR_ERR  0x01 /* Error */

/* Commands */
#define ATA_CMD_READ_PIO        0x20
#define ATA_CMD_READ_PIO_EXT    0x24
#define ATA_CMD_READ_DMA        0xC8
#define ATA_CMD_READ_DMA_EXT    0x25
#define ATA_CMD_WRITE_PIO       0x30
#define ATA_CMD_WRITE_PIO_EXT   0x34
#define ATA_CMD_WRITE_DMA       0xCA
#define ATA_CMD_WRITE_DMA_EXT   0x35
#define ATA_CMD_CACHE_FLUSH     0xE7
#define ATA_CMD_CACHE_FLUSH_EXT 0xEA
#define ATA_CMD_IDENTIFY        0xEC

/* Bus Master IDE registers (offset from BAR4) */
#define BMI_CMD    0x00 /* Command register */
#define BMI_STATUS 0x02 /* Status register */
#define BMI_PRDT   0x04 /* PRD table address */

/* BMI command bits */
#define BMI_CMD_START 0x01
#define BMI_CMD_READ  0x08 /* 1 = read, 0 = write */

/* BMI status bits */
#define BMI_STATUS_ACTIVE 0x01
#define BMI_STATUS_ERR    0x02
#define BMI_STATUS_IRQ    0x04

#define ATA_SECTOR_SIZE   512

#define IRQ_ATA_PRIMARY   14
#define IRQ_ATA_SECONDARY 15

/**
 * @brief Physical Region Descriptor for DMA.
 */
typedef struct PACKED
{
  u32 phys_addr;  /* Physical buffer address */
  u16 byte_count; /* Byte count (0 = 64K) */
  u16 flags;      /* Bit 15 = end of table */
} ata_prd_t;

/**
 * @brief Channel state for IRQ synchronization.
 */
typedef enum
{
  ATA_STATE_IDLE,
  ATA_STATE_PENDING
} ata_state_t;

/**
 * @brief ATA channel descriptor (primary or secondary).
 */
typedef struct ata_channel
{
  u16         base;       /* Data port base (0x1F0 or 0x170) */
  u16         ctrl;       /* Control port (0x3F6 or 0x376) */
  u16         bmi;        /* Bus Master IDE base */
  u8          irq;        /* IRQ number (14 or 15) */
  ata_state_t state;      /* Current I/O state */
  u8          status;     /* Last status from IRQ */
  u8          bmi_status; /* Last BMI status */
  u8          error;      /* Last error register */
  void       *waiter;     /* Blocked task waiting for IRQ */
  ata_prd_t  *prdt;       /* PRD table (virtual) */
  u64         prdt_phys;  /* PRD table (physical) */
  bool        dma_ok;     /* DMA available */
} ata_channel_t;

/**
 * @brief ATA drive descriptor.
 */
typedef struct ata_drive
{
  ata_channel_t *channel;    /* Parent channel */
  u8             slave;      /* 0 = master, 1 = slave */
  bool           present;    /* Drive detected */
  bool           atapi;      /* ATAPI device (not supported) */
  bool           lba48;      /* Supports 48-bit LBA */
  bool           dma;        /* Supports DMA */
  u64            sectors;    /* Total sector count */
  char           model[41];  /* Model string */
  char           serial[21]; /* Serial number */
} ata_drive_t;

/**
 * @brief Initialize ATA subsystem and detect drives.
 */
void ata_init(void);

/**
 * @brief Get drive descriptor.
 * @param idx Drive index (0-3: primary master/slave, secondary master/slave).
 * @return Drive pointer or NULL if not present.
 */
ata_drive_t *ata_get_drive(u8 idx);

/**
 * @brief Read sectors from drive.
 * @param drive Drive index.
 * @param lba   Starting logical block address.
 * @param count Number of sectors to read.
 * @param buf   Output buffer (must hold count * 512 bytes).
 * @return 0 on success, negative errno on error.
 */
i64 ata_read(u8 drive, u64 lba, u32 count, void *buf);

/**
 * @brief Write sectors to drive.
 * @param drive Drive index.
 * @param lba   Starting logical block address.
 * @param count Number of sectors to write.
 * @param buf   Input buffer (count * 512 bytes).
 * @return 0 on success, negative errno on error.
 */
i64 ata_write(u8 drive, u64 lba, u32 count, const void *buf);

/**
 * @brief IRQ handler (called from IDT stub).
 * @param channel 0 for primary, 1 for secondary.
 */
void ata_irq(u8 channel);

#endif
