#ifndef __obsd79_ioctl_h
#define __obsd79_ioctl_h

#include "obsd79-ioctl-defs.h"

/* Ioctls */

/* Terminal I/O */
#define OBSD79_TIOCGETA  _OBSD79_IOR('t', 19, struct obsd79_termios)
#define OBSD79_TIOCSETA  _OBSD79_IOW('t', 20, struct obsd79_termios)
#define OBSD79_TIOCSETAW _OBSD79_IOW('t', 21, struct obsd79_termios)
#define OBSD79_TIOCSETAF _OBSD79_IOW('t', 22, struct obsd79_termios)

/* File */
#define OBSD79_FIONREAD  _OBSD79_IOR('f', 127, int)

int
obsd79_ioctl_dispatch (nix_env_t        *env,
					   nix_endian_t      endian,
					   int               fd,
					   obsd79_ulong_t    request,
					   obsd79_uintptr_t  arg);

#endif  /* !__obsd79_ioctl_h */
