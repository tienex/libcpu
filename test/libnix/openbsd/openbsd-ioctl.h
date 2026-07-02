#ifndef __openbsd_ioctl_h
#define __openbsd_ioctl_h

#include "openbsd-ioctl-defs.h"

/* Ioctls */

/* Terminal I/O */
#define OPENBSD_TIOCGETA  _OPENBSD_IOR('t', 19, struct openbsd_termios)
#define OPENBSD_TIOCSETA  _OPENBSD_IOW('t', 20, struct openbsd_termios)
#define OPENBSD_TIOCSETAW _OPENBSD_IOW('t', 21, struct openbsd_termios)
#define OPENBSD_TIOCSETAF _OPENBSD_IOW('t', 22, struct openbsd_termios)

/* File */
#define OPENBSD_FIONREAD  _OPENBSD_IOR('f', 127, int)

int
openbsd_ioctl_dispatch (nix_env_t        *env,
					   nix_endian_t      endian,
					   int               fd,
					   openbsd_ulong_t    request,
					   openbsd_uintptr_t  arg);

#endif  /* !__openbsd_ioctl_h */
