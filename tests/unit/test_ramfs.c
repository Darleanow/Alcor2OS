#include "test_common.h"
#include <alcor2/types.h>
#include <alcor2/errno.h>

#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

void *kmalloc(u64 n) { return malloc((size_t)n); }
void  kfree(void *p) { free(p); }
void *kmemcpy(void *d, const void *s, u64 n) { return memcpy(d, s, (size_t)n); }
void  kzero(void *d, u64 n) { memset(d, 0, (size_t)n); }
u64   kstrlen(const char *s) { return strlen(s); }
bool  kstreq(const char *a, const char *b) { return strcmp(a, b) == 0; }
char *kstrncpy(char *d, const char *s, u64 m) { if(!m) return d; u64 i; for(i=0;i<m-1&&s[i];i++) d[i]=s[i]; d[i]='\0'; return d; }
static int g_kzalloc_fail_after = -1; /* -1 = never fail */
void *kzalloc(u64 n) {
  if(g_kzalloc_fail_after == 0) return NULL;
  if(g_kzalloc_fail_after > 0) g_kzalloc_fail_after--;
  return calloc(1, (size_t)n);
}
static int g_krealloc_fail_after = -1;
void *krealloc(void *ptr, u64 size) {
  if(g_krealloc_fail_after == 0) return NULL;
  if(g_krealloc_fail_after > 0) g_krealloc_fail_after--;
  return realloc(ptr, (size_t)size);
}

char *kstrrchr(const char *s, int c) {
  return strrchr(s, c);
}

/* Prevent linking real VFS */
#include <alcor2/fs/vfs.h>
i64 vfs_register_fs(const fs_type_t *fstype) {
  (void)fstype;
  return 0;
}

#include "../../src/fs/ramfs.c"

static int reset_ramfs(void **state) {
  (void)state;
  g_kzalloc_fail_after  = -1;
  g_krealloc_fail_after = -1;
  if(root) root = NULL;
  ramfs_init();
  return 0;
}

static void ram_mkdir_creates_directory(void **state) {
  (void)state;
  assert_int_equal(ram_mkdir(NULL, "/testdir"), 0);
  ram_node_t *node = ram__resolve("/testdir");
  assert_non_null(node);
  assert_int_equal(node->type, VFS_DIRECTORY);
}

static void ram_mkdir_nested_fails_if_parent_missing(void **state) {
  (void)state;
  assert_int_equal(ram_mkdir(NULL, "/missing/dir"), -ENOENT);
}

static void ram_mkdir_nested_fails_if_parent_notdir(void **state) {
  (void)state;
  ram_open(NULL, "/file", O_CREAT | O_WRONLY);
  assert_int_equal(ram_mkdir(NULL, "/file/dir"), -ENOTDIR);
}

static void ram_open_creat_creates_file(void **state) {
  (void)state;
  fs_handle_t fh = ram_open(NULL, "/testfile", O_CREAT | O_WRONLY);
  assert_non_null(fh);
  ram_node_t *node = ram__resolve("/testfile");
  assert_non_null(node);
  assert_int_equal(node->type, VFS_FILE);
}

static void ram_write_and_read_file(void **state) {
  (void)state;
  fs_handle_t fh = ram_open(NULL, "/file_rw", O_CREAT | O_WRONLY);
  assert_non_null(fh);
  const char *data = "hello ramfs";
  assert_int_equal(ram_write(fh, data, 11, 0), 11);
  
  char buf[16] = {0};
  assert_int_equal(ram_read(fh, buf, 11, 0), 11);
  assert_string_equal(buf, "hello ramfs");
}

static void ram_write_past_end_expands_capacity(void **state) {
  (void)state;
  fs_handle_t fh = ram_open(NULL, "/file_expand", O_CREAT | O_WRONLY);
  assert_int_equal(ram_write(fh, "x", 1, 2000), 1);
  ram_node_t *node = (ram_node_t *)fh;
  assert_int_equal(node->size, 2001);
  assert_true(node->capacity >= 2001);
}

