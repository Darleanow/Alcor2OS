#include "test_common.h"

#include <alcor2/mm/memory_layout.h>
#include <alcor2/proc/elf.h>
#include <alcor2/types.h>

#include <string.h>

void console_print(const char *s)
{
  (void)s;
}
void *kmalloc(u64 n)
{
  (void)n;
  return NULL;
}
void kfree(void *p)
{
  (void)p;
}
void *kmemcpy(void *d, const void *s, u64 n)
{
  return memcpy(d, s, n);
}
void kzero(void *d, u64 n)
{
  memset(d, 0, n);
}

bool vmm_map_range_alloc(u64 v, u64 c, u64 f)
{
  (void)v;
  (void)c;
  (void)f;
  return true;
}
u64 vmm_get_phys(u64 v)
{
  return v;
}
u64 vmm_get_hhdm(void)
{
  return 0;
}
i64 vfs_read(i64 fd, void *b, u64 n)
{
  (void)fd;
  (void)b;
  (void)n;
  return 0;
}
i64 vfs_seek(i64 fd, i64 off, int w)
{
  (void)fd;
  (void)off;
  (void)w;
  return 0;
}

#include "../../src/kernel/sys/elf.c"

static Elf64_Ehdr make_valid_ehdr(void)
{
  Elf64_Ehdr h;
  memset(&h, 0, sizeof(h));
  h.e_ident[EI_MAG0]  = 0x7F;
  h.e_ident[EI_MAG1]  = 'E';
  h.e_ident[EI_MAG2]  = 'L';
  h.e_ident[EI_MAG3]  = 'F';
  h.e_ident[EI_CLASS] = ELFCLASS64;
  h.e_ident[EI_DATA]  = ELFDATA2LSB;
  h.e_type            = ET_EXEC;
  h.e_machine         = EM_X86_64;
  h.e_entry           = 0x400000;
  return h;
}

static void valid_header_passes(void **state)
{
  (void)state;
  Elf64_Ehdr h = make_valid_ehdr();
  assert_true(elf_validate(&h));
}

static void dyn_type_passes(void **state)
{
  (void)state;
  Elf64_Ehdr h = make_valid_ehdr();
  h.e_type     = ET_DYN;
  assert_true(elf_validate(&h));
}

static void bad_magic_fails(void **state)
{
  (void)state;
  Elf64_Ehdr h       = make_valid_ehdr();
  h.e_ident[EI_MAG0] = 0x00;
  assert_false(elf_validate(&h));
}

static void bad_magic_e_fails(void **state)
{
  (void)state;
  Elf64_Ehdr h       = make_valid_ehdr();
  h.e_ident[EI_MAG1] = 'X';
  assert_false(elf_validate(&h));
}

static void not_64bit_fails(void **state)
{
  (void)state;
  Elf64_Ehdr h        = make_valid_ehdr();
  h.e_ident[EI_CLASS] = 1; /* ELFCLASS32 */
  assert_false(elf_validate(&h));
}

static void big_endian_fails(void **state)
{
  (void)state;
  Elf64_Ehdr h       = make_valid_ehdr();
  h.e_ident[EI_DATA] = 2; /* ELFDATA2MSB */
  assert_false(elf_validate(&h));
}

static void non_exec_type_fails(void **state)
{
  (void)state;
  Elf64_Ehdr h = make_valid_ehdr();
  h.e_type     = 1; /* ET_REL */
  assert_false(elf_validate(&h));
}

static void wrong_machine_fails(void **state)
{
  (void)state;
  Elf64_Ehdr h = make_valid_ehdr();
  h.e_machine  = 3; /* EM_386 */
  assert_false(elf_validate(&h));
}

static void elf_info_init_sets_sentinel(void **state)
{
  (void)state;
  Elf64_Ehdr h  = make_valid_ehdr();
  h.e_phentsize = sizeof(Elf64_Phdr);
  h.e_phnum     = 2;
  h.e_phoff     = sizeof(Elf64_Ehdr);
  elf_info_t info;
  elf_info_init(&h, &info);
  assert_int_equal(info.entry, 0x400000);
  assert_int_equal(info.base, ELF_BASE_SENTINEL);
  assert_int_equal(info.end, 0);
  assert_int_equal(info.phnum, 2);
}

static void elf_info_track_updates_range(void **state)
{
  (void)state;
  Elf64_Ehdr h  = make_valid_ehdr();
  h.e_phentsize = sizeof(Elf64_Phdr);
  h.e_phnum     = 1;
  h.e_phoff     = sizeof(Elf64_Ehdr);
  elf_info_t info;
  elf_info_init(&h, &info);

  Elf64_Phdr phdr;
  memset(&phdr, 0, sizeof(phdr));
  phdr.p_type  = PT_LOAD;
  phdr.p_vaddr = 0x400000;
  phdr.p_memsz = 0x1000;
  elf_info_track_segment(&h, &phdr, &info);
  assert_int_equal(info.base, 0x400000);
  assert_int_equal(info.end, 0x401000);
}

static void elf_load_too_small_fails(void **state)
{
  (void)state;
  u8         buf[4] = {0x7F, 'E', 'L', 'F'};
  elf_info_t info;
  assert_int_equal(elf_load(buf, 4, &info), -1);
}

static void elf_load_bad_magic_fails(void **state)
{
  (void)state;
  Elf64_Ehdr h       = make_valid_ehdr();
  h.e_ident[EI_MAG0] = 0;
  elf_info_t info;
  assert_int_equal(elf_load(&h, sizeof(h), &info), -1);
}

static void elf_load_no_phdrs_fails(void **state)
{
  (void)state;
  Elf64_Ehdr h = make_valid_ehdr();
  h.e_phnum    = 0;
  elf_info_t info;
  assert_int_equal(elf_load(&h, sizeof(h), &info), -1);
}

