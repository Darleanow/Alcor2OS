/**
 * @file include/alcor2/fs/pipe.h
 * @brief Kernel pipe object interface for VFS and syscall layers.
 */

#ifndef ALCOR2_FS_PIPE_H
#define ALCOR2_FS_PIPE_H

#include <alcor2/types.h>

bool pipe_poll_read_ready(const void *pipe);
bool pipe_poll_write_ready(const void *pipe);
i64  pipe_read_obj(void *pipe, void *buf, u64 count);
i64  pipe_write_obj(void *pipe, const void *buf, u64 count);
void *pipe_alloc_obj(void);
void pipe_rd_release(void *pipe);
void pipe_wr_release(void *pipe);

#endif
