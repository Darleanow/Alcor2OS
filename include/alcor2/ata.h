/**
 * @file include/alcor2/ata.h
 * @brief ATA/IDE PIO-mode driver definitions and interfaces.
 *
 * Minimal definitions used by the kernel to control ATA/IDE devices
 * (I/O ports, status bits, commands), the `ata_drive_t` descriptor,
 * and the public driver API.
 */

#ifndef ALCOR2_ATA_H
#define ALCOR2_ATA_H

#include <alcor2/types.h>

/** @name Primary channel I/O ports
 * Low-level port addresses for the primary ATA channel.
 * @{ */
#define ATA_PRIMARY_DATA     0x1F0
#define ATA_PRIMARY_ERROR    0x1F1
#define ATA_PRIMARY_SECCOUNT 0x1F2
#define ATA_PRIMARY_LBA_LO   0x1F3
#define ATA_PRIMARY_LBA_MID  0x1F4
#define ATA_PRIMARY_LBA_HI   0x1F5
#define ATA_PRIMARY_DRIVE    0x1F6
#define ATA_PRIMARY_STATUS   0x1F7
#define ATA_PRIMARY_CMD      0x1F7
#define ATA_PRIMARY_CTRL     0x3F6
/** @} */

/** @name Secondary channel I/O ports
 * @{ */
#define ATA_SECONDARY_DATA     0x170
#define ATA_SECONDARY_ERROR    0x171
#define ATA_SECONDARY_SECCOUNT 0x172
#define ATA_SECONDARY_LBA_LO   0x173
#define ATA_SECONDARY_LBA_MID  0x174
#define ATA_SECONDARY_LBA_HI   0x175
#define ATA_SECONDARY_DRIVE    0x176
#define ATA_SECONDARY_STATUS   0x177
#define ATA_SECONDARY_CMD      0x177
#define ATA_SECONDARY_CTRL     0x376
/** @} */

/**
 * @brief ATA status register bit masks.
 *
 * These masks apply to the status register read from the channel status port.
 */
#define ATA_SR_BSY  0x80
#define ATA_SR_DRDY 0x40
#define ATA_SR_DF   0x20
#define ATA_SR_DSC  0x10
#define ATA_SR_DRQ  0x08
#define ATA_SR_CORR 0x04
#define ATA_SR_IDX  0x02
#define ATA_SR_ERR  0x01

/** @brief ATA command opcodes used by the driver. */
#define ATA_CMD_READ_PIO        0x20
#define ATA_CMD_READ_PIO_EXT    0x24
#define ATA_CMD_WRITE_PIO       0x30
#define ATA_CMD_WRITE_PIO_EXT   0x34
#define ATA_CMD_CACHE_FLUSH     0xE7
#define ATA_CMD_CACHE_FLUSH_EXT 0xEA
#define ATA_CMD_IDENTIFY        0xEC

/** @brief Drive selection values for the drive/head register. */
#define ATA_DRIVE_MASTER 0xA0
#define ATA_DRIVE_SLAVE  0xB0

/** @brief Size of a logical sector in bytes. */
#define ATA_SECTOR_SIZE 512

/**
 * @brief Descriptor for a detected ATA drive.
 *
 * The driver maintains one of these per logical drive. Fields are
 * populated during identification and used for subsequent I/O.
 */
typedef struct
{
  u16  base;       /**< I/O base port for the channel. */
  u16  ctrl;       /**< Control port for the channel. */
  u8   drive;      /**< 0 = master, 1 = slave on the channel. */
  bool present;    /**< True when a drive has been detected. */
  bool is_atapi;   /**< True for ATAPI devices (optical drives). */
  u64  sectors;    /**< Total number of sectors (LBA48 when available). */
  char model[41];  /**< NUL-terminated model string (trimmed). */
  char serial[21]; /**< NUL-terminated serial number string. */
} ata_drive_t;

/**
 * @brief Initialize the ATA driver and detect attached drives.
 *
 * After calling this function, detected drives are available via
 * `ata_get_drive()`.
 */
void ata_init(void);

/**
 * @brief Return a descriptor for the specified logical drive.
 * @param drive Drive index: 0=primary master, 1=primary slave,
 * 2=secondary master, 3=secondary slave.
 * @return Pointer to `ata_drive_t` or NULL if not present.
 */
ata_drive_t *ata_get_drive(u8 drive);

/**
 * @brief Read sectors from a drive using PIO.
 * @param drive Drive index (0-3).
 * @param lba Starting sector (LBA).
 * @param count Number of sectors to read.
 * @param buffer Destination buffer (size must be count * %ATA_SECTOR_SIZE).
 * @return 0 on success, negative value on error.
 */
i64 ata_read(u8 drive, u64 lba, u32 count, void *buffer);

/**
 * @brief Write sectors to a drive using PIO.
 * @param drive Drive index (0-3).
 * @param lba Starting sector (LBA).
 * @param count Number of sectors to write.
 * @param buffer Source buffer (size must be count * %ATA_SECTOR_SIZE).
 * @return 0 on success, negative value on error.
 */
i64 ata_write(u8 drive, u64 lba, u32 count, const void *buffer);

#endif /* ALCOR2_ATA_H */
