/** @file
  OpenVMS host-compatibility shim for libnix's portable core.

  OpenVMS reaches POSIX through the DEC/VSI C Run-Time Library, which provides most of the file,
  process, signal and (via TCP/IP Services) socket surface the generic nix-*.c core needs, so the
  core plus the BSD host family build against the CRTL directly. This header fills the gaps the
  CRTL leaves and papers over a couple of VMS-isms:

    * NIS/YP domain names: VMS has no domain concept -- report "not in a domain" (empty name)
      successfully and an unsupported setter.
    * mprotect: VMS memory protection is page-region based ($SETPRT) and is not exposed as POSIX
      mprotect by the CRTL; report it unsupported (the emulator falls back to no-op protection).
    * The CRTL is most POSIX-faithful in its "DECC$" feature-logical modes (e.g.
      DECC$FILENAME_UNIX_REPORT, DECC$POSIX_SEEK_STREAM_FILE); a VMS build is expected to enable
      those at link time so stat()/lseek()/path handling match POSIX semantics.

  COMPILE-BLIND: no OpenVMS toolchain (cc/CRTL) is available in this tree, so this port is written
  to the documented CRTL API but has not been compiled or run here. The intended gate is a
  VMS-hosted `ctest -R nix.hostops`.

  Copyright (c) the LibCPU developers. Distributed under the 2-clause BSD license.
**/
#ifndef __nix_vms_compat_h
#define __nix_vms_compat_h

#ifdef __VMS

#include <errno.h>
#include <unistd.h>

#ifndef ENOSYS
#define ENOSYS EVMSERR /* the CRTL's generic "VMS-specific error"; mapped to "unsupported" here */
#endif

/* No NIS/YP domain on VMS: report an empty domain successfully, refuse to set one. */
#ifndef NIX_VMS_HAVE_DOMAINNAME
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

/* The CRTL does not expose POSIX mprotect (VMS uses $SETPRT on page regions). Report unsupported
   so the memory layer links; protection changes become advisory on VMS. */
#ifndef NIX_VMS_HAVE_MPROTECT
static int
mprotect(void *addr, size_t len, int prot)
{
	(void)addr;
	(void)len;
	(void)prot;
	errno = ENOSYS;
	return -1;
}
#endif

#endif /* __VMS */

#endif /* !__nix_vms_compat_h */
