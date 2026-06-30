/** @file
  Haiku host-compatibility shim for libnix's portable core.

  Haiku is a POSIX operating system (libroot implements the POSIX file/process/signal surface and
  a FreeBSD-derived network stack), so unlike the Win32 shim almost nothing is missing: the generic
  nix-*.c core plus the BSD host family compile against Haiku's native primitives. This header only
  fills the few genuine gaps:

    * NIS/YP domain names: Haiku has no domain concept (getdomainname/setdomainname absent), so we
      provide the same "not in a domain" answer a non-NIS POSIX host gives -- an empty name reported
      successfully -- and an unsupported setter.
    * mknod: Haiku exposes device creation through a different (devfs) mechanism, not POSIX mknod;
      report it unsupported so the file-op core links.

  Everything else (fsync, pread/pwrite, ftruncate, lstat, symlink/link, chown/fchmod, readlink,
  gethostname, getuid.., kill, mprotect, SysV IPC) is native on Haiku and used directly.

  COMPILE-BLIND: no Haiku toolchain is available in this tree, so this port is written to the
  documented Haiku/POSIX API but has not been compiled or run here (cf. the Win32 port, which is
  verified under Wine). The intended gate is a Haiku-hosted `ctest -R nix.hostops`.

  Copyright (c) the LibCPU developers. Distributed under the 2-clause BSD license.
**/
#ifndef __nix_haiku_compat_h
#define __nix_haiku_compat_h

#ifdef __HAIKU__

#include <errno.h>
#include <unistd.h>
#include <sys/stat.h>

#ifndef ENOSYS
#define ENOSYS 9901 /* B_NOT_SUPPORTED maps to ENOSYS in Haiku's POSIX layer */
#endif

/* Haiku has no NIS/YP domain: report "not in a domain" (empty name) successfully, as a
   non-NIS POSIX host does, and report the setter unsupported. Guarded in case a future
   Haiku release adds the BSD calls. */
#ifndef NIX_HAIKU_HAVE_DOMAINNAME
static inline int
getdomainname(char *buf, size_t len)
{
	if (len == 0)
		return 0;
	buf[0] = '\0';
	return 0;
}

static inline int
setdomainname(char const *name, size_t len)
{
	(void)name;
	(void)len;
	errno = ENOSYS;
	return -1;
}
#endif

/* POSIX mknod has no direct Haiku analog (device nodes come from devfs, not user mknod):
   report unsupported so the file-op core links. */
#ifndef NIX_HAIKU_HAVE_MKNOD
static inline int
nix_haiku_mknod_stub(char const *path, int mode, dev_t dev)
{
	(void)path;
	(void)mode;
	(void)dev;
	errno = ENOSYS;
	return -1;
}
#define mknod(path, mode, dev) nix_haiku_mknod_stub((path), (mode), (dev))
#endif

/* Haiku has no user-space reboot(2) (and no <sys/reboot.h>): report it unsupported. The guest must
   never reboot the host anyway. (nix_reboot otherwise calls reboot() directly.) */
#ifndef NIX_HAIKU_HAVE_REBOOT
static inline int
nix_haiku_reboot_stub(int howto)
{
	(void)howto;
	errno = ENOSYS;
	return -1;
}
#define reboot(howto) nix_haiku_reboot_stub((howto))
#endif

#endif /* __HAIKU__ */

#endif /* !__nix_haiku_compat_h */
