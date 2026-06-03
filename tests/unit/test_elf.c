#include "test_common.h"
#include <alcor2/proc/elf.h>
#include <alcor2/types.h>
#include <string.h>

void *kmemcpy(void *dest, const void *src, u64 n);

/* Mock implementations */
bool vmm_map_range_alloc(u64 start_page, u64 count, u64 flags) {
    (void)start_page; (void)count; (void)flags;
    return mock_type(bool);
}

u64 vmm_get_phys(u64 vaddr) {
    (void)vaddr;
    return mock_type(u64);
}

u64 vmm_get_hhdm(void) {
    return mock_type(u64);
}

i64 vfs_read(i64 fd, void *buf, u64 count) {
    (void)fd; (void)count;
    void *src = mock_ptr_type(void*);
    i64 ret = mock_type(i64);
    if(src && ret > 0) {
        kmemcpy(buf, src, ret);
    }
    return ret;
}

i64 vfs_seek(i64 fd, i64 offset, i32 whence) {
    (void)fd; (void)offset; (void)whence;
    return 0;
}

void *kmalloc(u64 size) {
    return test_malloc(size);
}

void kfree(void *ptr) {
    if(ptr) test_free(ptr);
}

void *kmemcpy(void *dest, const void *src, u64 n) {
    u8 *d = dest;
    const u8 *s = src;
    for (u64 i = 0; i < n; i++) d[i] = s[i];
    return dest;
}

void console_print(const char *s) {
    (void)s;
}

#include "../../src/kernel/sys/elf.c"

static void test_elf_validate_valid(void **state) {
    (void)state;
    Elf64_Ehdr ehdr = {0};
    ehdr.e_ident[EI_MAG0] = 0x7F;
    ehdr.e_ident[EI_MAG1] = 'E';
    ehdr.e_ident[EI_MAG2] = 'L';
    ehdr.e_ident[EI_MAG3] = 'F';
    ehdr.e_ident[EI_CLASS] = ELFCLASS64;
    ehdr.e_ident[EI_DATA] = ELFDATA2LSB;
    ehdr.e_type = ET_EXEC;
    ehdr.e_machine = EM_X86_64;
    assert_true(elf_validate(&ehdr));
}

static void test_elf_validate_invalid_magic(void **state) {
    (void)state;
    Elf64_Ehdr ehdr = {0};
    ehdr.e_ident[EI_MAG0] = 0x00;
    ehdr.e_ident[EI_MAG1] = 'E';
    ehdr.e_ident[EI_MAG2] = 'L';
    ehdr.e_ident[EI_MAG3] = 'F';
    ehdr.e_ident[EI_CLASS] = ELFCLASS64;
    ehdr.e_ident[EI_DATA] = ELFDATA2LSB;
    ehdr.e_type = ET_EXEC;
    ehdr.e_machine = EM_X86_64;
    assert_false(elf_validate(&ehdr));
}

static void test_elf_validate_invalid_class(void **state) {
    (void)state;
    Elf64_Ehdr ehdr = {0};
    ehdr.e_ident[EI_MAG0] = 0x7F;
    ehdr.e_ident[EI_MAG1] = 'E';
    ehdr.e_ident[EI_MAG2] = 'L';
    ehdr.e_ident[EI_MAG3] = 'F';
    ehdr.e_ident[EI_CLASS] = ELFCLASS32;
    ehdr.e_ident[EI_DATA] = ELFDATA2LSB;
    ehdr.e_type = ET_EXEC;
    ehdr.e_machine = EM_X86_64;
    assert_false(elf_validate(&ehdr));
}

static void test_elf_validate_invalid_data(void **state) {
    (void)state;
    Elf64_Ehdr ehdr = {0};
    ehdr.e_ident[EI_MAG0] = 0x7F;
    ehdr.e_ident[EI_MAG1] = 'E';
    ehdr.e_ident[EI_MAG2] = 'L';
    ehdr.e_ident[EI_MAG3] = 'F';
    ehdr.e_ident[EI_CLASS] = ELFCLASS64;
    ehdr.e_ident[EI_DATA] = ELFDATA2MSB;
    ehdr.e_type = ET_EXEC;
    ehdr.e_machine = EM_X86_64;
    assert_false(elf_validate(&ehdr));
}

