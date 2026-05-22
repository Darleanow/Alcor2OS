/**
 * @file include/alcor2/sys/pipe.h
 * @brief Kernel pipe object interface for VFS and syscall layers.
 *
 * Isolated from sys/internal.h so the VFS can use pipe I/O without
 * pulling in all syscall handler declarations.
 */

#ifndef ALCOR2_SYS_PIPE_H
#define ALCOR2_SYS_PIPE_H

#include <alcor2/types.h>

/** @brief True if pipe_read would not block (data in buffer or EOF). */
bool pipe_poll_read_ready(const void *pipe);

/** @brief True if a write would not block (space available or read end closed). */
bool pipe_poll_write_ready(const void *pipe);

/** @brief Read up to @p count bytes from the read end. Returns bytes read or -errno. */
i64 pipe_read_obj(void *pipe, void *buf, u64 count);

/** @brief Write up to @p count bytes to the write end. Returns bytes written or -errno. */
i64 pipe_write_obj(void *pipe, const void *buf, u64 count);

/** @brief Allocate a fresh pipe object. Returns opaque pointer or NULL. */
void *pipe_alloc_obj(void);

/** @brief Release a pipe endpoint when its last fd reference drops. */
void pipe_oft_release(i32 kind, void *pipe);

#endif /* ALCOR2_SYS_PIPE_H */