static void ram_write_overflow_fails(void **state) {
  (void)state;
  fs_handle_t fh = ram_open(NULL, "/file_overflow", O_CREAT | O_WRONLY);
  /* Test u64 wrap-around check */
  assert_int_equal(ram_write(fh, "x", 10, (u64)-5), -EFBIG);
}

static void ram_unlink_removes_file(void **state) {
  (void)state;
  ram_open(NULL, "/todelete", O_CREAT | O_WRONLY);
  assert_non_null(ram__resolve("/todelete"));
  assert_int_equal(ram_unlink(NULL, "/todelete"), 0);
  assert_null(ram__resolve("/todelete"));
}

static void ram_rmdir_removes_empty_dir(void **state) {
  (void)state;
  ram_mkdir(NULL, "/emptydir");
  assert_int_equal(ram_rmdir(NULL, "/emptydir"), 0);
  assert_null(ram__resolve("/emptydir"));
}

static void ram_rmdir_fails_on_nonempty(void **state) {
  (void)state;
  ram_mkdir(NULL, "/fulldir");
  ram_open(NULL, "/fulldir/file", O_CREAT | O_WRONLY);
  assert_int_equal(ram_rmdir(NULL, "/fulldir"), -ENOTEMPTY);
}

static void ram_truncate_shrinks_size(void **state) {
  (void)state;
  fs_handle_t fh = ram_open(NULL, "/file_trunc", O_CREAT | O_WRONLY);
  ram_write(fh, "1234567890", 10, 0);
  assert_int_equal(ram_truncate(fh, 5), 0);
  ram_node_t *node = (ram_node_t *)fh;
  assert_int_equal(node->size, 5);
}

static void ramfs_chardev_register_works(void **state) {
  (void)state;
  ramfs_chardev_ops_t cops = {0};
  assert_int_equal(ramfs_chardev_register("/mydev", &cops, NULL), 0);
  ram_node_t *node = ram__resolve("/mydev");
  assert_non_null(node);
  assert_ptr_equal(node->cops, &cops);
}

static void ramfs_chardev_register_missing_parent(void **state) {
  (void)state;
  ramfs_chardev_ops_t cops = {0};
  assert_int_equal(ramfs_chardev_register("/missing/mydev", &cops, NULL), -ENOENT);
}

static void ramfs_chardev_register_notdir_parent(void **state) {
  (void)state;
  ram_open(NULL, "/file", O_CREAT | O_WRONLY);
  ramfs_chardev_ops_t cops = {0};
  assert_int_equal(ramfs_chardev_register("/file/mydev", &cops, NULL), -ENOTDIR);
}

static void ram_resolve_rejects_long_name(void **state) {
  (void)state;
  /* VFS_NAME_MAX is usually 256. Let's create a path with > 256 chars */
  char path[300];
  path[0] = '/';
  memset(path + 1, 'a', 260);
  path[261] = '\0';
  assert_null(ram__resolve(path));
}

/* ram_open: missing file without O_CREAT returns NULL */
static void ram_open_missing_no_creat_returns_null(void **state) {
  (void)state;
  assert_null(ram_open(NULL, "/nonexistent", O_RDONLY));
}

/* ram_open: open existing file */
static void ram_open_existing_file(void **state) {
  (void)state;
  ram_open(NULL, "/myfile", O_CREAT | O_WRONLY);
  fs_handle_t fh = ram_open(NULL, "/myfile", O_RDONLY);
  assert_non_null(fh);
}

/* ram_open: open a directory */
static void ram_open_directory(void **state) {
  (void)state;
  ram_mkdir(NULL, "/mydir");
  fs_handle_t fh = ram_open(NULL, "/mydir", O_RDONLY);
  assert_non_null(fh);
}

/* ram_close: does not crash */
static void ram_close_is_noop(void **state) {
  (void)state;
  fs_handle_t fh = ram_open(NULL, "/f", O_CREAT | O_WRONLY);
  ram_close(fh);
}

/* ram_read: offset past end returns 0 */
static void ram_read_past_eof_returns_zero(void **state) {
  (void)state;
  fs_handle_t fh = ram_open(NULL, "/eof", O_CREAT | O_WRONLY);
  ram_write(fh, "hi", 2, 0);
  char buf[4];
  assert_int_equal(ram_read(fh, buf, 4, 100), 0);
}