static void test_elf_validate_invalid_type(void **state) {
    (void)state;
    Elf64_Ehdr ehdr = {0};
    ehdr.e_ident[EI_MAG0] = 0x7F;
    ehdr.e_ident[EI_MAG1] = 'E';
    ehdr.e_ident[EI_MAG2] = 'L';
    ehdr.e_ident[EI_MAG3] = 'F';
    ehdr.e_ident[EI_CLASS] = ELFCLASS64;
    ehdr.e_ident[EI_DATA] = ELFDATA2LSB;
    ehdr.e_type = ET_REL;
    ehdr.e_machine = EM_X86_64;
    assert_false(elf_validate(&ehdr));
}

static void test_elf_validate_invalid_machine(void **state) {
    (void)state;
    Elf64_Ehdr ehdr = {0};
    ehdr.e_ident[EI_MAG0] = 0x7F;
    ehdr.e_ident[EI_MAG1] = 'E';
    ehdr.e_ident[EI_MAG2] = 'L';
    ehdr.e_ident[EI_MAG3] = 'F';
    ehdr.e_ident[EI_CLASS] = ELFCLASS64;
    ehdr.e_ident[EI_DATA] = ELFDATA2LSB;
    ehdr.e_type = ET_EXEC;
    ehdr.e_machine = 3;
    assert_false(elf_validate(&ehdr));
}

static void test_elf_load_success(void **state) {
    (void)state;
    u8 dummy_bin[1024] = {0};
    Elf64_Ehdr *ehdr = (Elf64_Ehdr *)dummy_bin;
    ehdr->e_ident[EI_MAG0] = 0x7F;
    ehdr->e_ident[EI_MAG1] = 'E';
    ehdr->e_ident[EI_MAG2] = 'L';
    ehdr->e_ident[EI_MAG3] = 'F';
    ehdr->e_ident[EI_CLASS] = ELFCLASS64;
    ehdr->e_ident[EI_DATA] = ELFDATA2LSB;
    ehdr->e_type = ET_EXEC;
    ehdr->e_machine = EM_X86_64;
    ehdr->e_phoff = sizeof(Elf64_Ehdr);
    ehdr->e_phnum = 1;
    ehdr->e_phentsize = sizeof(Elf64_Phdr);
    ehdr->e_entry = 0x400000;

    Elf64_Phdr *phdr = (Elf64_Phdr *)(dummy_bin + sizeof(Elf64_Ehdr));
    phdr->p_type = PT_LOAD;
    phdr->p_memsz = 0x1000;
    phdr->p_filesz = 0x10;
    phdr->p_vaddr = 0x400000;
    phdr->p_offset = sizeof(Elf64_Ehdr) + sizeof(Elf64_Phdr);
    
    dummy_bin[phdr->p_offset] = 0x90;
    
    elf_info_t info = {0};
    
    will_return(vmm_map_range_alloc, true);
    static u8 mock_phys_mem[0x2000];
    will_return(vmm_get_phys, (u64)mock_phys_mem);
    will_return(vmm_get_hhdm, 0);

    int ret = elf_load(dummy_bin, sizeof(dummy_bin), &info);
    assert_int_equal(ret, 0);
    assert_int_equal(info.entry, 0x400000);
    assert_int_equal(info.base, 0x400000);
    assert_int_equal(mock_phys_mem[0], 0x90);
}

