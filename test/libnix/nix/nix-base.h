#ifndef __nix_base_h
#define __nix_base_h

/* Win32 host-compat shim: supplies the POSIX host primitives MinGW lacks (included before the
   nix headers/sources that call them). A no-op on POSIX hosts. */
#include "nix-win32-compat.h"

#include "nix-types.h"

#include "nix-env.h"
#include "nix-xcpt.h"
#include "nix-signal.h"
#include "nix-file.h"
#include "nix-stat.h"
#include "nix-socket.h"
#include "nix-rt.h"
#include "nix-bsd.h"
#include "nix-linux.h"
#include "nix-mem.h"
#include "nix-termios.h"
#include "nix-ioctl.h"

#endif  /* !__nix_base_h */
