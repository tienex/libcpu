#include <sys/types.h>
#ifndef _WIN32
#include <sys/mman.h> /* host mmap/mprotect; win32 has neither (mprotect shimmed via VirtualProtect) */
#endif
#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <errno.h>
#include <unistd.h>

#include "nix.h"
#include "nix-host.h"

extern void *g_LCLogImpl;

uintmax_t
nix_brk(uintmax_t ptr, nix_env_t *env)
{
	LCLog(g_LCLogImpl, LCLogDebug, 0, "ptr=%llx", ptr);
	return (nix_nosys(env));
}

uintmax_t
nix_sbrk(int incr, nix_env_t *env)
{
	LCLog(g_LCLogImpl, LCLogDebug, 0, "incr=%d", incr);
	return (nix_nosys(env));
}

uintmax_t
nix_sstk(int incr, nix_env_t *env)
{
	LCLog(g_LCLogImpl, LCLogDebug, 0, "incr=%d", incr);
	return (nix_nosys(env));
}

nix_gaddr_t
nix_mmap(nix_gaddr_t gaddr, size_t len, int prot, int flags, int fd,
         off_t offset, nix_env_t *env)
{
	nix_haddr_t      ha;
	nix_gaddr_t      ga;
	nix_host_mmap_t *xm;
	nix_mem_if_t    *mem = nix_env_get_memory(env);
	unsigned         xf = 0;

	LCLog(g_LCLogImpl, LCLogDebug, 0, "addr=%llx, len=%08zx, prot=%x, flags=%x, fd=%d, offset=%lld",
	        (uint64_t)(uintmax_t)gaddr, len, prot, flags, fd, offset);

	if (prot & NIX_PROT_READ)
		xf |= NIX_HOST_MMAP_READ;
	if (prot & NIX_PROT_WRITE)
		xf |= NIX_HOST_MMAP_WRITE;

	if (flags & NIX_MAP_SHARED)
		xf |= NIX_HOST_MMAP_SHARED;

	LCLog(g_LCLogImpl, LCLogDebug, 0, "nix prot = %x host flags = %x", prot, xf);

	if (flags & NIX_MAP_FIXED)
		LCBugCheck(g_LCLogImpl, 5040);

	if (fd != -1)
		LCBugCheck(g_LCLogImpl, 5050);

	xm = nix_host_mmap_create(len, xf);
	ha = (nix_haddr_t)nix_host_mmap_bytes(xm);
	ga = nix_mem_gmap(mem, ha, len, xf);

	LCLog(g_LCLogImpl, LCLogDebug, 0, "mmap()'ed %d bytes at 0x%x w/ flags 0x%x\n", len, ga, flags);

	if (ga == (nix_gaddr_t)(-1)) {
		nix_env_set_errno(env, ENOMEM);
	}

	return (ga);
}

int
nix_munmap(uintmax_t addr, size_t len, nix_env_t *env)
{
	LCLog(g_LCLogImpl, LCLogDebug, 0, "addr=%llx, len=%08x", addr, len);

	return 0; //(nix_nosys(env));
}

int
nix_mlock(uintmax_t addr, size_t len, nix_env_t *env)
{
	LCLog(g_LCLogImpl, LCLogDebug, 0, "addr=%llx, len=%08x", addr, len);

	return (nix_nosys(env));
}

int
nix_munlock(uintmax_t addr, size_t len, nix_env_t *env)
{
	LCLog(g_LCLogImpl, LCLogDebug, 0, "addr=%llx, len=%08x", addr, len);

	return (nix_nosys(env));
}

int
nix_msync(uintmax_t addr, size_t len, int flags, nix_env_t *env)
{
	LCLog(g_LCLogImpl, LCLogDebug, 0, "addr=%llx, len=%08x, flags=%x", addr, len, flags);

	return (nix_nosys(env));
}

int
nix_mlockall(int flags, nix_env_t *env)
{
	LCLog(g_LCLogImpl, LCLogDebug, 0, "flags=%x", flags);

	return (nix_nosys(env));
}

int
nix_munlockall(nix_env_t *env)
{
	LCLog(g_LCLogImpl, LCLogDebug, 0, "invoked", 0);

	return (nix_nosys(env));
}

int
nix_mprotect(uintmax_t addr, size_t len, int prot, nix_env_t *env)
{
	LCLog(g_LCLogImpl, LCLogDebug, 0, "addr=%llx, len=%08x, prot=%x", addr, len, prot);

	if (mprotect((void *)(uintptr_t)addr, len, prot) != 0) { /*XXX*/
		nix_env_set_errno(env, errno);
		return (-1);
	}

	return (0);
}

int
nix_madvise(uintmax_t addr, size_t len, int flags, nix_env_t *env)
{
	LCLog(g_LCLogImpl, LCLogDebug, 0, "addr=%llx, len=%08x, flags=%x", addr, len, flags);

	return (nix_nosys(env));
}

int
nix_mincore(uintmax_t addr, size_t len, char *vec, nix_env_t *env)
{
	LCLog(g_LCLogImpl, LCLogDebug, 0, "addr=%llx, len=%08x, vec=%p", addr, len, vec);

	return (nix_nosys(env));
}

int
nix_minherit(uintmax_t addr, size_t len, int inherit, nix_env_t *env)
{
	LCLog(g_LCLogImpl, LCLogDebug, 0, "addr=%llx, len=%08x, inherit=%d", addr, len, inherit);

	return (nix_nosys(env));
}
