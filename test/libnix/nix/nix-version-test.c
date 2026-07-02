/* Unit test for the packed guest-OS version type. */
#include <assert.h>
#include <string.h>
#include "nix-version.h"

int
main (void)
{
	char buf[16];

	/* Packing orders numerically across components. */
	assert (NIX_VERSION (2, 0, 0) < NIX_VERSION (7, 9, 0));
	assert (NIX_VERSION (9, 0, 0) < NIX_VERSION (10, 1, 0)); /* not lexical */
	assert (NIX_VERSION (1, 6, 1) < NIX_VERSION (1, 6, 2));

	/* Parse dotted-numeric, tolerating a missing patch. */
	assert (nix_version_parse ("7.9") == NIX_VERSION (7, 9, 0));
	assert (nix_version_parse ("10.1") == NIX_VERSION (10, 1, 0));
	assert (nix_version_parse ("1.6.2") == NIX_VERSION (1, 6, 2));
	assert (nix_version_parse ("") == NIX_VERSION_NONE);
	assert (nix_version_parse (NULL) == NIX_VERSION_NONE);

	/* Format round-trips major.minor.patch. */
	assert (strcmp (nix_version_format (NIX_VERSION (7, 9, 0), buf, sizeof (buf)), "7.9.0") == 0);
	assert (strcmp (nix_version_format (NIX_VERSION (10, 1, 3), buf, sizeof (buf)), "10.1.3") == 0);
	return 0;
}
