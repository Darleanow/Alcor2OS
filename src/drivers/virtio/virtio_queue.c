/**
 * @file src/drivers/virtio/virtio_queue.c
 * @brief Split-virtqueue allocator and ring management.
 *
 * One PMM page backs desc / avail / used for queue sizes up to 64 (which
 * fits in 4 KiB with room to spare): desc starts at offset 0, avail and used
 * are placed after it with their alignment requirements satisfied.
 */

#include <alcor2/drivers/virtio.h>
#include <alcor2/kstdlib.h>
#include <alcor2/mm/pmm.h>
#include <alcor2/mm/vmm.h>

#define VIRTIO_DEFAULT_QSIZE 64

static inline u64 align_up(u64 v, u64 a)
{
  return (v + a - 1) & ~(a - 1);
}

bool virtio_vq_init(virtio_dev_t *vd, virtio_vq_t *vq, u16 index)
{
  virtio_pci_common_cfg_t *c = vd->common;

  c->queue_select = index;
  u16 max         = c->queue_size;
  if(max == 0)
    return false;

  u16 size      = (max < VIRTIO_DEFAULT_QSIZE) ? max : VIRTIO_DEFAULT_QSIZE;
  c->queue_size = size;

  void *page = pmm_alloc();
  if(!page)
    return false;
  void *virt = phys_to_virt((u64)page);
  kmemset(virt, 0, 4096);

  /* Layout within the single backing page: desc at 0, avail after desc,
   * used after avail with 4-byte alignment. */
  u64 off_desc  = 0;
  u64 off_avail = off_desc + (u64)size * sizeof(virtq_desc_t);
  /* avail = u16 flags + u16 idx + u16 ring[size] + u16 used_event */
  u64 avail_bytes = 2 + 2 + 2u * size + 2;
  u64 off_used    = align_up(off_avail + avail_bytes, 4);

  vq->index         = index;
  vq->size          = size;
  vq->last_used_idx = 0;
  vq->desc          = (virtq_desc_t *)((u8 *)virt + off_desc);
  vq->avail         = (virtq_avail_t *)((u8 *)virt + off_avail);
  vq->used          = (virtq_used_t *)((u8 *)virt + off_used);
  vq->desc_phys     = (u64)page + off_desc;
  vq->avail_phys    = (u64)page + off_avail;
  vq->used_phys     = (u64)page + off_used;
  vq->backing_page  = page;

  for(u16 i = 0; i < size - 1; i++)
    vq->desc[i].next = i + 1;
  vq->desc[size - 1].next = 0xFFFF;
  vq->free_head           = 0;
  vq->num_free            = size;

  c->queue_desc        = vq->desc_phys;
  c->queue_driver      = vq->avail_phys;
  c->queue_device      = vq->used_phys;
  c->queue_msix_vector = 0xFFFF; /* no MSI-X */
  vq->notify_off       = c->queue_notify_off;
  c->queue_enable      = 1;
  return true;
}

i16 virtio_vq_alloc_desc(virtio_vq_t *vq)
{
  if(vq->num_free == 0)
    return -1;
  u16 d         = vq->free_head;
  vq->free_head = vq->desc[d].next;
  vq->num_free--;
  vq->desc[d].next  = 0;
  vq->desc[d].flags = 0;
  return (i16)d;
}

void virtio_vq_free_chain(virtio_vq_t *vq, u16 head)
{
  u16 cur = head;
  while(1) {
    u16 next           = vq->desc[cur].next;
    u16 has_next       = vq->desc[cur].flags & VIRTQ_DESC_F_NEXT;
    vq->desc[cur].next = vq->free_head;
    vq->free_head      = cur;
    vq->num_free++;
    if(!has_next)
      break;
    cur = next;
  }
}

void virtio_vq_submit(virtio_vq_t *vq, u16 head)
{
  u16 slot              = vq->avail->idx % vq->size;
  vq->avail->ring[slot] = head;
  __asm__ volatile("" ::: "memory");
  vq->avail->idx++;
}

bool virtio_vq_pop(virtio_vq_t *vq, u16 *out_head, u32 *out_len)
{
  if(vq->used->idx == vq->last_used_idx)
    return false;
  u16 slot = vq->last_used_idx % vq->size;
  if(out_head)
    *out_head = (u16)vq->used->ring[slot].id;
  if(out_len)
    *out_len = vq->used->ring[slot].len;
  vq->last_used_idx++;
  return true;
}

void virtio_vq_kick(virtio_dev_t *vd, virtio_vq_t *vq)
{
  __asm__ volatile("" ::: "memory");
  volatile u16 *notify =
      (volatile u16 *)(vd->notify_base +
                       (u64)vq->notify_off * vd->notify_off_multiplier);
  *notify = vq->index;
}
