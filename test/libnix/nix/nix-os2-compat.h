/** @file
  OS/2 host-compatibility shim for libnix's portable core.

  The modern OS/2 GCC runtime, kLIBC (the libc of the OS/2 / ArcaOS GCC port), is POSIX-family
  capable: it provides fork/exec/wait, pipe/fcntl/select/poll, vectored I/O, mmap/mprotect, and
  BSD sockets (over the OS/2 TCP/IP stack). So OS/2 builds the portable CORE plus the BSD host
  family (exactly like Haiku/VMS, not the CORE-only surface win32 uses), and per-function presence
  is settled by the CMake CHECK_FUNCTION_EXISTS probes (HAVE_*). This header only fills the few
  gaps those probes do not cover:

    * NIS/YP domain names: OS/2 has no domain concept -- report "not in a domain" (empty name)
      successfully and an unsupported setter.

  COMPILE-BLIND: no OS/2 (kLIBC) toolchain is available in this tree, so this port is written to
  the documented kLIBC + OS/2 API but has not been compiled or run here. Its source SELECTION
  (CORE + BSD) is, however, cross-verified by building and running nixtest with
  -DNIX_HOST_PROFILE=os2 on a BSD-family build host (see nix/CMakeLists.txt). The intended native
  gate is an OS/2 (or ArcaOS) `ctest -R nix.hostops`.

  Copyright (c) the LibCPU developers. Distributed under the 2-clause BSD license.
**/
#ifndef __nix_os2_compat_h
#define __nix_os2_compat_h

#if defined(__OS2__) || defined(__EMX__)

#include <errno.h>
#include <unistd.h>

#ifndef ENOSYS
#define ENOSYS 88 /* EMX/kLIBC ENOSYS */
#endif

/* No NIS/YP domain on OS/2: report an empty domain successfully, refuse to set one. (Guarded in
   case a given kLIBC revision already provides the BSD calls.) */
#ifndef NIX_OS2_HAVE_DOMAINNAME
static int
getdomainname(char *buf, size_t len)
{
	if (len == 0)
		return 0;
	buf[0] = '\0';
	return 0;
}

static int
setdomainname(char const *name, size_t len)
{
	(void)name;
	(void)len;
	errno = ENOSYS;
	return -1;
}
#endif

/* kLIBC does not declare sethostname (host identity is fixed) or reboot (the guest must never reboot
   the host): report both unsupported, as the Win32/Haiku shims do. */
#ifndef NIX_OS2_HAVE_SETHOSTNAME
static int
sethostname(char const *name, size_t len)
{
	(void)name;
	(void)len;
	errno = ENOSYS;
	return -1;
}
#endif
#ifndef NIX_OS2_HAVE_REBOOT
static int
reboot(int howto)
{
	(void)howto;
	errno = ENOSYS;
	return -1;
}
#endif

#endif /* __OS2__ || __EMX__ */

#endif /* !__nix_os2_compat_h */
