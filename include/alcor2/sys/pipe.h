/**
 * @file include/alcor2/sys/pipe.h
 * @deprecated Use <alcor2/fs/pipe.h> instead.
 *
 * The pipe interface was moved to the fs/ layer because pipes are VFS objects.
 * This header is kept only for out-of-tree consumers; it will be removed once
 * all internal users have been updated.
 */
#ifndef ALCOR2_SYS_PIPE_H
#define ALCOR2_SYS_PIPE_H
#include <alcor2/fs/pipe.h>
#endif