static void test_elf_load_fd_success(void **state) {
    (void)state;
    u8 dummy_bin[1024] = {0};
    Elf64_Ehdr *ehdr = (Elf64_Ehdr *)dummy_bin;
    ehdr->e_ident[EI_MAG0] = 0x7F;
    ehdr->e_ident[EI_MAG1] = 'E';
    ehdr->e_ident[EI_MAG2] = 'L';
    ehdr->e_ident[EI_MAG3] = 'F';
    ehdr->e_ident[EI_CLASS] = ELFCLASS64;
    ehdr->e_ident[EI_DATA] = ELFDATA2LSB;
    ehdr->e_type = ET_EXEC;
    ehdr->e_machine = EM_X86_64;
    ehdr->e_phoff = sizeof(Elf64_Ehdr);
    ehdr->e_phnum = 1;
    ehdr->e_phentsize = sizeof(Elf64_Phdr);
    ehdr->e_entry = 0x400000;

    Elf64_Phdr *phdr = (Elf64_Phdr *)(dummy_bin + sizeof(Elf64_Ehdr));
    phdr->p_type = PT_LOAD;
    phdr->p_memsz = 0x1000;
    phdr->p_filesz = 0x10;
    phdr->p_vaddr = 0x400000;
    phdr->p_offset = sizeof(Elf64_Ehdr) + sizeof(Elf64_Phdr);
    dummy_bin[phdr->p_offset] = 0x90;
    
    elf_info_t info = {0};
    
    will_return(vfs_read, dummy_bin);
    will_return(vfs_read, sizeof(Elf64_Ehdr));
    
    will_return(vfs_read, dummy_bin + sizeof(Elf64_Ehdr));
    will_return(vfs_read, sizeof(Elf64_Phdr));
    
    will_return(vmm_map_range_alloc, true);
    
    will_return(vfs_read, dummy_bin + phdr->p_offset);
    will_return(vfs_read, 0x10);
    
    static u8 mock_phys_mem[0x2000];
    will_return(vmm_get_phys, (u64)mock_phys_mem);
    will_return(vmm_get_hhdm, 0);
    
    int ret = elf_load_fd(123, &info);
    assert_int_equal(ret, 0);
    assert_int_equal(info.entry, 0x400000);
    assert_int_equal(info.base, 0x400000);
    assert_int_equal(mock_phys_mem[0], 0x90);
}

static void test_elf_load_small_file(void **state) {
    (void)state;
    elf_info_t info = {0};
    u8 small_bin[10] = {0};
    int ret = elf_load(small_bin, sizeof(small_bin), &info);
    assert_int_equal(ret, -1);
}

static void test_elf_load_no_phdr(void **state) {
    (void)state;
    u8 dummy_bin[1024] = {0};
    Elf64_Ehdr *ehdr = (Elf64_Ehdr *)dummy_bin;
    ehdr->e_ident[EI_MAG0] = 0x7F;
    ehdr->e_ident[EI_MAG1] = 'E';
    ehdr->e_ident[EI_MAG2] = 'L';
    ehdr->e_ident[EI_MAG3] = 'F';
    ehdr->e_ident[EI_CLASS] = ELFCLASS64;
    ehdr->e_ident[EI_DATA] = ELFDATA2LSB;
    ehdr->e_type = ET_EXEC;
    ehdr->e_machine = EM_X86_64;
    ehdr->e_phoff = 0; // No program headers
    
    elf_info_t info = {0};
    int ret = elf_load(dummy_bin, sizeof(dummy_bin), &info);
    assert_int_equal(ret, -1);
}

static void test_elf_load_no_loadable(void **state) {
    (void)state;
    u8 dummy_bin[1024] = {0};
    Elf64_Ehdr *ehdr = (Elf64_Ehdr *)dummy_bin;
    ehdr->e_ident[EI_MAG0] = 0x7F;
    ehdr->e_ident[EI_MAG1] = 'E';
    ehdr->e_ident[EI_MAG2] = 'L';
    ehdr->e_ident[EI_MAG3] = 'F';
    ehdr->e_ident[EI_CLASS] = ELFCLASS64;
    ehdr->e_ident[EI_DATA] = ELFDATA2LSB;
    ehdr->e_type = ET_EXEC;
    ehdr->e_machine = EM_X86_64;
    ehdr->e_phoff = sizeof(Elf64_Ehdr);
    ehdr->e_phnum = 1;
    
    Elf64_Phdr *phdr = (Elf64_Phdr *)(dummy_bin + sizeof(Elf64_Ehdr));
    phdr->p_type = PT_NOTE; // Not PT_LOAD
    
    elf_info_t info = {0};
    int ret = elf_load(dummy_bin, sizeof(dummy_bin), &info);
    assert_int_equal(ret, -1);
}

