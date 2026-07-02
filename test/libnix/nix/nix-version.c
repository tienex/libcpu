#include <stdio.h>
#include <stdlib.h>
#include "nix-version.h"

nix_version_t
nix_version_parse (char const *s)
{
	unsigned long major = 0, minor = 0, patch = 0;
	char         *end;

	if (s == NULL || *s == '\0') {
		return NIX_VERSION_NONE;
	}

	major = strtoul (s, &end, 10);
	if (*end == '.') {
		minor = strtoul (end + 1, &end, 10);
	}
	if (*end == '.') {
		patch = strtoul (end + 1, &end, 10);
	}
	return NIX_VERSION ((unsigned) major, (unsigned) minor, (unsigned) patch);
}

char *
nix_version_format (nix_version_t v, char *buf, size_t buflen)
{
	unsigned major = (unsigned) ((v >> NIX_VERSION_MAJOR_SHIFT) & NIX_VERSION_MAJOR_MASK);
	unsigned minor = (unsigned) ((v >> NIX_VERSION_MINOR_SHIFT) & NIX_VERSION_COMPONENT_MASK);
	unsigned patch = (unsigned) (v & NIX_VERSION_COMPONENT_MASK);

	snprintf (buf, buflen, "%u.%u.%u", major, minor, patch);
	return buf;
}
