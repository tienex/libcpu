#ifndef __nix_base_h
#define __nix_base_h

/* Host-compatibility shims: supply the POSIX host primitives a non-POSIX (or partially-POSIX)
   host lacks, included before the nix headers/sources that call them. Each header is guarded by
   its own host macro (_WIN32 / __OS2__ / __VMS / __HAIKU__) and is a no-op on every other host,
   so they can all be included unconditionally. POSIX hosts (Linux/BSD/macOS/Solaris) see no-ops. */
#include "nix-win32-compat.h" /* Win32 (MinGW): verified under Wine */
#include "nix-os2-compat.h"   /* OS/2 (EMX/kLIBC): compile-blind (no local toolchain) */
#include "nix-vms-compat.h"   /* OpenVMS (DEC C RTL): compile-blind (no local toolchain) */
#include "nix-haiku-compat.h" /* Haiku (POSIX + BeOS): compile-blind (no local toolchain) */

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

#endif /* !__nix_base_h */