/* ram_read: on a directory returns -EISDIR */
static void ram_read_on_dir_returns_eisdir(void **state) {
  (void)state;
  ram_mkdir(NULL, "/d");
  fs_handle_t fh = ram_open(NULL, "/d", O_RDONLY);
  char buf[4];
  assert_int_equal((i64)ram_read(fh, buf, 4, 0), -EISDIR);
}

/* ram_write: on a directory returns -EISDIR */
static void ram_write_on_dir_returns_eisdir(void **state) {
  (void)state;
  ram_mkdir(NULL, "/wd");
  fs_handle_t fh = ram_open(NULL, "/wd", O_RDONLY);
  assert_int_equal((i64)ram_write(fh, "x", 1, 0), -EISDIR);
}

/* ram_write: krealloc-based expansion with write into existing capacity */
static void ram_write_within_capacity(void **state) {
  (void)state;
  fs_handle_t fh = ram_open(NULL, "/cap", O_CREAT | O_WRONLY);
  /* First write: allocates capacity */
  assert_int_equal(ram_write(fh, "hello", 5, 0), 5);
  /* Second write: within existing capacity */
  assert_int_equal(ram_write(fh, "world", 5, 0), 5);
}

/* ram_stat: existing file */
static void ram_stat_existing_file(void **state) {
  (void)state;
  fs_handle_t fh = ram_open(NULL, "/sf", O_CREAT | O_WRONLY);
  ram_write(fh, "data", 4, 0);
  vfs_stat_t st;
  assert_int_equal(ram_stat(NULL, "/sf", &st), 0);
  assert_int_equal(st.size, 4);
  assert_int_equal(st.type, VFS_FILE);
}

/* ram_stat: nonexistent returns -ENOENT */
static void ram_stat_missing_returns_enoent(void **state) {
  (void)state;
  vfs_stat_t st;
  assert_int_equal((i64)ram_stat(NULL, "/nosuchfile", &st), -ENOENT);
}

/* ram_fstat */
static void ram_fstat_fills_stat(void **state) {
  (void)state;
  fs_handle_t fh = ram_open(NULL, "/fst", O_CREAT | O_WRONLY);
  ram_write(fh, "abc", 3, 0);
  vfs_stat_t st;
  assert_int_equal(ram_fstat(fh, &st), 0);
  assert_int_equal(st.size, 3);
  assert_int_equal(st.type, VFS_FILE);
}

/* ram_readdir: iterates children */
static void ram_readdir_returns_children(void **state) {
  (void)state;
  ram_mkdir(NULL, "/parent");
  ram_open(NULL, "/parent/child1", O_CREAT | O_WRONLY);
  ram_open(NULL, "/parent/child2", O_CREAT | O_WRONLY);
  fs_handle_t fh = ram_open(NULL, "/parent", O_RDONLY);
  char name[64];
  vfs_stat_t st;
  assert_int_equal(ram_readdir(fh, 0, name, &st), 1);
  assert_int_equal(ram_readdir(fh, 1, name, &st), 1);
  assert_int_equal(ram_readdir(fh, 2, name, NULL), 0); /* past end */
}

/* ram_readdir: on non-directory returns -ENOTDIR */
static void ram_readdir_on_file_returns_enotdir(void **state) {
  (void)state;
  fs_handle_t fh = ram_open(NULL, "/rd_file", O_CREAT | O_WRONLY);
  char name[64];
  assert_int_equal((i64)ram_readdir(fh, 0, name, NULL), -ENOTDIR);
}

/* ram_ioctl: no chardev returns -ENOTTY */
static void ram_ioctl_no_chardev_returns_enotty(void **state) {
  (void)state;
  fs_handle_t fh = ram_open(NULL, "/ioctl_f", O_CREAT | O_WRONLY);
  assert_int_equal((i64)ram_ioctl(fh, 1, 0), -ENOTTY);
}

