/** @file
  Standalone host-op conformance test for libnix's `nix_` layer.

  libnix sits between a guest's syscalls and the host OS: the `obsd41` personality
  translates guest arguments and calls the portable `nix_` primitives, which in turn
  call the host (POSIX today; win32/haiku/os2/vms to come). This test drives those
  portable primitives directly -- open a real host file, write, reopen, read it back,
  fstat it -- with NO CPU, monitor or guest binary in the loop. It is the regression
  vehicle for the host layer: every host backend must make it pass, so the portability
  refactor can be validated without the (legacy-LLVM) emulation core.

  Copyright (c) the LibCPU developers. Distributed under the 2-clause BSD license.
**/

#include "nix.h"

#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

extern void xec_init (void);                  /* brings up the xec log + memory subsystems */

int
main (void)
{
	xec_init ();
	nix_env_t *env = nix_env_create (NULL);   /* file ops use host pointers; no guest memory needed */
	if (env == NULL) {
		printf ("RESULT: FAIL (nix_env_create)\n");
		return 1;
	}

	char const *path = "/tmp/nixtest.libnix.tmp";
	char const *msg  = "libnix-host-ok";
	size_t      len  = strlen (msg);

	/* Create + write. */
	int fd = nix_open (path, O_WRONLY | O_CREAT | O_TRUNC, 0644, env);
	if (fd < 0) {
		printf ("RESULT: FAIL (open-write errno=%lld)\n", (long long) nix_env_get_errno (env));
		return 1;
	}
	nix_ssize_t wrote = nix_write (fd, msg, len, env);
	nix_close (fd, env);

	/* Reopen read-only + read back. */
	char buf[64];
	memset (buf, 0, sizeof (buf));
	int fd2 = nix_open (path, O_RDONLY, 0, env);
	if (fd2 < 0) {
		printf ("RESULT: FAIL (open-read errno=%lld)\n", (long long) nix_env_get_errno (env));
		return 1;
	}
	nix_ssize_t got = nix_read (fd2, buf, sizeof (buf), env);

	/* fstat the read handle for the size. */
	struct nix_stat st;
	memset (&st, 0, sizeof (st));
	int sr = nix_fstat (fd2, &st, env);

	nix_close (fd2, env);
	remove (path);

	int filok = (wrote == (nix_ssize_t) len)
	         && (got == (nix_ssize_t) len)
	         && (memcmp (buf, msg, len) == 0)
	         && (sr == 0)
	         && (st.st_size == (nix_off_t) len);

	/* Directory ops (create + remove) -- exercises the nix-dir host layer. */
	char const *dir = "/tmp/nixtest.libnix.dir";
	nix_rmdir (dir, env);                          /* clear any leftover */
	int mk  = nix_mkdir (dir, 0755, env);
	int rm  = nix_rmdir (dir, env);
	int dirok = (mk == 0) && (rm == 0);

	int ok = filok && dirok;

	printf ("  write=%lld read=%lld fstat=%d size=%lld data='%.*s'  mkdir=%d rmdir=%d\n",
	        (long long) wrote, (long long) got, sr, (long long) st.st_size, (int) len, buf, mk, rm);
	printf ("RESULT: %s\n", ok ? "PASS" : "FAIL");
	return ok ? 0 : 1;
}
