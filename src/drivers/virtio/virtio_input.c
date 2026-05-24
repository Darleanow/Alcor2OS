/**
 * @file src/drivers/virtio/virtio_input.c
 * @brief Driver for virtio-input (we only handle the mouse subtype today).
 *
 * Stuffs the event virtqueue with empty buffers, parses incoming evdev-style
 * events into (dx, dy, wheel, button) state, and emits one packet to the
 * kernel mouse broker per EV_SYN report.
 */

#include <alcor2/arch/idt.h>
#include <alcor2/arch/pic.h>
#include <alcor2/drivers/console.h>
#include <alcor2/drivers/mouse.h>
#include <alcor2/drivers/virtio.h>
#include <alcor2/kstdlib.h>
#include <alcor2/mm/pmm.h>
#include <alcor2/mm/vmm.h>

/* evdev event codes (linux/input-event-codes.h, mouse subset). */
#define EV_SYN     0x00
#define EV_KEY     0x01
#define EV_REL     0x02
#define SYN_REPORT 0x00
#define REL_X      0x00
#define REL_Y      0x01
#define REL_WHEEL  0x08
#define BTN_LEFT   0x110
#define BTN_RIGHT  0x111
#define BTN_MIDDLE 0x112

typedef struct PACKED
{
  u16 type;
  u16 code;
  u32 value;
} virtio_input_event_t;

#define EVENT_BUFFERS 32

static virtio_dev_t         vd;
static virtio_vq_t          evq;
static virtio_vq_t          statusq;
static virtio_input_event_t event_buf[EVENT_BUFFERS];
static u16                  desc_to_slot[256];
static u8                   button_state;

/* Accumulator across one report. Flushed on EV_SYN. */
static i32  acc_dx;
static i32  acc_dy;
static i16  acc_dwheel;

static u64  event_buf_phys[EVENT_BUFFERS];

static void refill_descriptor(u16 desc_id)
{
  u16           slot = desc_to_slot[desc_id];
  virtq_desc_t *d    = &evq.desc[desc_id];
  d->addr            = event_buf_phys[slot];
  d->len             = sizeof(virtio_input_event_t);
  d->flags           = VIRTQ_DESC_F_WRITE;
  d->next            = 0;
  virtio_vq_submit(&evq, desc_id);
}

static void handle_event(const virtio_input_event_t *e)
{
  switch(e->type) {
  case EV_REL:
    if(e->code == REL_X)
      acc_dx += (i32)e->value;
    else if(e->code == REL_Y)
      acc_dy += (i32)e->value;
    else if(e->code == REL_WHEEL)
      acc_dwheel = (i16)((i32)acc_dwheel + (i32)e->value);
    break;
  case EV_KEY: {
    u8 mask = 0;
    if(e->code == BTN_LEFT)
      mask = ALCOR2_MOUSE_BTN_LEFT;
    else if(e->code == BTN_RIGHT)
      mask = ALCOR2_MOUSE_BTN_RIGHT;
    else if(e->code == BTN_MIDDLE)
      mask = ALCOR2_MOUSE_BTN_MIDDLE;
    if(mask) {
      if(e->value)
        button_state |= mask;
      else
        button_state &= ~mask;
    }
    break;
  }
  case EV_SYN:
    if(e->code == SYN_REPORT) {
      mouse_post_event(acc_dx, acc_dy, acc_dwheel, button_state);
      acc_dx     = 0;
      acc_dy     = 0;
      acc_dwheel = 0;
    }
    break;
  default:
    break;
  }
}

static void virtio_input_irq(u8 irq)
{
  (void)irq;
  u8 isr = *vd.isr;
  (void)isr;

  u16  head;
  u32  len;
  bool any = false;
  while(virtio_vq_pop(&evq, &head, &len)) {
    if(len >= sizeof(virtio_input_event_t))
      handle_event(&event_buf[desc_to_slot[head]]);
    refill_descriptor(head);
    any = true;
  }
  if(any)
    virtio_vq_kick(&vd, &evq);
}

bool virtio_input_init(void)
{
  if(!virtio_probe(&vd, VIRTIO_DEVICE_INPUT)) {
    console_print("[virtio-input] no device found\n");
    return false;
  }

  if(!virtio_init_features(&vd, 0)) {
    console_print("[virtio-input] features handshake failed\n");
    return false;
  }

  /* virtio-input exposes queue 0 = eventq (device → driver), queue 1 = statusq.
   */
  if(!virtio_vq_init(&vd, &evq, 0)) {
    console_print("[virtio-input] eventq init failed\n");
    virtio_set_failed(&vd);
    return false;
  }
  /* statusq is required by spec but we never push to it. */
  if(!virtio_vq_init(&vd, &statusq, 1)) {
    console_print("[virtio-input] statusq init failed\n");
    virtio_set_failed(&vd);
    return false;
  }

  /* Resolve physical addresses for each event buffer once — refill happens
   * from the IRQ path which must not walk page tables. */
  for(u32 i = 0; i < EVENT_BUFFERS; i++)
    event_buf_phys[i] = vmm_get_phys((u64)&event_buf[i]);

  for(u32 i = 0; i < EVENT_BUFFERS; i++) {
    i16 d = virtio_vq_alloc_desc(&evq);
    if(d < 0)
      break;
    desc_to_slot[(u16)d] = (u16)i;
    refill_descriptor((u16)d);
  }

  irq_register(vd.pci.irq, virtio_input_irq);
  pic_unmask(vd.pci.irq);

  virtio_set_driver_ok(&vd);
  virtio_vq_kick(&vd, &evq);

  console_printf(
      "[virtio-input] online (IRQ %d, %d event slots)\n", (int)vd.pci.irq,
      (int)EVENT_BUFFERS
  );
  return true;
}
