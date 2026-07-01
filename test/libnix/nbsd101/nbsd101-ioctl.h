#ifndef __nbsd101_ioctl_h
#define __nbsd101_ioctl_h

#include "nbsd101-ioctl-defs.h"

/* Ioctls */

/* Terminal I/O */
#define NBSD101_TIOCGETA  _NBSD101_IOR('t', 19, struct nbsd101_termios)
#define NBSD101_TIOCSETA  _NBSD101_IOW('t', 20, struct nbsd101_termios)
#define NBSD101_TIOCSETAW _NBSD101_IOW('t', 21, struct nbsd101_termios)
#define NBSD101_TIOCSETAF _NBSD101_IOW('t', 22, struct nbsd101_termios)

/* File */
#define NBSD101_FIONREAD  _NBSD101_IOR('f', 127, int)

int
nbsd101_ioctl_dispatch (nix_env_t        *env,
					   nix_endian_t      endian,
					   int               fd,
					   nbsd101_ulong_t    request,
					   nbsd101_uintptr_t  arg);

#endif  /* !__nbsd101_ioctl_h */
