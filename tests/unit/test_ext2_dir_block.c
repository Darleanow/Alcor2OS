#include "test_common.h"
#include <alcor2/types.h>
#include <alcor2/errno.h>

#include <stdlib.h>
#include <string.h>

#include <alcor2/fs/ext2.h>
#include <fs/ext2/internal.h>

void *kmalloc(u64 n) { return malloc((size_t)n); }
void  kfree(void *p) { free(p); }
void *kmemcpy(void *d, const void *s, u64 n) { return memcpy(d, s, n); }
void  kzero(void *d, u64 n) { memset(d, 0, (size_t)n); }
int   kstrncmp(const char *a, const char *b, u64 n) { return strncmp(a, b, (size_t)n); }
u64   kstrlen(const char *s) { return strlen(s); }
char *kstrncpy(char *d, const char *s, u64 m) { if(!m) return d; u64 i; for(i=0;i<m-1&&s[i];i++) d[i]=s[i]; d[i]='\0'; return d; }

u8   *cache_get_block(u32 s) { (void)s; return NULL; }
void  cache_put_block(u8 *p) { (void)p; }
i64   vol_read_block(const ext2_volume_t *v, u32 b, void *buf) { (void)v;(void)b;(void)buf; return -1; }
i64   vol_write_block(const ext2_volume_t *v, u32 b, const void *buf) { (void)v;(void)b;(void)buf; return -1; }
u32   get_block_num(const ext2_volume_t *v, const ext2_inode_t *in, u32 fb) { (void)v;(void)in;(void)fb; return 0; }
u32   alloc_file_block(ext2_volume_t *v, ext2_inode_t *in, u32 fb, u32 g) { (void)v;(void)in;(void)fb;(void)g; return 0; }
i64   flush_metadata(ext2_volume_t *v) { (void)v; return -1; }
i64   read_inode(const ext2_volume_t *v, u32 i, ext2_inode_t *n) { (void)v;(void)i;(void)n; return -1; }
i64   write_inode(const ext2_volume_t *v, u32 i, const ext2_inode_t *n) { (void)v;(void)i;(void)n; return -1; }

#include "../../src/fs/ext2/dir_entry.c"

#define BSIZ 64u

static void put_entry(u8 *buf, u32 off, u32 ino, u16 rec_len, const char *name) {
  ext2_dirent_t *de = (ext2_dirent_t *)(buf + off);
  de->inode    = ino;
  de->rec_len  = rec_len;
  de->name_len = (u8)strlen(name);
  de->file_type = 1;
  memcpy(de->name, name, de->name_len);
}

static void aligned_len_exact_multiple_unchanged(void **state) {
  (void)state;
  assert_int_equal(dirent_aligned_len(4), 12);
}

static void aligned_len_includes_header(void **state) {
  (void)state;
  assert_int_equal(dirent_aligned_len(0), 8);
}

static void aligned_len_rounds_to_four(void **state) {
  (void)state;
  assert_int_equal(dirent_aligned_len(1), 12);
}

static void count_dot_and_dotdot_excluded(void **state) {
  (void)state;
  u8 buf[BSIZ] = {0};
  put_entry(buf, 0, 1, 12, ".");
  put_entry(buf, 12, 2, 52, "..");
  assert_int_equal(dir_block_count_entries(buf, BSIZ), 0);
}

static void count_dot_prefix_not_excluded(void **state) {
  (void)state;
  u8 buf[BSIZ] = {0};
  put_entry(buf, 0, 1, 64, "..hidden");
  assert_int_equal(dir_block_count_entries(buf, BSIZ), 1);
}

static void count_skips_tombstone(void **state) {
  (void)state;
  u8 buf[BSIZ] = {0};
  put_entry(buf, 0, 0, 64, "foo");
  assert_int_equal(dir_block_count_entries(buf, BSIZ), 0);
}

static void count_stops_at_zero_rec_len(void **state) {
  (void)state;
  u8 buf[BSIZ] = {0};
  put_entry(buf, 0, 1, 0, "foo");
  put_entry(buf, 12, 2, 52, "bar");
  assert_int_equal(dir_block_count_entries(buf, BSIZ), 0);
}

static void find_name_longer_query_not_matched(void **state) {
  (void)state;
  u8 buf[BSIZ] = {0};
  put_entry(buf, 0, 1, 64, "foo");
  assert_null(dir_block_find_name(buf, BSIZ, "foobar", 6));
}

static void find_name_prefix_not_matched(void **state) {
  (void)state;
  u8 buf[BSIZ] = {0};
  put_entry(buf, 0, 1, 64, "foobar");
  assert_null(dir_block_find_name(buf, BSIZ, "foo", 3));
}

static void find_name_returns_correct_ptr(void **state) {
  (void)state;
  u8 buf[BSIZ] = {0};
  put_entry(buf, 0, 1, 12, "foo");
  put_entry(buf, 12, 2, 52, "bar");
  assert_ptr_equal(dir_block_find_name(buf, BSIZ, "bar", 3), buf + 12);
}

static void find_name_skips_tombstone(void **state) {
  (void)state;
  u8 buf[BSIZ] = {0};
  put_entry(buf, 0, 0, 64, "foo");
  assert_null(dir_block_find_name(buf, BSIZ, "foo", 3));
}

static void find_name_stops_at_zero_rec_len(void **state) {
  (void)state;
  u8 buf[BSIZ] = {0};
  put_entry(buf, 0, 1, 0, "foo");
  assert_null(dir_block_find_name(buf, BSIZ, "foo", 3));
}

