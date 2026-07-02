#ifndef __netbsd_ioctl_h
#define __netbsd_ioctl_h

#include "netbsd-ioctl-defs.h"

/* Ioctls */

/* Terminal I/O */
#define NETBSD_TIOCGETA  _NETBSD_IOR('t', 19, struct netbsd_termios)
#define NETBSD_TIOCSETA  _NETBSD_IOW('t', 20, struct netbsd_termios)
#define NETBSD_TIOCSETAW _NETBSD_IOW('t', 21, struct netbsd_termios)
#define NETBSD_TIOCSETAF _NETBSD_IOW('t', 22, struct netbsd_termios)

/* File */
#define NETBSD_FIONREAD  _NETBSD_IOR('f', 127, int)

int
netbsd_ioctl_dispatch (nix_env_t        *env,
					   nix_endian_t      endian,
					   int               fd,
					   netbsd_ulong_t    request,
					   netbsd_uintptr_t  arg);

#endif  /* !__netbsd_ioctl_h */