/* ram_ioctl: chardev with ioctl handler */
static i64 mock_ioctl(void *ctx, u64 req, u64 arg) {
  (void)ctx; (void)req; (void)arg;
  return 42;
}
static void ram_ioctl_chardev_calls_handler(void **state) {
  (void)state;
  ramfs_chardev_ops_t cops = {.ioctl = mock_ioctl};
  ramfs_chardev_register("/cdev_ioctl", &cops, NULL);
  fs_handle_t fh = ram_open(NULL, "/cdev_ioctl", O_RDONLY);
  assert_int_equal(ram_ioctl(fh, 99, 0), 42);
}

/* ram_poll: regular file returns events as-is */
static void ram_poll_regular_file(void **state) {
  (void)state;
  fs_handle_t fh = ram_open(NULL, "/pf", O_CREAT | O_WRONLY);
  assert_int_equal(ram_poll(fh, POLL_IN | POLL_OUT), POLL_IN | POLL_OUT);
  assert_int_equal(ram_poll(fh, POLL_IN), POLL_IN);
}

/* ram_poll: chardev with poll handler */
static u32 mock_poll(void *ctx, u32 events) {
  (void)ctx;
  return events & POLL_OUT; /* only write-ready */
}
static void ram_poll_chardev_delegates(void **state) {
  (void)state;
  ramfs_chardev_ops_t cops = {.poll = mock_poll};
  ramfs_chardev_register("/cdev_poll", &cops, NULL);
  fs_handle_t fh = ram_open(NULL, "/cdev_poll", O_RDONLY);
  assert_int_equal(ram_poll(fh, POLL_IN | POLL_OUT), POLL_OUT);
}

/* ram_unlink: nonexistent file */
static void ram_unlink_nonexistent(void **state) {
  (void)state;
  assert_int_equal((i64)ram_unlink(NULL, "/no_such"), -EISDIR);
}

/* ram_unlink: chardev returns -EBUSY */
static void ram_unlink_chardev_returns_ebusy(void **state) {
  (void)state;
  ramfs_chardev_ops_t cops = {0};
  ramfs_chardev_register("/busy_dev", &cops, NULL);
  assert_int_equal((i64)ram_unlink(NULL, "/busy_dev"), -EBUSY);
}

/* ram_unlink: removes non-first child from parent's list */
static void ram_unlink_non_first_child(void **state) {
  (void)state;
  ram_open(NULL, "/f1", O_CREAT | O_WRONLY);
  ram_open(NULL, "/f2", O_CREAT | O_WRONLY);
  /* f2 is after f1 in children list */
  assert_int_equal(ram_unlink(NULL, "/f2"), 0);
  assert_null(ram__resolve("/f2"));
}

/* ram_rmdir: root directory returns -EBUSY */
static void ram_rmdir_root_returns_ebusy(void **state) {
  (void)state;
  assert_int_equal((i64)ram_rmdir(NULL, "/"), -EBUSY);
}

/* ram_rmdir: non-directory returns -ENOTDIR */
static void ram_rmdir_on_file_returns_enotdir(void **state) {
  (void)state;
  ram_open(NULL, "/rdf", O_CREAT | O_WRONLY);
  assert_int_equal((i64)ram_rmdir(NULL, "/rdf"), -ENOTDIR);
}

/* ram_rmdir: nonexistent returns -ENOENT */
static void ram_rmdir_nonexistent(void **state) {
  (void)state;
  assert_int_equal((i64)ram_rmdir(NULL, "/nosuchdir"), -ENOENT);
}

/* ram_truncate: zero length frees data */
static void ram_truncate_zero_length(void **state) {
  (void)state;
  fs_handle_t fh = ram_open(NULL, "/tz", O_CREAT | O_WRONLY);
  ram_write(fh, "hello", 5, 0);
  assert_int_equal(ram_truncate(fh, 0), 0);
  ram_node_t *node = (ram_node_t *)fh;
  assert_int_equal(node->size, 0);
  assert_null(node->data);
}

/* ram_truncate: length > size returns -ENOSYS */
static void ram_truncate_extend_returns_enosys(void **state) {
  (void)state;
  fs_handle_t fh = ram_open(NULL, "/te", O_CREAT | O_WRONLY);
  ram_write(fh, "hi", 2, 0);
  assert_int_equal((i64)ram_truncate(fh, 100), -ENOSYS);
}

