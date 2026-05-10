/**
 * @file src/kernel/sys/sys_fb.c
 * @brief Syscalls: framebuffer info + mmap (userspace drawing / font stacks).
 */

#include <alcor2/alcor_fb.h>
#include <alcor2/drivers/fb_user.h>
#include <alcor2/errno.h>
#include <alcor2/kstdlib.h>
#include <alcor2/mm/memory_layout.h>
#include <alcor2/mm/pmm.h>
#include <alcor2/mm/vmm.h>
#include <alcor2/proc/proc.h>
#include <alcor2/sys/internal.h>

static inline bool user_rw_ok(u64 ptr, u64 size)
{
  return ptr && vmm_is_user_range((void *)ptr, size);
}

u64 sys_alcor_fb_info(u64 user_info, u64 a2, u64 a3, u64 a4, u64 a5, u64 a6)
{
  (void)a2;
  (void)a3;
  (void)a4;
  (void)a5;
  (void)a6;

  if(!user_rw_ok(user_info, sizeof(alcor_fb_info_t)))
    return (u64)-EFAULT;
  if(!fb_user_ready())
    return (u64)-ENODEV;

  alcor_fb_info_t st;
  fb_user_fill_info(&st);
  kmemcpy((void *)user_info, &st, sizeof(st));
  return 0;
}

u64 sys_alcor_fb_mmap(u64 hint, u64 size_req, u64 a3, u64 a4, u64 a5, u64 a6)
{
  (void)a3;
  (void)a4;
  (void)a5;
  (void)a6;

  if(!fb_user_ready())
    return (u64)-ENODEV;

  proc_t *p = proc_current();
  if(!p)
    return (u64)-ENOMEM;

  u64 map_sz   = fb_user_map_size();
  u64 px_off   = fb_user_mmap_pixel_offset();
  u64 phys0    = fb_user_phys_base();
  u64 n_pages  = map_sz / PAGE_SIZE;

  if(size_req != 0 && size_req != map_sz)
    return (u64)-EINVAL;

  u64 base = (hint != 0) ? (hint & ~(u64)(PAGE_SIZE - 1))
                         : (p->mmap_base & ~(u64)(PAGE_SIZE - 1));
  u64 end  = base + map_sz;
  if(end < base || end > USER_SPACE_END)
    return (u64)-ENOMEM;

  if(hint == 0)
    p->mmap_base = end;

  u64 flags = VMM_PRESENT | VMM_WRITE | VMM_USER;

  for(u64 i = 0; i < n_pages; i++) {
    vmm_map_in(p->cr3, base + i * PAGE_SIZE, phys0 + i * PAGE_SIZE, flags);
  }

  return base + px_off;
}
