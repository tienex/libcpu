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
#include "xec-mmap.h"

#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdint.h>

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

	/* Time op -- exercises the nix-time host layer (gettimeofday). */
	struct nix_timeval tv;
	memset (&tv, 0, sizeof (tv));
	int tr = nix_gettimeofday (&tv, NULL, env);
	int timeok = (tr == 0) && (tv.tv_sec > 1000000000);   /* a plausible wall clock (after 2001) */

	/* Host-identity op -- exercises the nix-hostinfo host layer (gethostname). */
	char host[256];
	memset (host, 0, sizeof (host));
	int hr = nix_gethostname (host, sizeof (host), env);
	int hostok = (hr == 0) && (host[0] != '\0');           /* a real machine name, non-empty */

	/* Credentials -- exercises the nix-cred host layer (uid/gid identity + setid semantics). */
	int uid = nix_getuid (env);
	int gid = nix_getgid (env);
	int seteu = nix_seteuid ((nix_uid_t) nix_geteuid (env), env);   /* re-set to self: must succeed */
	int credok = (uid >= 0) && (gid >= 0) && (nix_geteuid (env) == uid) && (seteu == 0);

	/* Memory ops -- exercises the nix-mem host layer. brk is nosys everywhere (returns the all-ones
	   sentinel + ENOSYS); mprotect runs against a real page obtained portably from xec_mmap_create
	   (VirtualAlloc on win32, mmap on POSIX), validating the win32 PAGE_* translation. */
	uintmax_t brk = nix_brk ((uintmax_t) 0x1000, env);
	xec_mmap_t *mm = xec_mmap_create (4096, XEC_MMAP_READ | XEC_MMAP_WRITE);
	int mp = -1;
	if (mm != NULL) {
		mp = nix_mprotect ((uintmax_t) (uintptr_t) xec_mmap_get_bytes (mm), 4096, NIX_PROT_READ, env);
		xec_mmap_free (mm);
	}
	int memok = (brk == (uintmax_t) -1) && (mm != NULL) && (mp == 0);

	/* POSIX realtime clock -- exercises the nix-rt-time host layer (clock_gettime, or its
	   gettimeofday fallback where the host lacks clock_gettime). */
	struct nix_timespec ts;
	memset (&ts, 0, sizeof (ts));
	int cr = nix_rt_clock_gettime (NIX_CLOCK_REALTIME, &ts, env);
	int clockok = (cr == 0) && (ts.tv_sec > 1000000000);   /* plausible wall clock (after 2001) */

	/* SysV IPC -- exercises nix-s5-* (no analog off POSIX: nosys everywhere) -- and the realtime
	   scheduler yield from nix-rt-process (advisory; succeeds even where it is a no-op). */
	int ipc = nix_s5_msgget ((nix_key_t) 1, 0, env);
	int yld = nix_rt_sched_yield (env);
	int rtok = (ipc == -1) && (yld == 0);

	/* Process ops -- exercises nix-process. getpid/getppid are real; fork has no analog in the
	   single-process model and must report the nosys sentinel. */
	int pid  = nix_getpid (env);
	int ppid = nix_getppid (env);
	int frk  = nix_fork (env);
	int procok = (pid > 0) && (ppid > 0) && (frk == -1);

	/* Signal dispositions -- exercises nix-signal's guest-side core (host-independent): install a
	   handler via sigaction, read it back, and round-trip the process signal mask. */
	nix_signal_init (32);
	struct nix_sigaction sa, osa;
	memset (&sa, 0, sizeof (sa));
	memset (&osa, 0, sizeof (osa));
	sa.__sa_handler = 0x1234;
	int sga = nix_sigaction (2, &sa, NULL, env);    /* install */
	int sgb = nix_sigaction (2, &sa, &osa, env);    /* reinstall, recovering the previous handler */
	nix_sigset_t oldmask = 0, newmask = 0x5;
	/* nix_sigprocmask reports success via env errno (==0), which the obsd41 dispatcher clears at
	   each syscall entry; model that boundary here since this harness calls primitives directly. */
	nix_env_set_errno (env, 0);
	int spm = nix_sigprocmask (NIX_SIG_SETMASK, &newmask, &oldmask, env);
	int sigok = (sga == 0) && (sgb == 0) && (osa.__sa_handler == 0x1234) && (spm == 0);

	int ok = filok && dirok && timeok && hostok && credok && memok && clockok && rtok && procok && sigok;

	printf ("  write=%lld read=%lld fstat=%d size=%lld data='%.*s'  mkdir=%d rmdir=%d  gettimeofday=%d sec=%lld  gethostname=%d host='%s'  uid=%d gid=%d seteuid(self)=%d  brk=%s mprotect=%d  clock_gettime=%d sec=%lld\n",
	        (long long) wrote, (long long) got, sr, (long long) st.st_size, (int) len, buf, mk, rm,
	        tr, (long long) tv.tv_sec, hr, host, uid, gid, seteu,
	        brk == (uintmax_t) -1 ? "ENOSYS" : "?", mp, cr, (long long) ts.tv_sec);
	printf ("  msgget=%d (nosys expected -1)  sched_yield=%d  pid=%d ppid=%d fork=%d  sigaction=%d/%d handler=0x%llx sigprocmask=%d\n",
	        ipc, yld, pid, ppid, frk, sga, sgb, (unsigned long long) osa.__sa_handler, spm);
	printf ("RESULT: %s\n", ok ? "PASS" : "FAIL");
	return ok ? 0 : 1;
}