static void test_elf_load_fd_read_fail(void **state) {
    (void)state;
    elf_info_t info = {0};
    will_return(vfs_read, NULL);
    will_return(vfs_read, 0);
    int ret = elf_load_fd(123, &info);
    assert_int_equal(ret, -1);
}

static void test_elf_load_fd_no_loadable(void **state) {
    (void)state;
    u8 dummy_bin[1024] = {0};
    Elf64_Ehdr *ehdr = (Elf64_Ehdr *)dummy_bin;
    ehdr->e_ident[EI_MAG0] = 0x7F;
    ehdr->e_ident[EI_MAG1] = 'E';
    ehdr->e_ident[EI_MAG2] = 'L';
    ehdr->e_ident[EI_MAG3] = 'F';
    ehdr->e_ident[EI_CLASS] = ELFCLASS64;
    ehdr->e_ident[EI_DATA] = ELFDATA2LSB;
    ehdr->e_type = ET_EXEC;
    ehdr->e_machine = EM_X86_64;
    ehdr->e_phoff = sizeof(Elf64_Ehdr);
    ehdr->e_phnum = 1;
    
    Elf64_Phdr *phdr = (Elf64_Phdr *)(dummy_bin + sizeof(Elf64_Ehdr));
    phdr->p_type = PT_NOTE; 
    
    elf_info_t info = {0};
    
    will_return(vfs_read, dummy_bin);
    will_return(vfs_read, sizeof(Elf64_Ehdr));
    
    will_return(vfs_read, dummy_bin + sizeof(Elf64_Ehdr));
    will_return(vfs_read, sizeof(Elf64_Phdr));
    
    int ret = elf_load_fd(123, &info);
    assert_int_equal(ret, -1);
}

/* Helper: build a minimal valid ELF header with one PT_LOAD segment */
static void build_elf(u8 *buf, u64 bufsz, Elf64_Phdr *phdr_out)
{
  memset(buf, 0, bufsz);
  Elf64_Ehdr *ehdr = (Elf64_Ehdr *)buf;
  ehdr->e_ident[EI_MAG0]  = 0x7F;
  ehdr->e_ident[EI_MAG1]  = 'E';
  ehdr->e_ident[EI_MAG2]  = 'L';
  ehdr->e_ident[EI_MAG3]  = 'F';
  ehdr->e_ident[EI_CLASS] = ELFCLASS64;
  ehdr->e_ident[EI_DATA]  = ELFDATA2LSB;
  ehdr->e_type            = ET_EXEC;
  ehdr->e_machine         = EM_X86_64;
  ehdr->e_phoff           = sizeof(Elf64_Ehdr);
  ehdr->e_phnum           = 1;
  ehdr->e_phentsize       = sizeof(Elf64_Phdr);
  ehdr->e_entry           = 0x400000;

  Elf64_Phdr *phdr = (Elf64_Phdr *)(buf + sizeof(Elf64_Ehdr));
  phdr->p_type    = PT_LOAD;
  phdr->p_memsz   = 0x1000;
  phdr->p_filesz  = 4;
  phdr->p_vaddr   = 0x400000;
  phdr->p_offset  = sizeof(Elf64_Ehdr) + sizeof(Elf64_Phdr);
  if(phdr_out) *phdr_out = *phdr;
}

/* elf_map_segment_pages failure → elf_load returns -1 */
static void test_elf_load_map_fail(void **state) {
  (void)state;
  u8 buf[1024];
  build_elf(buf, sizeof(buf), NULL);
  elf_info_t info = {0};
  will_return(vmm_map_range_alloc, false); /* allocation fails */
  int ret = elf_load(buf, sizeof(buf), &info);
  assert_int_equal(ret, -1);
}

