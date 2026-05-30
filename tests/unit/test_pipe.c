#include "test_common.h"

#include <alcor2/types.h>

#include <string.h>

void *kmalloc(u64 n)
{
  (void)n;
  return NULL;
}
void kfree(void *p)
{
  (void)p;
}
void kzero(void *d, u64 n)
{
  memset(d, 0, n);
}
void console_print(const char *s)
{
  (void)s;
}

/* No scheduler — proc_current returns NULL so pipe never blocks. */
struct proc;
struct proc *proc_current(void)
{
  return NULL;
}
void proc_block(struct proc *p)
{
  (void)p;
}
void proc_wake(struct proc *p)
{
  (void)p;
}
void proc_schedule(void) {}

#include "../../src/kernel/sys/pipe.c"

static int setup(void **state)
{
  (void)state;
  memset(pipes, 0, sizeof(pipes));
  return 0;
}

static pipe_t *new_pipe(void)
{
  return (pipe_t *)pipe_alloc_obj();
}

static void alloc_returns_non_null(void **state)
{
  (void)state;
  pipe_t *p = new_pipe();
  assert_non_null(p);
  assert_true(p->allocated);
  assert_int_equal(p->read_open, 1);
  assert_int_equal(p->write_open, 1);
}

static void alloc_exhaustion(void **state)
{
  (void)state;
  pipe_t *ptrs[MAX_PIPES];
  for(int i = 0; i < MAX_PIPES; i++)
    ptrs[i] = new_pipe();
  assert_null(pipe_alloc_obj());
  for(int i = 0; i < MAX_PIPES; i++)
    ptrs[i]->allocated = 0;
}

static void write_and_read_roundtrip(void **state)
{
  (void)state;
  pipe_t *p = new_pipe();
  char    buf[8];
  assert_int_equal(pipe_write_obj(p, "hello", 5), 5);
  assert_int_equal(pipe_read_obj(p, buf, 8), 5);
  assert_memory_equal(buf, "hello", 5);
  p->allocated = 0;
}

static void partial_read(void **state)
{
  (void)state;
  pipe_t *p = new_pipe();
  char    buf[2];
  pipe_write_obj(p, "abcde", 5);
  assert_int_equal(pipe_read_obj(p, buf, 2), 2);
  assert_memory_equal(buf, "ab", 2);
  assert_int_equal(p->count, 3);
  p->allocated = 0;
}

static void read_empty_write_end_closed_returns_zero(void **state)
{
  (void)state;
  pipe_t *p     = new_pipe();
  p->write_open = 0;
  char buf[4]   = {0};
  assert_int_equal(pipe_read_obj(p, buf, 4), 0);
  p->allocated = 0;
}

static void write_read_end_closed_returns_epipe(void **state)
{
  (void)state;
  pipe_t *p    = new_pipe();
  p->read_open = 0;
  char buf[4]  = "hi";
  assert_int_equal((i64)pipe_write_obj(p, buf, 2), -EPIPE);
  p->allocated = 0;
}

static void write_fills_buffer_exactly(void **state)
{
  (void)state;
  pipe_t *p = new_pipe();
  char    src[PIPE_BUF_SIZE];
  memset(src, 0xAB, sizeof(src));
  assert_int_equal(pipe_write_obj(p, src, PIPE_BUF_SIZE), PIPE_BUF_SIZE);
  assert_int_equal(p->count, PIPE_BUF_SIZE);
  p->allocated = 0;
}

static void write_wraps_around(void **state)
{
  (void)state;
  pipe_t *p = new_pipe();
  char    fill[PIPE_BUF_SIZE / 2];
  char    drain[PIPE_BUF_SIZE / 2];
  char    tail[4] = {1, 2, 3, 4};
  char    got[4];

  memset(fill, 'X', sizeof(fill));
  pipe_write_obj(p, fill, sizeof(fill));
  pipe_read_obj(p, drain, sizeof(drain));
  pipe_write_obj(p, fill, sizeof(fill));
  pipe_write_obj(p, tail, 4);

  /* drain remaining fill */
  pipe_read_obj(p, drain, sizeof(drain));
  assert_int_equal(pipe_read_obj(p, got, 4), 4);
  assert_memory_equal(got, tail, 4);
  p->allocated = 0;
}

