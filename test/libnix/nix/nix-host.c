/*
 * LibCPU-native runtime support for libnix (see nix-host.h): host page allocation and the trap hook.
 * (Logging now lives in LibCPU's shared logging API, core/NixLog.c.) Portable C: POSIX and Win32.
 */

#include "nix-host.h"

#include <stdio.h>

/* ---- host page allocation + trap hook ------------------------------------ */

struct _nix_host_mmap {
	void  *ptr;
	size_t size;
};

#if defined(_WIN32)

#include <windows.h>

nix_host_mmap_t *
nix_host_mmap_create(size_t size, unsigned flags)
{
	DWORD            prot = PAGE_READWRITE;
	void            *p;
	nix_host_mmap_t *mm;

	if (flags & NIX_HOST_MMAP_EXEC) {
		prot = PAGE_EXECUTE_READWRITE;
	}
	p = VirtualAlloc(NULL, size, MEM_COMMIT | MEM_RESERVE, prot);
	if (p == NULL) {
		return NULL;
	}
	mm = (nix_host_mmap_t *)calloc(1, sizeof(*mm));
	if (mm == NULL) {
		VirtualFree(p, 0, MEM_RELEASE);
		return NULL;
	}
	mm->ptr = p;
	mm->size = size;
	return mm;
}

void
nix_host_mmap_free(nix_host_mmap_t *mm)
{
	if (mm != NULL) {
		VirtualFree(mm->ptr, 0, MEM_RELEASE);
		free(mm);
	}
}

/* Win32 has no SIGSEGV-to-handler convention used by the nix core's CORE-only build; the trap hook
   is a no-op there (the nix core's faulting paths are not part of the win32 source selection). */
nix_xcpt_handler_t
nix_xcpt_set_handler(nix_xcpt_handler_t handler)
{
	(void)handler;
	return NULL;
}

#else /* POSIX */

#include <sys/mman.h>
#include <signal.h>

nix_host_mmap_t *
nix_host_mmap_create(size_t size, unsigned flags)
{
	int              prot = 0;
	void            *p;
	nix_host_mmap_t *mm;

	if (flags & NIX_HOST_MMAP_READ) {
		prot |= PROT_READ;
	}
	if (flags & NIX_HOST_MMAP_WRITE) {
		prot |= PROT_WRITE;
	}
	if (flags & NIX_HOST_MMAP_EXEC) {
		prot |= PROT_EXEC;
	}
	if (prot == 0) {
		prot = PROT_READ | PROT_WRITE;
	}

	p = mmap(NULL, size, prot, MAP_ANON | MAP_PRIVATE, -1, 0);
	if (p == MAP_FAILED) {
		return NULL;
	}
	mm = (nix_host_mmap_t *)calloc(1, sizeof(*mm));
	if (mm == NULL) {
		munmap(p, size);
		return NULL;
	}
	mm->ptr = p;
	mm->size = size;
	return mm;
}

void
nix_host_mmap_free(nix_host_mmap_t *mm)
{
	if (mm != NULL) {
		munmap(mm->ptr, mm->size);
		free(mm);
	}
}

static nix_xcpt_handler_t g_nix_xcpt_handler = NULL;

static void
nix_xcpt_trampoline(int signo)
{
	if (g_nix_xcpt_handler != NULL) {
		(*g_nix_xcpt_handler)(signo, NULL);
	}
}

nix_xcpt_handler_t
nix_xcpt_set_handler(nix_xcpt_handler_t handler)
{
	nix_xcpt_handler_t old = g_nix_xcpt_handler;
	struct sigaction   sa;

	g_nix_xcpt_handler = handler;
	if (handler != NULL) {
		memset(&sa, 0, sizeof(sa));
		sa.sa_handler = nix_xcpt_trampoline;
		sigemptyset(&sa.sa_mask);
		sigaction(SIGSEGV, &sa, NULL);
		sigaction(SIGBUS, &sa, NULL);
	}
	return old;
}

#endif /* _WIN32 */

void *
nix_host_mmap_bytes(nix_host_mmap_t const *mm)
{
	return (mm != NULL) ? mm->ptr : NULL;
}