/* ram_truncate: on directory returns -EISDIR */
static void ram_truncate_dir_returns_eisdir(void **state) {
  (void)state;
  ram_mkdir(NULL, "/tdir");
  fs_handle_t fh = ram_open(NULL, "/tdir", O_RDONLY);
  assert_int_equal((i64)ram_truncate(fh, 5), -EISDIR);
}

/* chardev read: delegates to cops->read */
static i64 mock_read(void *ctx, void *buf, u64 n, u64 off) {
  (void)ctx; (void)off;
  *(char *)buf = 'X';
  return (i64)(n > 0 ? 1 : 0);
}
static void ram_read_chardev_calls_handler(void **state) {
  (void)state;
  ramfs_chardev_ops_t cops = {.read = mock_read};
  ramfs_chardev_register("/cdev_r", &cops, NULL);
  fs_handle_t fh = ram_open(NULL, "/cdev_r", O_RDONLY);
  char buf = 0;
  assert_int_equal(ram_read(fh, &buf, 1, 0), 1);
  assert_int_equal(buf, 'X');
}

/* chardev read: no read handler returns -EINVAL */
static void ram_read_chardev_no_handler(void **state) {
  (void)state;
  ramfs_chardev_ops_t cops = {0};
  ramfs_chardev_register("/cdev_nr", &cops, NULL);
  fs_handle_t fh = ram_open(NULL, "/cdev_nr", O_RDONLY);
  char buf;
  assert_int_equal((i64)ram_read(fh, &buf, 1, 0), -EINVAL);
}

/* chardev write: delegates to cops->write */
static i64 mock_write(void *ctx, const void *buf, u64 n, u64 off) {
  (void)ctx; (void)buf; (void)off;
  return (i64)n;
}
static void ram_write_chardev_calls_handler(void **state) {
  (void)state;
  ramfs_chardev_ops_t cops = {.write = mock_write};
  ramfs_chardev_register("/cdev_w", &cops, NULL);
  fs_handle_t fh = ram_open(NULL, "/cdev_w", O_WRONLY);
  assert_int_equal(ram_write(fh, "hi", 2, 0), 2);
}

/* chardev write: no write handler returns -EINVAL */
static void ram_write_chardev_no_handler(void **state) {
  (void)state;
  ramfs_chardev_ops_t cops = {0};
  ramfs_chardev_register("/cdev_nw", &cops, NULL);
  fs_handle_t fh = ram_open(NULL, "/cdev_nw", O_WRONLY);
  assert_int_equal((i64)ram_write(fh, "x", 1, 0), -EINVAL);
}

/* ram_mkdir: already exists returns -EEXIST */
static void ram_mkdir_already_exists(void **state) {
  (void)state;
  assert_int_equal(ram_mkdir(NULL, "/existdir"), 0);
  assert_int_equal((i64)ram_mkdir(NULL, "/existdir"), -EEXIST);
}

/* ram_read: count > available bytes — truncated to avail */
static void ram_read_count_clamped_to_avail(void **state) {
  (void)state;
  fs_handle_t fh = ram_open(NULL, "/clamp", O_CREAT | O_WRONLY);
  ram_write(fh, "hi", 2, 0);
  char buf[16] = {0};
  /* Ask for 16 bytes but only 2 available */
  i64 ret = ram_read(fh, buf, 16, 0);
  assert_int_equal(ret, 2);
  assert_memory_equal(buf, "hi", 2);
}

/* ram_unlink: removes non-first child from list (prev->next path) */
static void ram_rmdir_non_first_child(void **state) {
  (void)state;
  ram_mkdir(NULL, "/sub1");
  ram_mkdir(NULL, "/sub2");
  /* sub2 is after sub1 in root's children */
  assert_int_equal(ram_rmdir(NULL, "/sub2"), 0);
  assert_null(ram__resolve("/sub2"));
  assert_non_null(ram__resolve("/sub1"));
}