static void poll_read_ready_with_data(void **state)
{
  (void)state;
  pipe_t *p = new_pipe();
  pipe_write_obj(p, "x", 1);
  assert_true(pipe_poll_read_ready(p));
  p->allocated = 0;
}

static void poll_read_ready_empty_write_closed(void **state)
{
  (void)state;
  pipe_t *p     = new_pipe();
  p->write_open = 0;
  assert_true(pipe_poll_read_ready(p));
  p->allocated = 0;
}

static void poll_read_not_ready_empty(void **state)
{
  (void)state;
  pipe_t *p = new_pipe();
  assert_false(pipe_poll_read_ready(p));
  p->allocated = 0;
}

static void poll_write_ready_has_space(void **state)
{
  (void)state;
  pipe_t *p = new_pipe();
  assert_true(pipe_poll_write_ready(p));
  p->allocated = 0;
}

static void poll_write_not_ready_when_write_closed(void **state)
{
  (void)state;
  pipe_t *p     = new_pipe();
  p->write_open = 0;
  assert_false(pipe_poll_write_ready(p));
  p->allocated = 0;
}

static void rd_release_frees_when_both_closed(void **state)
{
  (void)state;
  pipe_t *p     = new_pipe();
  p->write_open = 0;
  pipe_rd_release(p);
  assert_false(p->allocated);
}

static void wr_release_frees_when_both_closed(void **state)
{
  (void)state;
  pipe_t *p    = new_pipe();
  p->read_open = 0;
  pipe_wr_release(p);
  assert_false(p->allocated);
}

static void rd_release_does_not_free_if_write_still_open(void **state)
{
  (void)state;
  pipe_t *p = new_pipe();
  pipe_rd_release(p);
  assert_true(p->allocated);
  p->allocated = 0;
}

static void read_on_closed_read_end_returns_ebadf(void **state)
{
  (void)state;
  pipe_t *p    = new_pipe();
  p->read_open = 0;
  char buf[4];
  assert_int_equal((i64)pipe_read_obj(p, buf, 4), -EBADF);
  p->allocated = 0;
}

static void write_on_closed_write_end_returns_ebadf(void **state)
{
  (void)state;
  pipe_t *p     = new_pipe();
  p->write_open = 0;
  char buf[4]   = "hi";
  assert_int_equal((i64)pipe_write_obj(p, buf, 2), -EBADF);
  p->allocated = 0;
}

int main(void)
{
  const struct CMUnitTest tests[] = {
      cmocka_unit_test_setup(alloc_returns_non_null, setup),
      cmocka_unit_test_setup(alloc_exhaustion, setup),
      cmocka_unit_test_setup(write_and_read_roundtrip, setup),
      cmocka_unit_test_setup(partial_read, setup),
      cmocka_unit_test_setup(read_empty_write_end_closed_returns_zero, setup),
      cmocka_unit_test_setup(write_read_end_closed_returns_epipe, setup),
      cmocka_unit_test_setup(write_fills_buffer_exactly, setup),
      cmocka_unit_test_setup(write_wraps_around, setup),
      cmocka_unit_test_setup(poll_read_ready_with_data, setup),
      cmocka_unit_test_setup(poll_read_ready_empty_write_closed, setup),
      cmocka_unit_test_setup(poll_read_not_ready_empty, setup),
      cmocka_unit_test_setup(poll_write_ready_has_space, setup),
      cmocka_unit_test_setup(poll_write_not_ready_when_write_closed, setup),
      cmocka_unit_test_setup(rd_release_frees_when_both_closed, setup),
      cmocka_unit_test_setup(wr_release_frees_when_both_closed, setup),
      cmocka_unit_test_setup(
          rd_release_does_not_free_if_write_still_open, setup
      ),
      cmocka_unit_test_setup(read_on_closed_read_end_returns_ebadf, setup),
      cmocka_unit_test_setup(write_on_closed_write_end_returns_ebadf, setup),
  };
  return cmocka_run_group_tests(tests, NULL, NULL);
}
