/**
 * @file pci.c
 * @brief PCI bus driver.
 *
 * Configuration-space I/O via 0xCF8/0xCFC, enumeration of buses 0-255 /
 * slots 0-31 / functions 0-7, capability-list traversal, BAR resolution
 * (including 64-bit pairs).
 */

#include <alcor2/arch/io.h>
#include <alcor2/drivers/console.h>
#include <alcor2/drivers/pci.h>

static inline u32 pci_addr(u8 bus, u8 slot, u8 func, u8 offset)
{
  return 0x80000000 | ((u32)bus << 16) | ((u32)slot << 11) | ((u32)func << 8) |
         (offset & 0xFC);
}

u32 pci_read32(u8 bus, u8 slot, u8 func, u8 offset)
{
  outl(PCI_CONFIG_ADDR, pci_addr(bus, slot, func, offset));
  return inl(PCI_CONFIG_DATA);
}

u16 pci_read16(u8 bus, u8 slot, u8 func, u8 offset)
{
  u32 val = pci_read32(bus, slot, func, offset);
  return (val >> ((offset & 2) * 8)) & 0xFFFF;
}

u8 pci_read8(u8 bus, u8 slot, u8 func, u8 offset)
{
  u32 val = pci_read32(bus, slot, func, offset);
  return (val >> ((offset & 3) * 8)) & 0xFF;
}

void pci_write32(u8 bus, u8 slot, u8 func, u8 offset, u32 val)
{
  outl(PCI_CONFIG_ADDR, pci_addr(bus, slot, func, offset));
  outl(PCI_CONFIG_DATA, val);
}

void pci_write16(u8 bus, u8 slot, u8 func, u8 offset, u16 val)
{
  u32 old     = pci_read32(bus, slot, func, offset);
  int shift   = (offset & 2) * 8;
  u32 mask    = 0xFFFF << shift;
  u32 new_val = (old & ~mask) | ((u32)val << shift);
  pci_write32(bus, slot, func, offset, new_val);
}

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

bool pci_for_each(
    bool (*cb)(const pci_device_t *, void *), void *ctx, pci_device_t *out
)
{
  pci_device_t dev;
  for(u16 bus = 0; bus < 256; bus++) {
    for(u8 slot = 0; slot < 32; slot++) {
      for(u8 func = 0; func < 8; func++) {
        u16 vendor = pci_read16(bus, slot, func, PCI_VENDOR_ID);
        if(vendor == 0xFFFF) {
          /* No function 0 → empty slot; otherwise just gap, keep scanning. */
          if(func == 0)
            break;
          continue;
        }

        pci_read_device(bus, slot, func, &dev);
        if(cb(&dev, ctx)) {
          if(out)
            *out = dev;
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

typedef struct
{
  u8 class_code;
  u8 subclass;
} class_match_t;

static bool match_class(const pci_device_t *d, void *ctx)
{
  const class_match_t *m = ctx;
  return d->class_code == m->class_code && d->subclass == m->subclass;
}

bool pci_find_device(u8 class_code, u8 subclass, pci_device_t *dev)
{
  class_match_t m = {class_code, subclass};
  return pci_for_each(match_class, &m, dev);
}

bool pci_bar_is_mmio(const pci_device_t *dev, int idx)
{
  if(idx < 0 || idx > 5)
    return false;
  return (dev->bar[idx] & 0x1) == 0;
}

bool pci_bar_is_64(const pci_device_t *dev, int idx)
{
  if(idx < 0 || idx > 4)
    return false;
  if(dev->bar[idx] & 0x1)
    return false;
  return ((dev->bar[idx] >> 1) & 0x3) == 0x2;
}

u64 pci_bar_base64(const pci_device_t *dev, int idx)
{
  if(idx < 0 || idx > 5)
    return 0;
  u32 lo = dev->bar[idx];
  if(lo & 0x1)
    return lo & ~0x3u; /* I/O port */
  if(pci_bar_is_64(dev, idx)) {
    u32 hi = dev->bar[idx + 1];
    return ((u64)hi << 32) | (lo & ~0xFu);
  }
  return lo & ~0xFu;
}

bool pci_find_capability(const pci_device_t *dev, u8 cap_id, u8 *out)
{
  u16 status = pci_read16(dev->bus, dev->slot, dev->func, PCI_STATUS);
  if(!(status & PCI_STATUS_CAP_LIST))
    return false;

  u8  off  = pci_read8(dev->bus, dev->slot, dev->func, PCI_CAP_PTR) & 0xFC;
  int safe = 48; /* PCI cap chain is bounded to config space (256 bytes / 4). */
  while(off && safe-- > 0) {
    u8 id = pci_read8(dev->bus, dev->slot, dev->func, off);
    if(id == cap_id) {
      if(out)
        *out = off;
      return true;
    }
    off = pci_read8(dev->bus, dev->slot, dev->func, off + 1) & 0xFC;
  }
  return false;
}

bool pci_find_capability_next(
    const pci_device_t *dev, u8 current, u8 cap_id, u8 *out
)
{
  u8  off  = pci_read8(dev->bus, dev->slot, dev->func, current + 1) & 0xFC;
  int safe = 48;
  while(off && safe-- > 0) {
    u8 id = pci_read8(dev->bus, dev->slot, dev->func, off);
    if(id == cap_id) {
      if(out)
        *out = off;
      return true;
    }
    off = pci_read8(dev->bus, dev->slot, dev->func, off + 1) & 0xFC;
  }
  return false;
}

void pci_enable_bus_master(const pci_device_t *dev)
{
  u16 cmd = pci_read16(dev->bus, dev->slot, dev->func, PCI_COMMAND);
  cmd |= PCI_CMD_IO | PCI_CMD_MEMORY | PCI_CMD_MASTER;
  pci_write16(dev->bus, dev->slot, dev->func, PCI_COMMAND, cmd);
}