/* ram_mkdir: no last slash → -EINVAL */
static void ram_mkdir_no_slash_einval(void **state) {
  (void)state;
  /* Path with no slash at all is caught by the no-last-slash guard */
  assert_int_equal((i64)ram_mkdir(NULL, "nodir"), -EINVAL);
}

/* ram_write: near u64 overflow returns -EFBIG */
static void ram_write_near_overflow_efbig(void **state) {
  (void)state;
  fs_handle_t fh = ram_open(NULL, "/of2", O_CREAT | O_WRONLY);
  /* offset + count overflows: use (u64)-1 - 3 + 10 */
  u64 big_offset = (u64)-1 - 3;
  assert_int_equal((i64)ram_write(fh, "x", 10, big_offset), -EFBIG);
}

/* ram__create_node OOM → ram_open O_CREAT returns NULL */
static void ram_open_creat_oom(void **state) {
  (void)state;
  /* root node consumed 1 kzalloc in ramfs_init; next kzalloc fails */
  g_kzalloc_fail_after = 0;
  fs_handle_t fh = ram_open(NULL, "/newfile", O_CREAT | O_WRONLY);
  assert_null(fh);
}

/* ram_open O_TRUNC on existing file resets size */
static void ram_open_trunc_resets_size(void **state) {
  (void)state;
  fs_handle_t fh = ram_open(NULL, "/f", O_CREAT | O_WRONLY);
  const char *data = "hello";
  ram_write(fh, data, 5, 0);
  /* Re-open with O_TRUNC */
  fh = ram_open(NULL, "/f", O_WRONLY | O_TRUNC);
  assert_non_null(fh);
  ram_node_t *node = ram__resolve("/f");
  assert_int_equal(node->size, 0);
}

/* ram_write: krealloc fails when capacity needs to grow → -ENOMEM */
static void ram_write_krealloc_fail_enomem(void **state) {
  (void)state;
  fs_handle_t fh = ram_open(NULL, "/f", O_CREAT | O_WRONLY);
  g_krealloc_fail_after = 0;
  char buf[64];
  /* Write past current capacity (0) → krealloc needed → fails */
  assert_int_equal((i64)ram_write(fh, buf, 64, 0), -ENOMEM);
}

/* ram_mkdir: ram__create_node OOM → -ENOMEM */
static void ram_mkdir_oom(void **state) {
  (void)state;
  g_kzalloc_fail_after = 0;
  assert_int_equal((i64)ram_mkdir(NULL, "/newdir"), -ENOMEM);
}

/* ram__resolve: path with trailing slash component → breaks cleanly */
static void ram_resolve_trailing_slash(void **state) {
  (void)state;
  /* "/testdir/" — the trailing slash creates an empty component after split */
  ram_mkdir(NULL, "/testdir");
  ram_node_t *node = ram__resolve("/testdir/");
  /* Should return the directory node */
  assert_non_null(node);
}

/* ramfs_chardev_register: null ops → -EINVAL */
static void ramfs_chardev_register_null_ops_einval(void **state) {
  (void)state;
  assert_int_equal((i64)ramfs_chardev_register("/x", NULL, NULL), -EINVAL);
}

/* ramfs_chardev_register: path already exists → -EEXIST */
static void ramfs_chardev_register_eexist(void **state) {
  (void)state;
  ramfs_chardev_ops_t cops = {0};
  /* Register directly under root — parent is "/" which exists */
  assert_int_equal(ramfs_chardev_register("/mydev", &cops, NULL), 0);
  assert_int_equal((i64)ramfs_chardev_register("/mydev", &cops, NULL), -EEXIST);
}

/* ramfs_chardev_register: OOM → -ENOMEM */
static void ramfs_chardev_register_oom_enomem(void **state) {
  (void)state;
  ramfs_chardev_ops_t cops = {0};
  g_kzalloc_fail_after = 0; /* next kzalloc fails */
  /* parent is "/" (root) which exists, node creation fails with OOM */
  assert_int_equal((i64)ramfs_chardev_register("/newdev2", &cops, NULL), -ENOMEM);
}

