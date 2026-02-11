/**
 * @file pci.c
 * @brief PCI bus driver.
 *
 * Provides configuration space access via I/O ports 0xCF8/0xCFC.
 * Enumerates devices on buses 0-255, slots 0-31, functions 0-7.
 */

#include <alcor2/console.h>
#include <alcor2/io.h>
#include <alcor2/pci.h>

/* Build PCI config address dword. */
static inline u32 pci_addr(u8 bus, u8 slot, u8 func, u8 offset)
{
  return 0x80000000 | ((u32)bus << 16) | ((u32)slot << 11) | ((u32)func << 8) |
         (offset & 0xFC);
}

/**
 * @brief Read 32-bit value from PCI config space.
 * @param bus  Bus number.
 * @param slot Device slot.
 * @param func Function number.
 * @param offset Register offset.
 * @return 32-bit config value.
 */
u32 pci_read32(u8 bus, u8 slot, u8 func, u8 offset)
{
  outl(PCI_CONFIG_ADDR, pci_addr(bus, slot, func, offset));
  return inl(PCI_CONFIG_DATA);
}

/**
 * @brief Read 16-bit value from PCI config space.
 * @param bus  Bus number.
 * @param slot Device slot.
 * @param func Function number.
 * @param offset Register offset.
 * @return 16-bit config value.
 */
u16 pci_read16(u8 bus, u8 slot, u8 func, u8 offset)
{
  u32 val = pci_read32(bus, slot, func, offset);
  return (val >> ((offset & 2) * 8)) & 0xFFFF;
}

/**
 * @brief Read 8-bit value from PCI config space.
 * @param bus  Bus number.
 * @param slot Device slot.
 * @param func Function number.
 * @param offset Register offset.
 * @return 8-bit config value.
 */
u8 pci_read8(u8 bus, u8 slot, u8 func, u8 offset)
{
  u32 val = pci_read32(bus, slot, func, offset);
  return (val >> ((offset & 3) * 8)) & 0xFF;
}

/**
 * @brief Write 32-bit value to PCI config space.
 * @param bus  Bus number.
 * @param slot Device slot.
 * @param func Function number.
 * @param offset Register offset.
 * @param val Value to write.
 */
void pci_write32(u8 bus, u8 slot, u8 func, u8 offset, u32 val)
{
  outl(PCI_CONFIG_ADDR, pci_addr(bus, slot, func, offset));
  outl(PCI_CONFIG_DATA, val);
}

/**
 * @brief Write 16-bit value to PCI config space (read-modify-write).
 * @param bus  Bus number.
 * @param slot Device slot.
 * @param func Function number.
 * @param offset Register offset.
 * @param val Value to write.
 */
void pci_write16(u8 bus, u8 slot, u8 func, u8 offset, u16 val)
{
  u32 old     = pci_read32(bus, slot, func, offset);
  int shift   = (offset & 2) * 8;
  u32 mask    = 0xFFFF << shift;
  u32 new_val = (old & ~mask) | ((u32)val << shift);
  pci_write32(bus, slot, func, offset, new_val);
}

/**
 * @brief Write 8-bit value to PCI config space (read-modify-write).
 * @param bus  Bus number.
 * @param slot Device slot.
 * @param func Function number.
 * @param offset Register offset.
 * @param val Value to write.
 */
// cppcheck-suppress unusedFunction
void pci_write8(u8 bus, u8 slot, u8 func, u8 offset, u8 val)
{
  u32 old     = pci_read32(bus, slot, func, offset);
  int shift   = (offset & 3) * 8;
  u32 mask    = 0xFF << shift;
  u32 new_val = (old & ~mask) | ((u32)val << shift);
  pci_write32(bus, slot, func, offset, new_val);
}

/**
 * @brief Populate device descriptor from config space.
 * @param bus  Bus number.
 * @param slot Device slot.
 * @param func Function number.
 * @param dev  Output device descriptor.
 */
static void pci_read_device(u8 bus, u8 slot, u8 func, pci_device_t *dev)
{
  dev->bus        = bus;
  dev->slot       = slot;
  dev->func       = func;
  dev->vendor_id  = pci_read16(bus, slot, func, PCI_VENDOR_ID);
  dev->device_id  = pci_read16(bus, slot, func, PCI_DEVICE_ID);
  dev->class_code = pci_read8(bus, slot, func, PCI_CLASS);
  dev->subclass   = pci_read8(bus, slot, func, PCI_SUBCLASS);
  dev->prog_if    = pci_read8(bus, slot, func, PCI_PROG_IF);
  dev->irq        = pci_read8(bus, slot, func, PCI_INTERRUPT);

  for(int i = 0; i < 6; i++)
    dev->bar[i] = pci_read32(bus, slot, func, PCI_BAR0 + i * 4);
}

/**
 * @brief Find first PCI device matching class/subclass.
 * @param class_code PCI class code.
 * @param subclass   PCI subclass code.
 * @param dev        Output device descriptor (filled if found).
 * @return true if found.
 */
bool pci_find_device(u8 class_code, u8 subclass, pci_device_t *dev)
{
  for(u16 bus = 0; bus < 256; bus++) {
    for(u8 slot = 0; slot < 32; slot++) {
      for(u8 func = 0; func < 8; func++) {
        u16 vendor = pci_read16(bus, slot, func, PCI_VENDOR_ID);
        if(vendor == 0xFFFF)
          continue;

        u8 cls = pci_read8(bus, slot, func, PCI_CLASS);
        u8 sub = pci_read8(bus, slot, func, PCI_SUBCLASS);

        if(cls == class_code && sub == subclass) {
          pci_read_device(bus, slot, func, dev);
          return true;
        }

        if(func == 0) {
          u8 header = pci_read8(bus, slot, func, PCI_HEADER_TYPE);
          if(!(header & 0x80))
            break;
        }
      }
    }
  }
  return false;
}

/**
 * @brief Enable I/O, memory, and bus master for a PCI device.
 * @param dev Device to configure.
 */
void pci_enable_bus_master(const pci_device_t *dev)
{
  u16 cmd = pci_read16(dev->bus, dev->slot, dev->func, PCI_COMMAND);
  cmd |= PCI_CMD_IO | PCI_CMD_MEMORY | PCI_CMD_MASTER;
  pci_write16(dev->bus, dev->slot, dev->func, PCI_COMMAND, cmd);
}
