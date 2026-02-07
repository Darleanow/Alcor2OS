/**
 * @file include/alcor2/errno.h
 * @brief Standard error number definitions (Linux-compatible).
 *
 * All kernel functions that can fail should return negative errno values.
 * This ensures consistent error handling across the entire codebase.
 */

#ifndef ALCOR2_ERRNO_H
#define ALCOR2_ERRNO_H

/** @name Standard POSIX error codes
 * @{ */

#define EPERM        1  /**< Operation not permitted */
#define ENOENT       2  /**< No such file or directory */
#define ESRCH        3  /**< No such process */
#define EINTR        4  /**< Interrupted system call */
#define EIO          5  /**< Input/output error */
#define ENXIO        6  /**< No such device or address */
#define E2BIG        7  /**< Argument list too long */
#define ENOEXEC      8  /**< Exec format error */
#define EBADF        9  /**< Bad file descriptor */
#define ECHILD       10 /**< No child processes */
#define EAGAIN       11 /**< Resource temporarily unavailable */
#define ENOMEM       12 /**< Out of memory */
#define EACCES       13 /**< Permission denied */
#define EFAULT       14 /**< Bad address */
#define ENOTBLK      15 /**< Block device required */
#define EBUSY        16 /**< Device or resource busy */
#define EEXIST       17 /**< File exists */
#define EXDEV        18 /**< Invalid cross-device link */
#define ENODEV       19 /**< No such device */
#define ENOTDIR      20 /**< Not a directory */
#define EISDIR       21 /**< Is a directory */
#define EINVAL       22 /**< Invalid argument */
#define ENFILE       23 /**< Too many open files in system */
#define EMFILE       24 /**< Too many open files */
#define ENOTTY       25 /**< Inappropriate ioctl for device */
#define ETXTBSY      26 /**< Text file busy */
#define EFBIG        27 /**< File too large */
#define ENOSPC       28 /**< No space left on device */
#define ESPIPE       29 /**< Illegal seek */
#define EROFS        30 /**< Read-only filesystem */
#define EMLINK       31 /**< Too many links */
#define EPIPE        32 /**< Broken pipe */
#define EDOM         33 /**< Math argument out of domain */
#define ERANGE       34 /**< Result too large */
#define EDEADLK      35 /**< Resource deadlock avoided */
#define ENAMETOOLONG 36 /**< File name too long */
#define ENOLCK       37 /**< No locks available */
#define ENOSYS       38 /**< Function not implemented */
#define ENOTEMPTY    39 /**< Directory not empty */
#define ELOOP        40 /**< Too many levels of symbolic links */

/** @} */

#endif /* ALCOR2_ERRNO_H */
