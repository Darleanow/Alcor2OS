/**
 * @file include/alcor2/drivers/virtio.h
 * @brief Modern (PCI 1.0+) virtio transport and split virtqueue.
 *
 * Discovers a virtio-pci device, parses its vendor-specific capabilities to
 * locate the common / notify / ISR / device-cfg regions, walks the device
 * status machine, and exposes a minimal split-virtqueue API. Concrete drivers
 * (virtio-input today) sit on top of this layer.
 */

#ifndef ALCOR2_VIRTIO_H
#define ALCOR2_VIRTIO_H

#include <alcor2/drivers/pci.h>
#include <alcor2/types.h>

/** Red Hat virtio vendor ID (covers all virtio-pci devices). */
#define VIRTIO_VENDOR_ID 0x1AF4

/** Virtio device-ID base for modern devices: 0x1040 + device_type. */
#define VIRTIO_DEVICE_MODERN_BASE 0x1040

/** Modern device IDs we care about. */
#define VIRTIO_DEVICE_INPUT 0x1052 /* 0x1040 + 18 (input) */

/** virtio vendor-cap cfg_type values. */
#define VIRTIO_PCI_CAP_COMMON_CFG 1
#define VIRTIO_PCI_CAP_NOTIFY_CFG 2
#define VIRTIO_PCI_CAP_ISR_CFG    3
#define VIRTIO_PCI_CAP_DEVICE_CFG 4
#define VIRTIO_PCI_CAP_PCI_CFG    5

/** Device-status bits. */
#define VIRTIO_STATUS_ACKNOWLEDGE 1
#define VIRTIO_STATUS_DRIVER      2
#define VIRTIO_STATUS_DRIVER_OK   4
#define VIRTIO_STATUS_FEATURES_OK 8
#define VIRTIO_STATUS_NEEDS_RESET 64
#define VIRTIO_STATUS_FAILED      128

/** Feature bit indicating virtio 1.0+ compliance (mandatory for modern). */
#define VIRTIO_F_VERSION_1 32

/** Descriptor flags (split virtqueue). */
#define VIRTQ_DESC_F_NEXT     1
#define VIRTQ_DESC_F_WRITE    2
#define VIRTQ_DESC_F_INDIRECT 4

/** @brief Common configuration layout (mapped MMIO; little-endian). */
typedef struct virtio_pci_common_cfg
{
  volatile u32 device_feature_select;
  volatile u32 device_feature;
  volatile u32 driver_feature_select;
  volatile u32 driver_feature;
  volatile u16 msix_config;
  volatile u16 num_queues;
  volatile u8  device_status;
  volatile u8  config_generation;
  volatile u16 queue_select;
  volatile u16 queue_size;
  volatile u16 queue_msix_vector;
  volatile u16 queue_enable;
  volatile u16 queue_notify_off;
  volatile u64 queue_desc;
  volatile u64 queue_driver;
  volatile u64 queue_device;
} virtio_pci_common_cfg_t;

/** @brief Split-virtqueue descriptor. */
typedef struct virtq_desc
{
  u64 addr;
  u32 len;
  u16 flags;
  u16 next;
} virtq_desc_t;

/** @brief Split-virtqueue available ring. */
typedef struct virtq_avail
{
  u16 flags;
  u16 idx;
  u16 ring[]; /* size: queue_size */
  /* u16 used_event; (placed by allocator at ring[queue_size]) */
} virtq_avail_t;

/** @brief Used-ring entry. */
typedef struct virtq_used_elem
{
  u32 id;
  u32 len;
} virtq_used_elem_t;

/** @brief Split-virtqueue used ring. */
typedef struct virtq_used
{
  u16               flags;
  u16               idx;
  virtq_used_elem_t ring[]; /* size: queue_size */
  /* u16 avail_event; (placed by allocator at ring[queue_size]) */
} virtq_used_t;

/** @brief Live virtio device (one per PCI function). */
typedef struct virtio_dev
{
  pci_device_t             pci;
  virtio_pci_common_cfg_t *common;
  volatile u8             *isr;
  u8                      *notify_base;
  u32                      notify_off_multiplier;
  u8                      *device_cfg;
  u32                      device_cfg_len;
} virtio_dev_t;

/** @brief Live virtqueue. */
typedef struct virtio_vq
{
  u16            index;
  u16            size;
  u16            last_used_idx;
  virtq_desc_t  *desc;
  virtq_avail_t *avail;
  virtq_used_t  *used;
  u64            desc_phys;
  u64            avail_phys;
  u64            used_phys;
  u16            notify_off;
  u16            free_head;
  u16            num_free;
  void          *backing_page; /* page that holds desc/avail/used */
} virtio_vq_t;

/**
 * @brief Locate the first virtio-pci device matching @p device_id.
 * @return @c true on match; @p vd is populated with capability pointers.
 */
bool virtio_probe(virtio_dev_t *vd, u16 device_id);

/**
 * @brief Reset the device and negotiate @p wanted_features.
 *
 * Walks the device-status machine: RESET → ACK → DRIVER → FEATURES_OK.
 * The caller writes @c DRIVER_OK after setting up its queues via
 * @ref virtio_set_driver_ok.
 *
 * @param vd              Probed device.
 * @param wanted_features Subset of the device features we accept. Bit 32
 *                        (@ref VIRTIO_F_VERSION_1) is OR'd in automatically.
 * @return @c true on success, @c false if features-ok handshake fails.
 */
bool virtio_init_features(virtio_dev_t *vd, u64 wanted_features);

/** @brief Write @c DRIVER_OK once queues are wired. */
void virtio_set_driver_ok(virtio_dev_t *vd);

/** @brief Set the device status to FAILED (sticky error). */
void virtio_set_failed(virtio_dev_t *vd);

/**
 * @brief Initialise virtqueue @p index using a freshly allocated page.
 * @return @c true if the queue was set up and enabled.
 */
bool virtio_vq_init(virtio_dev_t *vd, virtio_vq_t *vq, u16 index);

/** @brief Allocate a descriptor; returns -1 on exhaustion. */
i16 virtio_vq_alloc_desc(virtio_vq_t *vq);

/** @brief Submit chain head @p head into the avail ring (no kick). */
void virtio_vq_submit(virtio_vq_t *vq, u16 head);

/**
 * @brief Pop one used-ring entry.
 * @param out_head Filled with the descriptor chain head id.
 * @param out_len  Filled with the device-written length.
 * @return @c true if an entry was consumed.
 */
bool virtio_vq_pop(virtio_vq_t *vq, u16 *out_head, u32 *out_len);

/** @brief Kick @p vq (MMIO write to the device's notify register). */
void virtio_vq_kick(virtio_dev_t *vd, virtio_vq_t *vq);

/** @brief Probe for virtio-mouse and wire it into the kernel mouse broker. */
bool virtio_input_init(void);

#endif