/* ramfs_init: idempotent — second call returns early (line 421) */
static void ramfs_init_idempotent(void **state) {
  (void)state;
  ramfs_init(); /* root already set → early return */
  assert_non_null(root);
}

/* ram_open O_CREAT: path has no slash → NULL (line 113) */
static void ram_open_creat_no_slash_null(void **state) {
  (void)state;
  /* kstrrchr("noslash", '/') returns NULL → line 113 */
  fs_handle_t fh = ram_open(NULL, "noslash", O_CREAT | O_WRONLY);
  assert_null(fh);
}

/* ram_open O_CREAT: parent is a file not a dir → NULL (line 126) */
static void ram_open_creat_parent_not_dir_null(void **state) {
  (void)state;
  /* Create a file, then try to create inside it */
  ram_open(NULL, "/myfile", O_CREAT | O_WRONLY);
  /* Try /myfile/sub — parent is a file not a dir */
  fs_handle_t fh = ram_open(NULL, "/myfile/sub", O_CREAT | O_WRONLY);
  assert_null(fh);
}

/* ram_write: end wraps around (offset + count overflows) → -EFBIG (line 186) */
static void ram_write_end_overflow_efbig(void **state) {
  (void)state;
  fs_handle_t fh = ram_open(NULL, "/f", O_CREAT | O_WRONLY);
  /* offset near UINT64_MAX → end = offset + count wraps → end > UINT64_MAX - 1023 */
  u64 huge_offset = (u64)-1023ULL - 1ULL; /* end = huge + 1 wraps */
  char buf[1] = "x";
  /* end = huge_offset + 1 > (u64)-1 - 1023u → -EFBIG */
  i64 ret = (i64)ram_write(fh, buf, 1, huge_offset);
  assert_int_equal(ret, -EFBIG);
}

