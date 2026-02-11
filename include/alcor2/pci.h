/**
 * @file pci.h
 * @brief PCI bus driver.
 *
 * Provides PCI configuration space access and device enumeration.
 * Used primarily to locate the IDE controller for DMA support.
 */

#ifndef ALCOR2_PCI_H
#define ALCOR2_PCI_H

#include <alcor2/types.h>

/** @brief PCI config address port. */
#define PCI_CONFIG_ADDR 0xCF8
/** @brief PCI config data port. */
#define PCI_CONFIG_DATA 0xCFC

/** @brief Mass storage class code. */
#define PCI_CLASS_STORAGE 0x01
/** @brief IDE controller subclass. */
#define PCI_SUBCLASS_IDE 0x01

/* BAR register offsets */
#define PCI_BAR0 0x10
#define PCI_BAR1 0x14
#define PCI_BAR2 0x18
#define PCI_BAR3 0x1C
#define PCI_BAR4 0x20
#define PCI_BAR5 0x24

/* Configuration space register offsets */
#define PCI_VENDOR_ID   0x00
#define PCI_DEVICE_ID   0x02
#define PCI_COMMAND     0x04
#define PCI_STATUS      0x06
#define PCI_CLASS       0x0B
#define PCI_SUBCLASS    0x0A
#define PCI_PROG_IF     0x09
#define PCI_HEADER_TYPE 0x0E
#define PCI_INTERRUPT   0x3C

/* Command register bits */
#define PCI_CMD_IO     0x0001
#define PCI_CMD_MEMORY 0x0002
#define PCI_CMD_MASTER 0x0004

/**
 * @brief PCI device descriptor.
 */
typedef struct pci_device
{
  u8  bus;        /**< Bus number. */
  u8  slot;       /**< Device slot. */
  u8  func;       /**< Function number. */
  u16 vendor_id;  /**< Vendor ID. */
  u16 device_id;  /**< Device ID. */
  u8  class_code; /**< Class code. */
  u8  subclass;   /**< Subclass. */
  u8  prog_if;    /**< Programming interface. */
  u8  irq;        /**< Interrupt line. */
  u32 bar[6];     /**< Base Address Registers. */
} pci_device_t;

/**
 * @brief Read 8-bit value from PCI configuration space.
 * @param bus    Bus number.
 * @param slot   Device slot.
 * @param func   Function number.
 * @param offset Register offset.
 * @return 8-bit config value.
 */
u8 pci_read8(u8 bus, u8 slot, u8 func, u8 offset);

/**
 * @brief Read 16-bit value from PCI configuration space.
 * @param bus    Bus number.
 * @param slot   Device slot.
 * @param func   Function number.
 * @param offset Register offset.
 * @return 16-bit config value.
 */
u16 pci_read16(u8 bus, u8 slot, u8 func, u8 offset);

/**
 * @brief Read 32-bit value from PCI configuration space.
 * @param bus    Bus number.
 * @param slot   Device slot.
 * @param func   Function number.
 * @param offset Register offset.
 * @return 32-bit config value.
 */
u32 pci_read32(u8 bus, u8 slot, u8 func, u8 offset);

/**
 * @brief Write 8-bit value to PCI configuration space.
 * @param bus    Bus number.
 * @param slot   Device slot.
 * @param func   Function number.
 * @param offset Register offset.
 * @param val    Value to write.
 */
void pci_write8(u8 bus, u8 slot, u8 func, u8 offset, u8 val);

/**
 * @brief Write 16-bit value to PCI configuration space.
 * @param bus    Bus number.
 * @param slot   Device slot.
 * @param func   Function number.
 * @param offset Register offset.
 * @param val    Value to write.
 */
void pci_write16(u8 bus, u8 slot, u8 func, u8 offset, u16 val);

/**
 * @brief Write 32-bit value to PCI configuration space.
 * @param bus    Bus number.
 * @param slot   Device slot.
 * @param func   Function number.
 * @param offset Register offset.
 * @param val    Value to write.
 */
void pci_write32(u8 bus, u8 slot, u8 func, u8 offset, u32 val);

/**
 * @brief Find PCI device by class and subclass.
 * @param class_code PCI class code.
 * @param subclass   PCI subclass code.
 * @param dev        Output device descriptor (filled if found).
 * @return true if found.
 */
bool pci_find_device(u8 class_code, u8 subclass, pci_device_t *dev);

/**
 * @brief Enable bus mastering for a PCI device.
 * @param dev Device to configure.
 */
void pci_enable_bus_master(pci_device_t *dev);

/** @brief Initialize PCI subsystem. */
void pci_init(void);

#endif
