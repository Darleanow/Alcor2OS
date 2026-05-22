/**
 * @file include/alcor2/fs/pipe.h
 * @brief Kernel pipe object interface shared by the VFS and syscall layers.
 *
 * A pipe is an anonymous, unidirectional byte channel backed by a fixed-size
 * ring buffer. The VFS stores one opaque @c pipe pointer per OFT entry and
 * dispatches reads/writes through this interface. The pipe object itself is
 * allocated by @ref pipe_alloc_obj and freed when both ends are released.
 */

#ifndef ALCOR2_FS_PIPE_H
#define ALCOR2_FS_PIPE_H

#include <alcor2/types.h>

/**
 * @brief Allocate a fresh pipe object with both ends open.
 * @return Opaque pointer to the new pipe, or NULL if the pipe table is full.
 */
void *pipe_alloc_obj(void);

/**
 * @brief Check whether a read would return data immediately.
 *
 * Returns true if the ring buffer is non-empty, or if the write end is
 * closed (next read will return EOF rather than blocking).
 *
 * @param pipe Opaque pipe pointer returned by @ref pipe_alloc_obj.
 */
bool pipe_poll_read_ready(const void *pipe);

/**
 * @brief Check whether a write would proceed without blocking.
 *
 * Returns true if there is space in the ring buffer, or if the read end
 * is closed (next write will return @c -EPIPE rather than blocking).
 *
 * @param pipe Opaque pipe pointer returned by @ref pipe_alloc_obj.
 */
bool pipe_poll_write_ready(const void *pipe);

/**
 * @brief Read up to @p count bytes from the pipe's read end.
 *
 * Blocks if the buffer is empty and the write end is still open. Returns 0
 * (EOF) when the write end is closed and the buffer is drained.
 *
 * @param pipe  Opaque pipe pointer.
 * @param buf   Destination buffer.
 * @param count Maximum bytes to read.
 * @return Bytes read (≥ 0), or negative @c -errno on error.
 */
i64 pipe_read_obj(void *pipe, void *buf, u64 count);

/**
 * @brief Write up to @p count bytes to the pipe's write end.
 *
 * Blocks when the ring buffer is full and the read end is still open.
 * Returns @c -EPIPE if the read end has been closed.
 *
 * @param pipe  Opaque pipe pointer.
 * @param buf   Source buffer.
 * @param count Bytes to write.
 * @return Bytes written (≥ 0), or negative @c -errno on error.
 */
i64 pipe_write_obj(void *pipe, const void *buf, u64 count);

/**
 * @brief Release the read end of a pipe.
 *
 * Decrements the read-open counter. When it reaches zero, any blocked writer
 * is woken so it can return @c -EPIPE. Frees the pipe object if both ends are
 * now closed.
 *
 * @param pipe Opaque pipe pointer.
 */
void pipe_rd_release(void *pipe);

/**
 * @brief Release the write end of a pipe.
 *
 * Decrements the write-open counter. When it reaches zero, any blocked reader
 * is woken so it can return EOF (0). Frees the pipe object if both ends are
 * now closed.
 *
 * @param pipe Opaque pipe pointer.
 */
void pipe_wr_release(void *pipe);

#endif
