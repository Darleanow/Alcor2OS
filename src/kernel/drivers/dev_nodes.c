/**
 * @file src/kernel/drivers/dev_nodes.c
 * @brief /dev/null and /dev/zero character device implementations.
 *
 * Both are registered as ramfs chardevs during kernel init.
 * /dev/null: reads return EOF, writes are silently discarded.
 * /dev/zero: reads fill the buffer with zeroes, writes are silently discarded.
 */

#include <alcor2/drivers/console.h>
#include <alcor2/errno.h>
#include <alcor2/fs/ramfs.h>
#include <alcor2/kstdlib.h>
#include <alcor2/types.h>

/**
 * @brief Read from /dev/null — always returns 0 (EOF).
 *
 * @param ctx     Unused.
 * @param buf     Destination buffer (untouched).
 * @param count   Requested byte count (ignored).
 * @param offset  File offset (ignored).
 * @return Always 0.
 */
static i64 null_read(void *ctx, void *buf, u64 count, u64 offset)
{
  (void)ctx;
  (void)buf;
  (void)count;
  (void)offset;
  return 0;
}

/**
 * @brief Write to /dev/null — silently discards all data.
 *
 * @param ctx     Unused.
 * @param buf     Source buffer (ignored).
 * @param count   Number of bytes to "write".
 * @param offset  File offset (ignored).
 * @return @p count (pretends all bytes were written).
 */
static i64 null_write(void *ctx, const void *buf, u64 count, u64 offset)
{
  (void)ctx;
  (void)buf;
  (void)offset;
  return (i64)count;
}

/**
 * @brief Read from /dev/zero — fills the buffer with zero bytes.
 *
 * @param ctx     Unused.
 * @param buf     Destination buffer; filled with 0x00.
 * @param count   Number of bytes to produce.
 * @param offset  File offset (ignored).
 * @return @p count.
 */
static i64 zero_read(void *ctx, void *buf, u64 count, u64 offset)
{
  (void)ctx;
  (void)offset;
  kzero(buf, count);
  return (i64)count;
}

static const ramfs_chardev_ops_t null_ops = {
    .read  = null_read,
    .write = null_write,
};

static const ramfs_chardev_ops_t zero_ops = {
    .read  = zero_read,
    .write = null_write,
};

/**
 * @brief Register /dev/null and /dev/zero on the ramfs mounted at /dev.
 */
void dev_nodes_init(void)
{
  i64 rc;
  rc = ramfs_chardev_register("/null", &null_ops, NULL);
  if(rc < 0)
    console_printf("[dev] /dev/null register failed: %d\n", (int)rc);
  rc = ramfs_chardev_register("/zero", &zero_ops, NULL);
  if(rc < 0)
    console_printf("[dev] /dev/zero register failed: %d\n", (int)rc);
}
