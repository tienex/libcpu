/* The version gate: a descriptor is available iff since <= target && (until==NONE || target < until). */
#include <assert.h>
#include "nix-syscall.h"
#include "nix-version.h"

int
main (void)
{
	/* Fields: number, name, format, rettype, flags, nparams, callback, since, until. */
	/* write(2): present from 2.0, never removed. */
	nix_us_syscall_desc_t write_desc = { 4, "write", "wpw", "w", 0, 3, NULL,
	                                     NIX_VERSION (2, 0, 0), NIX_VERSION_NONE };
	/* pinsyscall(2): introduced 7.4. */
	nix_us_syscall_desc_t pin_desc = { 336, "pinsyscall", "", "w", 0, 0, NULL,
	                                   NIX_VERSION (7, 4, 0), NIX_VERSION_NONE };
	/* an old call removed at 3.6. */
	nix_us_syscall_desc_t old_desc = { 42, "oldcall", "", "w", 0, 0, NULL,
	                                   NIX_VERSION (2, 0, 0), NIX_VERSION (3, 6, 0) };

	assert (nix_us_syscall_desc_available (&write_desc, NIX_VERSION (2, 0, 0)));
	assert (nix_us_syscall_desc_available (&write_desc, NIX_VERSION (7, 9, 0)));

	assert (!nix_us_syscall_desc_available (&pin_desc, NIX_VERSION (5, 9, 0))); /* too early */
	assert (nix_us_syscall_desc_available (&pin_desc, NIX_VERSION (7, 9, 0)));

	assert (nix_us_syscall_desc_available (&old_desc, NIX_VERSION (2, 0, 0)));
	assert (!nix_us_syscall_desc_available (&old_desc, NIX_VERSION (3, 6, 0))); /* removed */
	assert (!nix_us_syscall_desc_available (&old_desc, NIX_VERSION (7, 9, 0)));

	/* LATEST target sees everything with a real since. */
	assert (nix_us_syscall_desc_available (&pin_desc, NIX_VERSION_LATEST));
	return 0;
}
