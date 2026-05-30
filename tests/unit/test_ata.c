#include "test_common.h"
#include <alcor2/types.h>
#include <string.h>

// Prevent inline functions from headers by mocking them out entirely
#define ALCOR2_IO_H
#include <alcor2/arch/io.h>
#define ALCOR2_VMM_H
#define ALCOR2_PCI_H

u8 inb(u16 p);
void outb(u16 p, u8 v);
u16 inw(u16 p);
void outw(u16 p, u16 v);
u32 inl(u16 p);
void outl(u16 p, u32 v);
void io_wait(void);

void *phys_to_virt(u64 phys);
u64 vmm_get_hhdm(void);

typedef struct {
    u16 vendor_id;
    u16 device_id;
    u32 bar[6];
} pci_device_t;
#define PCI_CLASS_STORAGE 0x01
#define PCI_SUBCLASS_IDE 0x01
bool pci_find_device(u8 class_code, u8 subclass, pci_device_t *dev);
void pci_enable_bus_master(const pci_device_t *dev);

static u8 ata_status = 0x40; // READY
static bool expect_identify = false;

u8 inb(u16 p) {
    if (p == 0x1F7 || p == 0x3F6) return ata_status;
    if (p == 0x1F4 || p == 0x1F5) return 0; // Not ATAPI
    if (p == 0x177 || p == 0x376) return 0xFF; // Secondary missing
    return 0;
}

void outb(u16 p, u8 v) {
    if (p == 0x1F7) {
        if (v == 0xEC) { // IDENTIFY
            ata_status = 0x58; // READY | DRQ
            expect_identify = true;
        } else {
            ata_status = 0x48; // READY | DRQ
        }
    }
}

u16 inw(u16 p) {
    if (p == 0x1F0 && expect_identify) {
        static int identify_word = 0;
        u16 val = 0;
        if (identify_word == 49) val = (1 << 8); // DMA
        else if (identify_word == 83) val = (1 << 10); // LBA48
        else if (identify_word == 60) val = 1000; // sectors (low)
        else if (identify_word == 61) val = 0; // sectors (high)
        else if (identify_word == 100) val = 1000;
        else if (identify_word == 101) val = 0;
        else if (identify_word == 102) val = 0;
        else if (identify_word == 103) val = 0;
        
        identify_word++;
        if (identify_word == 256) {
            identify_word = 0;
            expect_identify = false;
            ata_status = 0x40;
        }
        return val;
    }
    return 0;
}

void outw(u16 p, u16 v) { (void)p; (void)v; }
u16 insw(u16 p, void* buf, u32 cnt) { (void)p; (void)buf; (void)cnt; return 0; }
void outsw(u16 p, const void* buf, u32 cnt) { (void)p; (void)buf; (void)cnt; }
u32 inl(u16 p) { (void)p; return 0; }
void outl(u16 p, u32 v) { (void)p; (void)v; }
u32 insl(u16 p, void* buf, u32 cnt) { (void)p; (void)buf; (void)cnt; return 0; }
void outsl(u16 p, const void* buf, u32 cnt) { (void)p; (void)buf; (void)cnt; }
void io_wait(void) {}

void *phys_to_virt(u64 p) { return (void*)(uintptr_t)p; }
u64 vmm_get_hhdm(void) { return 0; }
bool pci_find_device(u8 c, u8 s, pci_device_t *o) { (void)c; (void)s; (void)o; return false; }
void pci_enable_bus_master(const pci_device_t *d) { (void)d; }

void pit_sleep_ms(u32 ms) { (void)ms; }
void* kmalloc(u64 size) { (void)size; return NULL; }
void kfree(void* ptr) { (void)ptr; }
void* kmemcpy(void* d, const void* s, u64 n) { return memcpy(d, s, n); }

// Mocks for ata.c
void console_print(const char *s) { (void)s; }
void console_printf(const char *fmt, ...) { (void)fmt; }
void cpu_pause(void) {}
void cpu_disable_interrupts(void) {}
void cpu_enable_interrupts(void) {}
struct proc;
struct proc *proc_current(void) { return NULL; }
void proc_block(struct proc *p) { (void)p; }
void proc_wake(struct proc *p) { (void)p; }
void proc_schedule(void) {}
u64 pit_get_ticks(void) { return 0; }
void pic_unmask(u8 irq) { (void)irq; }
void irq_register(u8 irq, void (*h)(u8)) { (void)irq; (void)h; }
void *pmm_alloc(void) { static char buf[4096]; return buf; }
void *pmm_alloc_pages(u64 n) { (void)n; static char buf[65536]; return buf; }
void pmm_free(void *p) { (void)p; }
void pmm_free_pages(void *p, u64 n) { (void)p; (void)n; }

#include "../../src/drivers/ata/ata.c"

static void test_ata_init(void **state) {
    (void)state;
    ata_init();
    ata_drive_t *d = ata_get_drive(0);
    assert_non_null(d);
    assert_true(d->present);
    assert_int_equal(d->sectors, 1000);
}

static void test_ata_read(void **state) {
    (void)state;
    u8 buf[512] = {0};
    i64 res = ata_read(0, 0, 1, buf);
    assert_int_equal(res, 0);
}

static void test_ata_write(void **state) {
    (void)state;
    u8 buf[512] = {0};
    i64 res = ata_write(0, 0, 1, buf);
    assert_int_equal(res, 0);
}

int main(void) {
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_ata_init),
        cmocka_unit_test(test_ata_read),
        cmocka_unit_test(test_ata_write),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
