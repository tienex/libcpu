#include <stdlib.h>

#include "nix.h"
#include "xec-debug.h"

void *g_nix_log = NULL;

extern int nix_fd_init(size_t);
#ifndef _WIN32
extern int nix_signal_init(size_t);   /* nix-signal.c is part of the generic layer (not yet on win32) */
#endif

// XXX MOVE TO ENV!

void
nix_init(size_t nfds, size_t nsigs)
{
	if (g_nix_log != NULL)
		return;

	g_nix_log = xec_log_register("nix");

	if (!nix_fd_init(nfds)) {
		XEC_BUGCHECK(NULL, 500);
		abort();
	}

#ifndef _WIN32
	if (!nix_signal_init(nsigs)) {
		XEC_BUGCHECK(NULL, 501);
		abort();
	}
#else
	(void) nsigs;
#endif
}