/* phdr->p_memsz == 0: segment is skipped */
static void test_elf_load_skip_zero_memsz(void **state) {
  (void)state;
  u8 buf[1024] = {0};
  Elf64_Ehdr *ehdr = (Elf64_Ehdr *)buf;
  ehdr->e_ident[EI_MAG0]  = 0x7F;
  ehdr->e_ident[EI_MAG1]  = 'E';
  ehdr->e_ident[EI_MAG2]  = 'L';
  ehdr->e_ident[EI_MAG3]  = 'F';
  ehdr->e_ident[EI_CLASS] = ELFCLASS64;
  ehdr->e_ident[EI_DATA]  = ELFDATA2LSB;
  ehdr->e_type            = ET_EXEC;
  ehdr->e_machine         = EM_X86_64;
  ehdr->e_phoff           = sizeof(Elf64_Ehdr);
  ehdr->e_phnum           = 2;
  ehdr->e_phentsize       = sizeof(Elf64_Phdr);
  ehdr->e_entry           = 0x400000;

  Elf64_Phdr *ph = (Elf64_Phdr *)(buf + sizeof(Elf64_Ehdr));
  /* First segment: memsz=0 → skipped */
  ph[0].p_type   = PT_LOAD;
  ph[0].p_memsz  = 0;
  ph[0].p_filesz = 0;
  ph[0].p_vaddr  = 0x400000;
  /* Second segment: valid */
  ph[1].p_type   = PT_LOAD;
  ph[1].p_memsz  = 0x1000;
  ph[1].p_filesz = 4;
  ph[1].p_vaddr  = 0x401000;
  ph[1].p_offset = sizeof(Elf64_Ehdr) + 2 * sizeof(Elf64_Phdr);

  elf_info_t info = {0};
  will_return(vmm_map_range_alloc, true);
  static u8 phys_mem[0x2000];
  will_return(vmm_get_phys, (u64)phys_mem);
  will_return(vmm_get_hhdm, 0);
  int ret = elf_load(buf, sizeof(buf), &info);
  assert_int_equal(ret, 0);
}

/* elf_copy_to_mapped: data spanning two pages forces second vmm_get_phys */
static void test_elf_copy_multi_page(void **state) {
  (void)state;
  u8 buf[1024];
  Elf64_Phdr ph;
  build_elf(buf, sizeof(buf), &ph);

  /* Make filesz span across a page boundary: set vaddr near page end */
  Elf64_Ehdr *ehdr = (Elf64_Ehdr *)buf;
  Elf64_Phdr *phdr = (Elf64_Phdr *)(buf + sizeof(Elf64_Ehdr));
  phdr->p_vaddr   = 0x400FF0; /* 16 bytes before page end */
  phdr->p_memsz   = 0x1000;
  phdr->p_filesz  = 0x20;     /* 32 bytes → crosses page boundary */

  elf_info_t info = {0};
  will_return(vmm_map_range_alloc, true);
  static u8 p0[4096], p1[4096];
  /* First page */
  will_return(vmm_get_phys, (u64)p0);
  will_return(vmm_get_hhdm, 0);
  /* Second page */
  will_return(vmm_get_phys, (u64)p1);
  will_return(vmm_get_hhdm, 0);
  int ret = elf_load(buf, sizeof(buf), &info);
  assert_int_equal(ret, 0);
}

int main(void) {
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_elf_validate_valid),
        cmocka_unit_test(test_elf_validate_invalid_magic),
        cmocka_unit_test(test_elf_validate_invalid_class),
        cmocka_unit_test(test_elf_validate_invalid_data),
        cmocka_unit_test(test_elf_validate_invalid_type),
        cmocka_unit_test(test_elf_validate_invalid_machine),
        cmocka_unit_test(test_elf_load_success),
        cmocka_unit_test(test_elf_load_small_file),
        cmocka_unit_test(test_elf_load_no_phdr),
        cmocka_unit_test(test_elf_load_no_loadable),
        cmocka_unit_test(test_elf_load_fd_success),
        cmocka_unit_test(test_elf_load_fd_read_fail),
        cmocka_unit_test(test_elf_load_fd_no_loadable),
        cmocka_unit_test(test_elf_load_map_fail),
        cmocka_unit_test(test_elf_load_skip_zero_memsz),
        cmocka_unit_test(test_elf_copy_multi_page),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
