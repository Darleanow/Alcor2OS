/**
 * @file src/drivers/virtio/virtio_pci.c
 * @brief Modern virtio PCI transport: cap discovery + status machine.
 *
 * Walks the PCI vendor-capability list to locate the common / notify / ISR /
 * device-cfg regions, maps each into a kernel MMIO arena, and provides the
 * RESET → ACK → DRIVER → FEATURES_OK → DRIVER_OK sequence.
 */

#include <alcor2/drivers/console.h>
#include <alcor2/drivers/pci.h>
#include <alcor2/drivers/virtio.h>
#include <alcor2/mm/memory_layout.h>
#include <alcor2/mm/vmm.h>

/* Bump allocator for MMIO virtual addresses. PCI BARs are small and we have
 * 256 MiB before bumping into anything important above us. */
static u64   mmio_next = KERNEL_MMIO_BASE;

static void *mmio_map(u64 phys, u32 length)
{
  if(length == 0)
    return (void *)0;

  u64 page_off = phys & 0xFFFULL;
  u64 phys_pg  = phys & ~0xFFFULL;
  u64 npages   = (page_off + length + 0xFFFULL) >> 12;
  u64 vbase    = mmio_next;

  for(u64 i = 0; i < npages; i++)
    vmm_map(
        vbase + (i << 12), phys_pg + (i << 12), VMM_PRESENT | VMM_WRITE | VMM_NX
    );

  mmio_next += npages << 12;
  return (void *)(vbase + page_off);
}

static bool match_virtio(const pci_device_t *d, void *ctx)
{
  const u16 *target = ctx;
  return d->vendor_id == VIRTIO_VENDOR_ID && d->device_id == *target;
}

/** Read one virtio_pci_cap field at offset @p f within the cap at @p cap. */
static u8 cap_u8(const pci_device_t *p, u8 cap, u8 f)
{
  return pci_read8(p->bus, p->slot, p->func, cap + f);
}
static u32 cap_u32(const pci_device_t *p, u8 cap, u8 f)
{
  return pci_read32(p->bus, p->slot, p->func, cap + f);
}

bool virtio_probe(virtio_dev_t *vd, u16 device_id)
{
  if(!vd)
    return false;

  if(!pci_for_each(match_virtio, &device_id, &vd->pci))
    return false;

  pci_enable_bus_master(&vd->pci);

  /* Walk the PCI vendor cap chain. Each virtio cap has this header:
   *   +0 cap_vndr (=0x09)   +1 cap_next   +2 cap_len   +3 cfg_type
   *   +4 bar   +5 id   +6 padding[2]   +8 offset   +12 length
   *   NOTIFY adds +16 notify_off_multiplier. */
  u8 off;
  if(!pci_find_capability(&vd->pci, PCI_CAP_ID_VENDOR, &off)) {
    console_printf(
        "[virtio] no vendor caps on %02x:%02x.%x\n", vd->pci.bus, vd->pci.slot,
        vd->pci.func
    );
    return false;
  }

  do {
    u8  cfg_type   = cap_u8(&vd->pci, off, 3);
    u8  bar_idx    = cap_u8(&vd->pci, off, 4);
    u32 region_off = cap_u32(&vd->pci, off, 8);
    u32 region_len = cap_u32(&vd->pci, off, 12);

    if(bar_idx <= 5 && pci_bar_is_mmio(&vd->pci, bar_idx)) {
      u64   bar_base = pci_bar_base64(&vd->pci, bar_idx);
      void *region   = mmio_map(bar_base + region_off, region_len);

      switch(cfg_type) {
      case VIRTIO_PCI_CAP_COMMON_CFG:
        vd->common = (virtio_pci_common_cfg_t *)region;
        break;
      case VIRTIO_PCI_CAP_NOTIFY_CFG:
        vd->notify_base           = region;
        vd->notify_off_multiplier = cap_u32(&vd->pci, off, 16);
        break;
      case VIRTIO_PCI_CAP_ISR_CFG:
        vd->isr = (volatile u8 *)region;
        break;
      case VIRTIO_PCI_CAP_DEVICE_CFG:
        vd->device_cfg     = region;
        vd->device_cfg_len = region_len;
        break;
      default:
        break;
      }
    }
  } while(pci_find_capability_next(&vd->pci, off, PCI_CAP_ID_VENDOR, &off));

  if(!vd->common || !vd->notify_base || !vd->isr) {
    console_printf("[virtio] device %04x missing required caps\n", device_id);
    return false;
  }
  return true;
}

bool virtio_init_features(virtio_dev_t *vd, u64 wanted_features)
{
  virtio_pci_common_cfg_t *c = vd->common;

  c->device_status = 0;
  while(c->device_status != 0)
    ;

  c->device_status = VIRTIO_STATUS_ACKNOWLEDGE;
  c->device_status = VIRTIO_STATUS_ACKNOWLEDGE | VIRTIO_STATUS_DRIVER;

  c->device_feature_select = 0;
  u64 dev_feat             = c->device_feature;
  c->device_feature_select = 1;
  dev_feat |= ((u64)c->device_feature) << 32;

  u64 accepted = (wanted_features | (1ULL << VIRTIO_F_VERSION_1)) & dev_feat;

  c->driver_feature_select = 0;
  c->driver_feature        = (u32)accepted;
  c->driver_feature_select = 1;
  c->driver_feature        = (u32)(accepted >> 32);

  c->device_status = VIRTIO_STATUS_ACKNOWLEDGE | VIRTIO_STATUS_DRIVER |
                     VIRTIO_STATUS_FEATURES_OK;

  if(!(c->device_status & VIRTIO_STATUS_FEATURES_OK)) {
    c->device_status |= VIRTIO_STATUS_FAILED;
    return false;
  }
  return true;
}

void virtio_set_driver_ok(virtio_dev_t *vd)
{
  vd->common->device_status |= VIRTIO_STATUS_DRIVER_OK;
}

void virtio_set_failed(virtio_dev_t *vd)
{
  vd->common->device_status |= VIRTIO_STATUS_FAILED;
}