static void init_first_entry_rec_len_is_block_size(void **state) {
  (void)state;
  u8 buf[BSIZ] = {0xFF};
  dir_init_first_entry(buf, BSIZ, "foo", 3, 1, 1);
  ext2_dirent_t *de = (ext2_dirent_t *)buf;
  assert_int_equal(de->rec_len, BSIZ);
}

static void init_first_entry_sets_correct_fields(void **state) {
  (void)state;
  u8 buf[BSIZ] = {0};
  dir_init_first_entry(buf, BSIZ, "foo", 3, 5, 2);
  ext2_dirent_t *de = (ext2_dirent_t *)buf;
  assert_int_equal(de->inode, 5);
  assert_int_equal(de->name_len, 3);
  assert_int_equal(de->file_type, 2);
  assert_memory_equal(de->name, "foo", 3);
}

static void insert_into_slack_at_end(void **state) {
  (void)state;
  u8 buf[BSIZ] = {0};
  put_entry(buf, 0, 1, BSIZ, "foo"); // actual size 12
  assert_true(dir_block_try_insert(buf, BSIZ, "bar", 3, 2, 1));
}

static void insert_returns_false_when_no_slack(void **state) {
  (void)state;
  u8 buf[BSIZ] = {0};
  put_entry(buf, 0, 1, 12, "foo");
  put_entry(buf, 12, 2, 12, "bar");
  put_entry(buf, 24, 3, 40, "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"); // actual size 40, sum=64.
  assert_false(dir_block_try_insert(buf, BSIZ, "baz", 3, 4, 1));
}

static void insert_sets_new_rec_len_to_remaining_space(void **state) {
  (void)state;
  u8 buf[BSIZ] = {0};
  put_entry(buf, 0, 1, BSIZ, "foo"); // actual 12
  dir_block_try_insert(buf, BSIZ, "bar", 3, 2, 1);
  ext2_dirent_t *de2 = (ext2_dirent_t *)(buf + 12);
  assert_int_equal(de2->rec_len, BSIZ - 12);
}

static void insert_shrinks_predecessor_rec_len(void **state) {
  (void)state;
  u8 buf[BSIZ] = {0};
  put_entry(buf, 0, 1, BSIZ, "foo"); // actual 12
  dir_block_try_insert(buf, BSIZ, "bar", 3, 2, 1);
  ext2_dirent_t *de1 = (ext2_dirent_t *)buf;
  assert_int_equal(de1->rec_len, 12);
}

static void remove_first_entry_sets_inode_zero(void **state) {
  (void)state;
  u8 buf[BSIZ] = {0};
  put_entry(buf, 0, 1, BSIZ, "foo");
  assert_true(dir_block_remove_name(buf, BSIZ, "foo", 3));
  ext2_dirent_t *de = (ext2_dirent_t *)buf;
  assert_int_equal(de->inode, 0);
  assert_int_equal(de->rec_len, BSIZ);
}

static void remove_later_entry_merges_rec_len(void **state) {
  (void)state;
  u8 buf[BSIZ] = {0};
  put_entry(buf, 0, 1, 12, "foo");
  put_entry(buf, 12, 2, 52, "bar");
  assert_true(dir_block_remove_name(buf, BSIZ, "bar", 3));
  ext2_dirent_t *de1 = (ext2_dirent_t *)buf;
  assert_int_equal(de1->rec_len, 64);
}

static void remove_missing_name_returns_false(void **state) {
  (void)state;
  u8 buf[BSIZ] = {0};
  put_entry(buf, 0, 1, BSIZ, "foo");
  assert_false(dir_block_remove_name(buf, BSIZ, "bar", 3));
}

static void remove_name_length_mismatch_not_matched(void **state) {
  (void)state;
  u8 buf[BSIZ] = {0};
  put_entry(buf, 0, 1, BSIZ, "ab");
  assert_false(dir_block_remove_name(buf, BSIZ, "abc", 3));
}

int main(void) {
  const struct CMUnitTest tests[] = {
      cmocka_unit_test(aligned_len_exact_multiple_unchanged),
      cmocka_unit_test(aligned_len_includes_header),
      cmocka_unit_test(aligned_len_rounds_to_four),
      cmocka_unit_test(count_dot_and_dotdot_excluded),
      cmocka_unit_test(count_dot_prefix_not_excluded),
      cmocka_unit_test(count_skips_tombstone),
      cmocka_unit_test(count_stops_at_zero_rec_len),
      cmocka_unit_test(find_name_longer_query_not_matched),
      cmocka_unit_test(find_name_prefix_not_matched),
      cmocka_unit_test(find_name_returns_correct_ptr),
      cmocka_unit_test(find_name_skips_tombstone),
      cmocka_unit_test(find_name_stops_at_zero_rec_len),
      cmocka_unit_test(init_first_entry_rec_len_is_block_size),
      cmocka_unit_test(init_first_entry_sets_correct_fields),
      cmocka_unit_test(insert_into_slack_at_end),
      cmocka_unit_test(insert_returns_false_when_no_slack),
      cmocka_unit_test(insert_sets_new_rec_len_to_remaining_space),
      cmocka_unit_test(insert_shrinks_predecessor_rec_len),
      cmocka_unit_test(remove_first_entry_sets_inode_zero),
      cmocka_unit_test(remove_later_entry_merges_rec_len),
      cmocka_unit_test(remove_missing_name_returns_false),
      cmocka_unit_test(remove_name_length_mismatch_not_matched),
  };
  return cmocka_run_group_tests(tests, NULL, NULL);
}
