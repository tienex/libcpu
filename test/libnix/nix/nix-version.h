#ifndef __nix_version_h
#define __nix_version_h

/*
 * A guest-OS ABI version, packed so it orders numerically: MAJOR in bits 16..31,
 * MINOR in bits 8..15, PATCH in bits 0..7.  This lets the syscall dispatcher gate a
 * call with a single unsigned comparison against a [since, until) range.
 */
#include "nix-host.h" /* the base integer types (uint32_t, size_t) */

typedef uint32_t nix_version_t;

#define NIX_VERSION_MAJOR_SHIFT    16
#define NIX_VERSION_MINOR_SHIFT    8
#define NIX_VERSION_COMPONENT_MASK 0xFFu
#define NIX_VERSION_MAJOR_MASK     0xFFFFu

#define NIX_VERSION(maj, min, pat)                                                       \
	(((nix_version_t) ((maj) & NIX_VERSION_MAJOR_MASK) << NIX_VERSION_MAJOR_SHIFT)        \
	 | ((nix_version_t) ((min) & NIX_VERSION_COMPONENT_MASK) << NIX_VERSION_MINOR_SHIFT)  \
	 | ((nix_version_t) ((pat) & NIX_VERSION_COMPONENT_MASK)))

/* "From the beginning" / unset floor -- the smallest possible version. */
#define NIX_VERSION_NONE   ((nix_version_t) 0)
/* Newer than any real release -- an unversioned run gates nothing. */
#define NIX_VERSION_LATEST (~(nix_version_t) 0)

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Parse "MAJOR.MINOR[.PATCH]" (e.g. "7.9", "10.1", "1.6.2"); components past PATCH are
 * ignored.  Returns NIX_VERSION_NONE for a NULL or empty string.
 */
nix_version_t nix_version_parse (char const *s);

/* Format v into buf ("MAJOR.MINOR.PATCH"); buf must hold >= 16 bytes.  Returns buf. */
char *nix_version_format (nix_version_t v, char *buf, size_t buflen);

#ifdef __cplusplus
}
#endif

#endif /* !__nix_version_h */