int main(void) {
  const struct CMUnitTest tests[] = {
      cmocka_unit_test_setup(ram_mkdir_creates_directory, reset_ramfs),
      cmocka_unit_test_setup(ram_mkdir_nested_fails_if_parent_missing, reset_ramfs),
      cmocka_unit_test_setup(ram_mkdir_nested_fails_if_parent_notdir, reset_ramfs),
      cmocka_unit_test_setup(ram_open_creat_creates_file, reset_ramfs),
      cmocka_unit_test_setup(ram_resolve_rejects_long_name, reset_ramfs),
      cmocka_unit_test_setup(ram_rmdir_fails_on_nonempty, reset_ramfs),
      cmocka_unit_test_setup(ram_rmdir_removes_empty_dir, reset_ramfs),
      cmocka_unit_test_setup(ram_truncate_shrinks_size, reset_ramfs),
      cmocka_unit_test_setup(ram_unlink_removes_file, reset_ramfs),
      cmocka_unit_test_setup(ram_write_and_read_file, reset_ramfs),
      cmocka_unit_test_setup(ram_write_overflow_fails, reset_ramfs),
      cmocka_unit_test_setup(ram_write_past_end_expands_capacity, reset_ramfs),
      cmocka_unit_test_setup(ramfs_chardev_register_missing_parent, reset_ramfs),
      cmocka_unit_test_setup(ramfs_chardev_register_notdir_parent, reset_ramfs),
      cmocka_unit_test_setup(ramfs_chardev_register_works, reset_ramfs),
      /* ram_open extra */
      cmocka_unit_test_setup(ram_open_missing_no_creat_returns_null, reset_ramfs),
      cmocka_unit_test_setup(ram_open_existing_file, reset_ramfs),
      cmocka_unit_test_setup(ram_open_directory, reset_ramfs),
      /* ram_close */
      cmocka_unit_test_setup(ram_close_is_noop, reset_ramfs),
      /* ram_read extra */
      cmocka_unit_test_setup(ram_read_past_eof_returns_zero, reset_ramfs),
      cmocka_unit_test_setup(ram_read_on_dir_returns_eisdir, reset_ramfs),
      cmocka_unit_test_setup(ram_read_chardev_calls_handler, reset_ramfs),
      cmocka_unit_test_setup(ram_read_chardev_no_handler, reset_ramfs),
      /* ram_write extra */
      cmocka_unit_test_setup(ram_write_on_dir_returns_eisdir, reset_ramfs),
      cmocka_unit_test_setup(ram_write_within_capacity, reset_ramfs),
      cmocka_unit_test_setup(ram_write_chardev_calls_handler, reset_ramfs),
      cmocka_unit_test_setup(ram_write_chardev_no_handler, reset_ramfs),
      /* ram_stat */
      cmocka_unit_test_setup(ram_stat_existing_file, reset_ramfs),
      cmocka_unit_test_setup(ram_stat_missing_returns_enoent, reset_ramfs),
      /* ram_fstat */
      cmocka_unit_test_setup(ram_fstat_fills_stat, reset_ramfs),
      /* ram_readdir */
      cmocka_unit_test_setup(ram_readdir_returns_children, reset_ramfs),
      cmocka_unit_test_setup(ram_readdir_on_file_returns_enotdir, reset_ramfs),
      /* ram_ioctl */
      cmocka_unit_test_setup(ram_ioctl_no_chardev_returns_enotty, reset_ramfs),
      cmocka_unit_test_setup(ram_ioctl_chardev_calls_handler, reset_ramfs),
      /* ram_poll */
      cmocka_unit_test_setup(ram_poll_regular_file, reset_ramfs),
      cmocka_unit_test_setup(ram_poll_chardev_delegates, reset_ramfs),
      /* ram_unlink extra */
      cmocka_unit_test_setup(ram_unlink_nonexistent, reset_ramfs),
      cmocka_unit_test_setup(ram_unlink_chardev_returns_ebusy, reset_ramfs),
      cmocka_unit_test_setup(ram_unlink_non_first_child, reset_ramfs),
      /* ram_rmdir extra */
      cmocka_unit_test_setup(ram_rmdir_root_returns_ebusy, reset_ramfs),
      cmocka_unit_test_setup(ram_rmdir_on_file_returns_enotdir, reset_ramfs),
      cmocka_unit_test_setup(ram_rmdir_nonexistent, reset_ramfs),
      /* ram_truncate extra */
      cmocka_unit_test_setup(ram_truncate_zero_length, reset_ramfs),
      cmocka_unit_test_setup(ram_truncate_extend_returns_enosys, reset_ramfs),
      cmocka_unit_test_setup(ram_truncate_dir_returns_eisdir, reset_ramfs),
      /* ram_mkdir extra */
      cmocka_unit_test_setup(ram_mkdir_already_exists, reset_ramfs),
      cmocka_unit_test_setup(ram_read_count_clamped_to_avail, reset_ramfs),
      cmocka_unit_test_setup(ram_rmdir_non_first_child, reset_ramfs),
      cmocka_unit_test_setup(ram_mkdir_no_slash_einval, reset_ramfs),
      cmocka_unit_test_setup(ram_write_near_overflow_efbig, reset_ramfs),
      /* new coverage */
      cmocka_unit_test_setup(ram_open_creat_oom, reset_ramfs),
      cmocka_unit_test_setup(ram_open_trunc_resets_size, reset_ramfs),
      cmocka_unit_test_setup(ram_write_krealloc_fail_enomem, reset_ramfs),
      cmocka_unit_test_setup(ram_mkdir_oom, reset_ramfs),
      cmocka_unit_test_setup(ram_resolve_trailing_slash, reset_ramfs),
      /* chardev_register error paths */
      cmocka_unit_test_setup(ramfs_chardev_register_null_ops_einval, reset_ramfs),
      cmocka_unit_test_setup(ramfs_chardev_register_eexist, reset_ramfs),
      cmocka_unit_test_setup(ramfs_chardev_register_oom_enomem, reset_ramfs),
      cmocka_unit_test_setup(ramfs_init_idempotent, reset_ramfs),
      cmocka_unit_test_setup(ram_open_creat_no_slash_null, reset_ramfs),
      cmocka_unit_test_setup(ram_open_creat_parent_not_dir_null, reset_ramfs),
      cmocka_unit_test_setup(ram_write_end_overflow_efbig, reset_ramfs),
  };
  return cmocka_run_group_tests(tests, NULL, NULL);
}