static void elf_load_valid_single_segment(void **state)
{
  (void)state;
  /* Build a minimal ELF: header + one PT_LOAD phdr inline. */
  typedef struct __attribute__((packed))
  {
    Elf64_Ehdr ehdr;
    Elf64_Phdr phdr;
    u8         data[16];
  } mini_elf_t;

  mini_elf_t elf;
  memset(&elf, 0, sizeof(elf));

  elf.ehdr             = make_valid_ehdr();
  elf.ehdr.e_phoff     = sizeof(Elf64_Ehdr);
  elf.ehdr.e_phentsize = sizeof(Elf64_Phdr);
  elf.ehdr.e_phnum     = 1;

  elf.phdr.p_type   = PT_LOAD;
  elf.phdr.p_offset = sizeof(Elf64_Ehdr) + sizeof(Elf64_Phdr);
  elf.phdr.p_vaddr  = 0x400000;
  elf.phdr.p_memsz  = 16;
  elf.phdr.p_filesz = 0; /* skip copy — vmm_get_phys stub points to host addr */

  elf_info_t info;
  int        ret = elf_load(&elf, sizeof(elf), &info);
  assert_int_equal(ret, 0);
  assert_int_equal(info.entry, 0x400000);
  assert_int_equal(info.base, 0x400000);
  assert_int_equal(info.end, 0x400000 + 16);
}

static void elf_load_skips_non_load_segments(void **state)
{
  (void)state;
  typedef struct __attribute__((packed))
  {
    Elf64_Ehdr ehdr;
    Elf64_Phdr phdr_interp;
    Elf64_Phdr phdr_load;
  } two_elf_t;

  two_elf_t elf;
  memset(&elf, 0, sizeof(elf));

  elf.ehdr             = make_valid_ehdr();
  elf.ehdr.e_phoff     = sizeof(Elf64_Ehdr);
  elf.ehdr.e_phentsize = sizeof(Elf64_Phdr);
  elf.ehdr.e_phnum     = 2;

  elf.phdr_interp.p_type  = 3; /* PT_INTERP — not PT_LOAD */
  elf.phdr_interp.p_memsz = 16;

  elf.phdr_load.p_type   = PT_LOAD;
  elf.phdr_load.p_vaddr  = 0x401000;
  elf.phdr_load.p_memsz  = 32;
  elf.phdr_load.p_filesz = 0; /* no file data */

  elf_info_t info;
  assert_int_equal(elf_load(&elf, sizeof(elf), &info), 0);
  assert_int_equal(info.base, 0x401000);
}

static void elf_load_zero_memsz_skipped(void **state)
{
  (void)state;
  typedef struct __attribute__((packed))
  {
    Elf64_Ehdr ehdr;
    Elf64_Phdr phdr_zero;
    Elf64_Phdr phdr_real;
  } zero_elf_t;

  zero_elf_t elf;
  memset(&elf, 0, sizeof(elf));

  elf.ehdr             = make_valid_ehdr();
  elf.ehdr.e_phoff     = sizeof(Elf64_Ehdr);
  elf.ehdr.e_phentsize = sizeof(Elf64_Phdr);
  elf.ehdr.e_phnum     = 2;

  elf.phdr_zero.p_type  = PT_LOAD;
  elf.phdr_zero.p_memsz = 0; /* must be skipped */

  elf.phdr_real.p_type  = PT_LOAD;
  elf.phdr_real.p_vaddr = 0x402000;
  elf.phdr_real.p_memsz = 8;

  elf_info_t info;
  assert_int_equal(elf_load(&elf, sizeof(elf), &info), 0);
  assert_int_equal(info.base, 0x402000);
}

static void elf_info_track_phdr_with_zero_offset_sets_phdr(void **state)
{
  (void)state;
  Elf64_Ehdr h  = make_valid_ehdr();
  h.e_phoff     = 64;
  h.e_phentsize = sizeof(Elf64_Phdr);
  h.e_phnum     = 1;

  elf_info_t info;
  elf_info_init(&h, &info);

  Elf64_Phdr phdr;
  memset(&phdr, 0, sizeof(phdr));
  phdr.p_type   = PT_LOAD;
  phdr.p_vaddr  = 0x400000;
  phdr.p_memsz  = 0x1000;
  phdr.p_offset = 0; /* zero offset → phdr field should be set */

  elf_info_track_segment(&h, &phdr, &info);
  assert_int_equal(info.phdr, 0x400000 + h.e_phoff);
}

int main(void)
{
  const struct CMUnitTest tests[] = {
      cmocka_unit_test(valid_header_passes),
      cmocka_unit_test(dyn_type_passes),
      cmocka_unit_test(bad_magic_fails),
      cmocka_unit_test(bad_magic_e_fails),
      cmocka_unit_test(not_64bit_fails),
      cmocka_unit_test(big_endian_fails),
      cmocka_unit_test(non_exec_type_fails),
      cmocka_unit_test(wrong_machine_fails),
      cmocka_unit_test(elf_info_init_sets_sentinel),
      cmocka_unit_test(elf_info_track_updates_range),
      cmocka_unit_test(elf_load_too_small_fails),
      cmocka_unit_test(elf_load_bad_magic_fails),
      cmocka_unit_test(elf_load_no_phdrs_fails),
      cmocka_unit_test(elf_load_valid_single_segment),
      cmocka_unit_test(elf_load_skips_non_load_segments),
      cmocka_unit_test(elf_load_zero_memsz_skipped),
      cmocka_unit_test(elf_info_track_phdr_with_zero_offset_sets_phdr),
  };
  return cmocka_run_group_tests(tests, NULL, NULL);
}
