/*
 * OpenBSD 7.9 System Calls Implementation
 * Copyright (C) 2007 Orlando Bassotto. All rights reserved.
 */
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "nix-host.h"
#include "nix-byte-order.h"
#include "nix-host.h"
#include "LibCPU/LcLog.h"
#include "nix-syscall.h"
#include "nix-syscall.h"
#include "openbsd-syscalls.h"
#include "openbsd-us-syscall-priv.h"

#include "nix.h"
#include "nix-fd.h"
#include "openbsd.h"
#include "arc4random.h"

#include "openbsd-args.h"
#include "openbsd-sysctl.h"
#include "openbsd-mman.h"

#include "nix-host.h" /* XXX */

#define GE32(gi, x) \
	(((gi)->endian != NIX_ENDIAN_NATIVE) ? nix_byte_swap_int32(x) : (x))

#define GE64(gi, x) \
	(((gi)->endian != NIX_ENDIAN_NATIVE) ? nix_byte_swap_int64(x) : (x))

void *g_bsd_log = NULL;

static __inline struct nix_iovec *
__openbsd_iovec32_copy_from(nix_env_t              *env,
                           nix_mem_if_t           *mem,
                           nix_guest_info_t const *gi,
                           struct openbsd_iovec32  *iov,
                           size_t                  niov)
{
	struct nix_iovec *xiov = NULL;

	__nix_try
	{
		xiov = nix_alloc_ntype(struct nix_iovec, niov, 0);
		if (xiov == NULL)
			nix_env_set_errno(env, ENOMEM);
		else {
			size_t n;

			for (n = 0; n < niov; n++) {
				uintptr_t    pa;
				nix_memflg_t mf = 0;

				pa = nix_mem_gtoh(mem, GE32(gi, iov[n].iov_base), &mf);
				if (mf != 0) {
					nix_env_set_errno(env, EFAULT);
					nix_free(xiov);
					xiov = NULL;
					break;
				}
				xiov[n].iov_base = (void *)pa;
				xiov[n].iov_len = GE32(gi, iov[n].iov_len);
			}
		}
	}
	__nix_catch_any
	{
		nix_env_set_errno(env, EFAULT);
		if (xiov != NULL)
			nix_free(xiov);
		xiov = NULL;
	}
	__nix_end_try

	    return (xiov);
}

static __inline struct nix_pollfd *
__openbsd_pollfd_copy_from(nix_env_t              *env,
                          nix_guest_info_t const *gi,
                          struct openbsd_pollfd   *fds,
                          size_t                  nfds)
{
	struct nix_pollfd *xfds = NULL;

	__nix_try
	{
		xfds = nix_alloc_ntype(struct nix_pollfd, nfds, 0);
		if (xfds == NULL)
			nix_env_set_errno(env, ENOMEM);
		else {
			size_t n;

			for (n = 0; n < nfds; n++)
				openbsd_pollfd_to_nix_pollfd(gi->endian, fds + n, xfds + n);
		}
	}
	__nix_catch_any
	{
		nix_env_set_errno(env, EFAULT);
		if (xfds != NULL)
			nix_free(xfds);
		xfds = NULL;
	}
	__nix_end_try

	    return (xfds);
}

int
openbsd_exit(
    nix_us_syscall_if_t      *xus,
    nix_monitor_t            *xmon,
    openbsd_exit_args_t const *args,
    openbsd_exit_result_t     *result)
{
	LCLog(g_bsd_log, LCLogDebug, 0, "invoked", 0);

	nix_exit(args->arg0);
	return (0); /* Not reached (should)! */
}

int
openbsd_fork(
    nix_us_syscall_if_t      *xus,
    nix_monitor_t            *xmon,
    openbsd_fork_args_t const *args,
    openbsd_fork_result_t     *result)
{
	nix_env_t *env = openbsd_us_syscall_get_nix_env(xus);

	LCLog(g_bsd_log, LCLogDebug, 0, "invoked", 0);

	nix_env_set_errno(env, 0);
	*result = nix_fork(env);
	return (nix_env_get_errno(env));
}

int
openbsd_read(
    nix_us_syscall_if_t      *xus,
    nix_monitor_t            *xmon,
    openbsd_read_args_t const *args,
    openbsd_read_result_t     *result)
{
	nix_env_t *env = openbsd_us_syscall_get_nix_env(xus);

	LCLog(g_bsd_log, LCLogDebug, 0, "invoked", 0);

	nix_env_set_errno(env, 0);
	*result = nix_read(args->arg0, args->arg1, args->arg2, env);
	return (nix_env_get_errno(env));
}

int
openbsd_write(
    nix_us_syscall_if_t       *xus,
    nix_monitor_t             *xmon,
    openbsd_write_args_t const *args,
    openbsd_write_result_t     *result)
{
	nix_env_t *env = openbsd_us_syscall_get_nix_env(xus);

	LCLog(g_bsd_log, LCLogDebug, 0, "invoked", 0);

	nix_env_set_errno(env, 0);
	*result = nix_write(args->arg0, args->arg1, args->arg2, env);
	return (nix_env_get_errno(env));
}

int
openbsd_open(
    nix_us_syscall_if_t      *xus,
    nix_monitor_t            *xmon,
    openbsd_open_args_t const *args,
    openbsd_open_result_t     *result)
{
	nix_env_t *env = openbsd_us_syscall_get_nix_env(xus);

	LCLog(g_bsd_log, LCLogDebug, 0, "invoked", 0);

	nix_env_set_errno(env, 0);
	*result = nix_open((char const *)args->arg0, args->arg1, args->arg2, env);
	return (nix_env_get_errno(env));
}

int
openbsd_close(
    nix_us_syscall_if_t       *xus,
    nix_monitor_t             *xmon,
    openbsd_close_args_t const *args,
    openbsd_close_result_t     *result)
{
	nix_env_t *env = openbsd_us_syscall_get_nix_env(xus);

	LCLog(g_bsd_log, LCLogDebug, 0, "invoked", 0);

	nix_env_set_errno(env, 0);
	*result = nix_close(args->arg0, env);
	return (nix_env_get_errno(env));
}

int
openbsd_wait4(
    nix_us_syscall_if_t       *xus,
    nix_monitor_t             *xmon,
    openbsd_wait4_args_t const *args,
    openbsd_wait4_result_t     *result)
{
	nix_env_t *env = openbsd_us_syscall_get_nix_env(xus);

	LCLog(g_bsd_log, LCLogDebug, 0, "invoked", 0);

	nix_env_set_errno(env, 0);
	*result = nix_bsd_wait4(args->arg0, args->arg1, args->arg2, (struct nix_rusage *)(uintptr_t)args->arg3, env);
	return (nix_env_get_errno(env));
}

int
openbsd_link(
    nix_us_syscall_if_t      *xus,
    nix_monitor_t            *xmon,
    openbsd_link_args_t const *args,
    openbsd_link_result_t     *result)
{
	nix_env_t *env = openbsd_us_syscall_get_nix_env(xus);

	LCLog(g_bsd_log, LCLogDebug, 0, "invoked", 0);

	nix_env_set_errno(env, 0);
	*result = nix_link((char const *)args->arg0, (char const *)args->arg1, env);
	return (nix_env_get_errno(env));
}

int
openbsd_unlink(
    nix_us_syscall_if_t        *xus,
    nix_monitor_t              *xmon,
    openbsd_unlink_args_t const *args,
    openbsd_unlink_result_t     *result)
{
	nix_env_t *env = openbsd_us_syscall_get_nix_env(xus);

	LCLog(g_bsd_log, LCLogDebug, 0, "invoked", 0);

	nix_env_set_errno(env, 0);
	*result = nix_unlink((char const *)args->arg0, env);
	return (nix_env_get_errno(env));
}

int
openbsd_chdir(
    nix_us_syscall_if_t       *xus,
    nix_monitor_t             *xmon,
    openbsd_chdir_args_t const *args,
    openbsd_chdir_result_t     *result)
{
	nix_env_t *env = openbsd_us_syscall_get_nix_env(xus);

	LCLog(g_bsd_log, LCLogDebug, 0, "invoked", 0);

	nix_env_set_errno(env, 0);
	*result = nix_chdir((char const *)args->arg0, env);
	return (nix_env_get_errno(env));
}

int
openbsd_fchdir(
    nix_us_syscall_if_t        *xus,
    nix_monitor_t              *xmon,
    openbsd_fchdir_args_t const *args,
    openbsd_fchdir_result_t     *result)
{
	nix_env_t *env = openbsd_us_syscall_get_nix_env(xus);

	LCLog(g_bsd_log, LCLogDebug, 0, "invoked", 0);

	nix_env_set_errno(env, 0);
	*result = nix_fchdir(args->arg0, env);
	return (nix_env_get_errno(env));
}

int
openbsd_mknod(
    nix_us_syscall_if_t       *xus,
    nix_monitor_t             *xmon,
    openbsd_mknod_args_t const *args,
    openbsd_mknod_result_t     *result)
{
	nix_env_t *env = openbsd_us_syscall_get_nix_env(xus);

	LCLog(g_bsd_log, LCLogDebug, 0, "invoked", 0);

	nix_env_set_errno(env, 0);
	*result = nix_mknod((char const *)args->arg0, args->arg1, args->arg2, env);
	return (nix_env_get_errno(env));
}

int
openbsd_chmod(
    nix_us_syscall_if_t       *xus,
    nix_monitor_t             *xmon,
    openbsd_chmod_args_t const *args,
    openbsd_chmod_result_t     *result)
{
	nix_env_t *env = openbsd_us_syscall_get_nix_env(xus);

	LCLog(g_bsd_log, LCLogDebug, 0, "invoked", 0);

	nix_env_set_errno(env, 0);
	__nix_try
	{
		*result = nix_chmod((char const *)args->arg0, args->arg1, env);
	}
	__nix_catch_any
	{
		nix_env_set_errno(env, EFAULT);
		*result = -1;
	}
	__nix_end_try

	    return (nix_env_get_errno(env));
}

int
openbsd_chown(
    nix_us_syscall_if_t       *xus,
    nix_monitor_t             *xmon,
    openbsd_chown_args_t const *args,
    openbsd_chown_result_t     *result)
{
	nix_env_t *env = openbsd_us_syscall_get_nix_env(xus);

	LCLog(g_bsd_log, LCLogDebug, 0, "invoked", 0);

	nix_env_set_errno(env, 0);
	__nix_try
	{
		*result = nix_chown((char const *)args->arg0, args->arg1, args->arg2, env);
	}
	__nix_catch_any
	{
		nix_env_set_errno(env, EFAULT);
		*result = -1;
	}
	__nix_end_try

	    return (nix_env_get_errno(env));
}

int
openbsd_getpid(
    nix_us_syscall_if_t        *xus,
    nix_monitor_t              *xmon,
    openbsd_getpid_args_t const *args,
    openbsd_getpid_result_t     *result)
{
	nix_env_t *env = openbsd_us_syscall_get_nix_env(xus);

	LCLog(g_bsd_log, LCLogDebug, 0, "invoked", 0);

	nix_env_set_errno(env, 0);
	*result = nix_getpid(env);
	return (nix_env_get_errno(env));
}

int
openbsd_mount(
    nix_us_syscall_if_t       *xus,
    nix_monitor_t             *xmon,
    openbsd_mount_args_t const *args,
    openbsd_mount_result_t     *result)
{
	nix_env_t *env = openbsd_us_syscall_get_nix_env(xus);

	LCLog(g_bsd_log, LCLogDebug, 0, "invoked", 0);

	nix_env_set_errno(env, 0);
	*result = nix_bsd_mount((char const *)args->arg0,
	                        (char const *)args->arg1,
	                        args->arg2,
	                        args->arg3,
	                        env);
	return (nix_env_get_errno(env));
}

int
openbsd_setuid(
    nix_us_syscall_if_t        *xus,
    nix_monitor_t              *xmon,
    openbsd_setuid_args_t const *args,
    openbsd_setuid_result_t     *result)
{
	nix_env_t *env = openbsd_us_syscall_get_nix_env(xus);

	LCLog(g_bsd_log, LCLogDebug, 0, "invoked", 0);

	nix_env_set_errno(env, 0);
	*result = nix_setuid(args->arg0, env);
	return (nix_env_get_errno(env));
}

int
openbsd_getuid(
    nix_us_syscall_if_t        *xus,
    nix_monitor_t              *xmon,
    openbsd_getuid_args_t const *args,
    openbsd_getuid_result_t     *result)
{
	nix_env_t *env = openbsd_us_syscall_get_nix_env(xus);

	LCLog(g_bsd_log, LCLogDebug, 0, "invoked", 0);

	nix_env_set_errno(env, 0);
	*result = nix_getuid(env);
	return (nix_env_get_errno(env));
}

int
openbsd_geteuid(
    nix_us_syscall_if_t         *xus,
    nix_monitor_t               *xmon,
    openbsd_geteuid_args_t const *args,
    openbsd_geteuid_result_t     *result)
{
	nix_env_t *env = openbsd_us_syscall_get_nix_env(xus);

	LCLog(g_bsd_log, LCLogDebug, 0, "invoked", 0);

	nix_env_set_errno(env, 0);
	*result = nix_geteuid(env);
	return (nix_env_get_errno(env));
}

int
openbsd_ptrace(
    nix_us_syscall_if_t        *xus,
    nix_monitor_t              *xmon,
    openbsd_ptrace_args_t const *args,
    openbsd_ptrace_result_t     *result)
{
	nix_env_t *env = openbsd_us_syscall_get_nix_env(xus);

	LCLog(g_bsd_log, LCLogDebug, 0, "invoked", 0);

	nix_env_set_errno(env, 0);
	*result = nix_ptrace(args->arg0, args->arg1, (void *)(uintptr_t)args->arg2, args->arg3, env);
	return (nix_env_get_errno(env));
}

int
openbsd_recvmsg(
    nix_us_syscall_if_t         *xus,
    nix_monitor_t               *xmon,
    openbsd_recvmsg_args_t const *args,
    openbsd_recvmsg_result_t     *result)
{
	LCLog(g_bsd_log, LCLogDebug, 0, "invoked", 0);

	/* Insert here implementation code */

	return (ENOSYS);
}

int
openbsd_sendmsg(
    nix_us_syscall_if_t         *xus,
    nix_monitor_t               *xmon,
    openbsd_sendmsg_args_t const *args,
    openbsd_sendmsg_result_t     *result)
{
	LCLog(g_bsd_log, LCLogDebug, 0, "invoked", 0);

	/* Insert here implementation code */

	return (ENOSYS);
}

int
openbsd_recvfrom(
    nix_us_syscall_if_t          *xus,
    nix_monitor_t                *xmon,
    openbsd_recvfrom_args_t const *args,
    openbsd_recvfrom_result_t     *result)
{
	nix_guest_info_t     gi;
	struct nix_sockaddr  sa;
	int                  rc = -1;
	nix_socklen_t        salen = sizeof(sa);
	struct nix_sockaddr *psa = args->arg4 != NULL ? &sa : NULL;
	nix_socklen_t       *psalen = args->arg4 != NULL ? &salen : NULL;
	nix_env_t           *env = openbsd_us_syscall_get_nix_env(xus);

	LCLog(g_bsd_log, LCLogDebug, 0, "invoked", 0);

	nix_monitor_get_guest_info(xmon, &gi);

	if (args->arg1 == NULL)
		return (EFAULT);

	if (args->arg2 == 0) {
		*result = 0;
		return (0);
	}

	if (rc == 0) {
		nix_env_set_errno(env, 0);
		*result = nix_recvfrom(args->arg0, args->arg1, args->arg2, args->arg3, psa, psalen, env);
	} else {
		*result = -1;
	}

	if (psa != NULL) {
		__nix_try
		{
			if (!nix_sockaddr_to_openbsd_sockaddr(gi.endian, psa, *psalen,
			                                     args->arg4, (openbsd_socklen_t *)args->arg5)) {
				nix_env_set_errno(env, EINVAL);
				rc = -1;
			}
		}
		__nix_catch_any
		{
			nix_env_set_errno(env, EFAULT);
			rc = -1;
		}
		__nix_end_try
	}

	return (nix_env_get_errno(env));
}

int
openbsd_accept(
    nix_us_syscall_if_t        *xus,
    nix_monitor_t              *xmon,
    openbsd_accept_args_t const *args,
    openbsd_accept_result_t     *result)
{
	LCLog(g_bsd_log, LCLogDebug, 0, "invoked", 0);

	/* Insert here implementation code */

	return (ENOSYS);
}

int
openbsd_getpeername(
    nix_us_syscall_if_t             *xus,
    nix_monitor_t                   *xmon,
    openbsd_getpeername_args_t const *args,
    openbsd_getpeername_result_t     *result)
{
	nix_guest_info_t    gi;
	struct nix_sockaddr sa;
	nix_socklen_t       salen = sizeof(sa);
	nix_env_t          *env = openbsd_us_syscall_get_nix_env(xus);

	LCLog(g_bsd_log, LCLogDebug, 0, "invoked", 0);

	if (args->arg1 == NULL)
		return (EFAULT);

	nix_monitor_get_guest_info(xmon, &gi);

	nix_env_set_errno(env, 0);
	*result = nix_getpeername(args->arg0, &sa, &salen, env);
	if (nix_env_get_errno(env))
		return (nix_env_get_errno(env));

	__nix_try
	{
		if (!nix_sockaddr_to_openbsd_sockaddr(gi.endian, &sa, salen,
		                                     args->arg1, args->arg2)) {
			nix_env_set_errno(env, EINVAL);
		}
	}
	__nix_catch_any
	{
		nix_env_set_errno(env, EFAULT);
	}
	__nix_end_try

	    return (nix_env_get_errno(env));
}

int
openbsd_getsockname(
    nix_us_syscall_if_t             *xus,
    nix_monitor_t                   *xmon,
    openbsd_getsockname_args_t const *args,
    openbsd_getsockname_result_t     *result)
{
	nix_guest_info_t    gi;
	struct nix_sockaddr sa;
	nix_socklen_t       salen = sizeof(sa);
	nix_env_t          *env = openbsd_us_syscall_get_nix_env(xus);

	LCLog(g_bsd_log, LCLogDebug, 0, "invoked", 0);

	if (args->arg1 == NULL)
		return (EFAULT);

	nix_monitor_get_guest_info(xmon, &gi);

	nix_env_set_errno(env, 0);
	*result = nix_getsockname(args->arg0, &sa, &salen, env);
	if (nix_env_get_errno(env))
		return (nix_env_get_errno(env));

	__nix_try
	{
		if (!nix_sockaddr_to_openbsd_sockaddr(gi.endian, &sa, salen, args->arg1, args->arg2)) {
			nix_env_set_errno(env, EINVAL);
		}
	}
	__nix_catch_any
	{
		nix_env_set_errno(env, EFAULT);
	}
	__nix_end_try

	    return (nix_env_get_errno(env));
}

int
openbsd_access(
    nix_us_syscall_if_t        *xus,
    nix_monitor_t              *xmon,
    openbsd_access_args_t const *args,
    openbsd_access_result_t     *result)
{
	nix_env_t *env = openbsd_us_syscall_get_nix_env(xus);

	LCLog(g_bsd_log, LCLogDebug, 0, "invoked", 0);

	nix_env_set_errno(env, 0);
	*result = nix_access((char const *)args->arg0, args->arg1, env);
	return (nix_env_get_errno(env));
}

int
openbsd_chflags(
    nix_us_syscall_if_t         *xus,
    nix_monitor_t               *xmon,
    openbsd_chflags_args_t const *args,
    openbsd_chflags_result_t     *result)
{
	nix_env_t *env = openbsd_us_syscall_get_nix_env(xus);

	LCLog(g_bsd_log, LCLogDebug, 0, "invoked", 0);

	nix_env_set_errno(env, 0);
	*result = nix_bsd_chflags((char const *)args->arg0, args->arg1, env);
	return (nix_env_get_errno(env));
}

int
openbsd_fchflags(
    nix_us_syscall_if_t          *xus,
    nix_monitor_t                *xmon,
    openbsd_fchflags_args_t const *args,
    openbsd_fchflags_result_t     *result)
{
	nix_env_t *env = openbsd_us_syscall_get_nix_env(xus);

	LCLog(g_bsd_log, LCLogDebug, 0, "invoked", 0);

	nix_env_set_errno(env, 0);
	*result = nix_bsd_fchflags(args->arg0, args->arg1, env);
	return (nix_env_get_errno(env));
}

int
openbsd_sync(
    nix_us_syscall_if_t      *xus,
    nix_monitor_t            *xmon,
    openbsd_sync_args_t const *args,
    openbsd_sync_result_t     *result)
{
	nix_env_t *env = openbsd_us_syscall_get_nix_env(xus);

	LCLog(g_bsd_log, LCLogDebug, 0, "invoked", 0);

	nix_env_set_errno(env, 0);
	nix_sync(env);
	return (nix_env_get_errno(env));
}

int
openbsd_kill(
    nix_us_syscall_if_t      *xus,
    nix_monitor_t            *xmon,
    openbsd_kill_args_t const *args,
    openbsd_kill_result_t     *result)
{
	nix_env_t *env = openbsd_us_syscall_get_nix_env(xus);

	LCLog(g_bsd_log, LCLogDebug, 0, "invoked", 0);

	nix_env_set_errno(env, 0);
	*result = nix_kill(args->arg0, args->arg1, env);
	return (nix_env_get_errno(env));
}

int
openbsd_getppid(
    nix_us_syscall_if_t         *xus,
    nix_monitor_t               *xmon,
    openbsd_getppid_args_t const *args,
    openbsd_getppid_result_t     *result)
{
	nix_env_t *env = openbsd_us_syscall_get_nix_env(xus);

	LCLog(g_bsd_log, LCLogDebug, 0, "invoked", 0);

	nix_env_set_errno(env, 0);
	*result = nix_getppid(env);
	return (nix_env_get_errno(env));
}

int
openbsd_dup(
    nix_us_syscall_if_t     *xus,
    nix_monitor_t           *xmon,
    openbsd_dup_args_t const *args,
    openbsd_dup_result_t     *result)
{
	nix_env_t *env = openbsd_us_syscall_get_nix_env(xus);

	LCLog(g_bsd_log, LCLogDebug, 0, "invoked", 0);

	nix_env_set_errno(env, 0);
	*result = nix_dup(args->arg0, env);
	return (nix_env_get_errno(env));
}

int
openbsd_getegid(
    nix_us_syscall_if_t         *xus,
    nix_monitor_t               *xmon,
    openbsd_getegid_args_t const *args,
    openbsd_getegid_result_t     *result)
{
	nix_env_t *env = openbsd_us_syscall_get_nix_env(xus);

	LCLog(g_bsd_log, LCLogDebug, 0, "invoked", 0);

	nix_env_set_errno(env, 0);
	*result = nix_getegid(env);
	return (nix_env_get_errno(env));
}

int
openbsd_profil(
    nix_us_syscall_if_t        *xus,
    nix_monitor_t              *xmon,
    openbsd_profil_args_t const *args,
    openbsd_profil_result_t     *result)
{
	nix_env_t *env = openbsd_us_syscall_get_nix_env(xus);

	LCLog(g_bsd_log, LCLogDebug, 0, "invoked", 0);

	nix_env_set_errno(env, 0);
	*result = nix_bsd_profil(args->arg0, args->arg1, args->arg2, args->arg3, env);
	return (nix_env_get_errno(env));
}

int
openbsd_ktrace(
    nix_us_syscall_if_t        *xus,
    nix_monitor_t              *xmon,
    openbsd_ktrace_args_t const *args,
    openbsd_ktrace_result_t     *result)
{
	nix_env_t *env = openbsd_us_syscall_get_nix_env(xus);

	LCLog(g_bsd_log, LCLogDebug, 0, "invoked", 0);

	nix_env_set_errno(env, 0);
	*result = nix_bsd_ktrace(args->arg0, args->arg1, args->arg2, args->arg3, env);
	return (nix_env_get_errno(env));
}

int
openbsd_sigaction(
    nix_us_syscall_if_t           *xus,
    nix_monitor_t                 *xmon,
    openbsd_sigaction_args_t const *args,
    openbsd_sigaction_result_t     *result)
{
	nix_guest_info_t      gi;
	struct nix_sigaction  sa;
	struct nix_sigaction  osa;
	struct nix_sigaction *psa = args->arg1 != NULL ? &sa : NULL;
	struct nix_sigaction *posa = args->arg2 != NULL ? &osa : NULL;
	nix_env_t            *env = openbsd_us_syscall_get_nix_env(xus);

	LCLog(g_bsd_log, LCLogDebug, 0, "invoked", 0);

	if (psa == NULL && posa == NULL)
		return (EINVAL);

	nix_monitor_get_guest_info(xmon, &gi);

	if (psa != NULL) {
		__nix_try
		{
			openbsd_sigaction32_to_nix_sigaction(gi.endian, args->arg1, psa);
		}
		__nix_catch_any
		{
			return (EFAULT);
		}
		__nix_end_try
	}

	*result = nix_sigaction(args->arg0, psa, posa, env);
	if (nix_env_get_errno(env) != 0)
		return (nix_env_get_errno(env));

	if (posa != NULL) {
		__nix_try
		{
			nix_sigaction_to_openbsd_sigaction32(gi.endian, posa, args->arg2);
		}
		__nix_catch_any
		{
			return (EFAULT);
		}
		__nix_end_try
	}

	return (0);
}

int
openbsd_getgid(
    nix_us_syscall_if_t        *xus,
    nix_monitor_t              *xmon,
    openbsd_getgid_args_t const *args,
    openbsd_getgid_result_t     *result)
{
	nix_env_t *env = openbsd_us_syscall_get_nix_env(xus);

	LCLog(g_bsd_log, LCLogDebug, 0, "invoked", 0);

	nix_env_set_errno(env, 0);
	*result = nix_getgid(env);
	return (nix_env_get_errno(env));
}

int
openbsd_sigprocmask(
    nix_us_syscall_if_t             *xus,
    nix_monitor_t                   *xmon,
    openbsd_sigprocmask_args_t const *args,
    openbsd_sigprocmask_result_t     *result)
{
	int          howto;
	nix_sigset_t oset;
	nix_sigset_t nset = args->arg1;
	nix_env_t   *env = openbsd_us_syscall_get_nix_env(xus);

	LCLog(g_bsd_log, LCLogDebug, 0, "invoked", 0);

	nix_env_set_errno(env, 0);
	switch (args->arg0) {
	case OPENBSD_SIG_BLOCK:
		howto = NIX_SIG_BLOCK;
		break;
	case OPENBSD_SIG_UNBLOCK:
		howto = NIX_SIG_UNBLOCK;
		break;
	case OPENBSD_SIG_SETMASK:
		howto = NIX_SIG_SETMASK;
		break;
	default:
		return (EINVAL);
	}

	nix_sigprocmask(howto, &nset, &oset, env);
	*result = oset;

	return (nix_env_get_errno(env));
}

int
openbsd_setlogin(
    nix_us_syscall_if_t          *xus,
    nix_monitor_t                *xmon,
    openbsd_setlogin_args_t const *args,
    openbsd_setlogin_result_t     *result)
{
	nix_env_t *env = openbsd_us_syscall_get_nix_env(xus);

	LCLog(g_bsd_log, LCLogDebug, 0, "invoked", 0);

	nix_env_set_errno(env, 0);
	*result = nix_bsd_setlogin((char const *)args->arg0, env);
	return (nix_env_get_errno(env));
}

int
openbsd_acct(
    nix_us_syscall_if_t      *xus,
    nix_monitor_t            *xmon,
    openbsd_acct_args_t const *args,
    openbsd_acct_result_t     *result)
{
	nix_env_t *env = openbsd_us_syscall_get_nix_env(xus);

	LCLog(g_bsd_log, LCLogDebug, 0, "invoked", 0);

	nix_env_set_errno(env, 0);
	*result = nix_bsd_acct((char const *)args->arg0, env);
	return (nix_env_get_errno(env));
}

int
openbsd_sigpending(
    nix_us_syscall_if_t            *xus,
    nix_monitor_t                  *xmon,
    openbsd_sigpending_args_t const *args,
    openbsd_sigpending_result_t     *result)
{
	nix_env_t *env = openbsd_us_syscall_get_nix_env(xus);

	LCLog(g_bsd_log, LCLogDebug, 0, "invoked", 0);

	nix_env_set_errno(env, 0);
	*result = 0; /* XXX */
	return (nix_env_get_errno(env));
}

int
openbsd_ioctl(
    nix_us_syscall_if_t       *xus,
    nix_monitor_t             *xmon,
    openbsd_ioctl_args_t const *args,
    openbsd_ioctl_result_t     *result)
{
	nix_guest_info_t gi;
	nix_env_t       *env = openbsd_us_syscall_get_nix_env(xus);

	LCLog(g_bsd_log, LCLogDebug, 0, "invoked", 0);

	nix_monitor_get_guest_info(xmon, &gi);

	nix_env_set_errno(env, 0);
	*result = openbsd_ioctl_dispatch(env, gi.endian, args->arg0, args->arg1, args->arg2);
	return (nix_env_get_errno(env));
}

int
openbsd_reboot(
    nix_us_syscall_if_t        *xus,
    nix_monitor_t              *xmon,
    openbsd_reboot_args_t const *args,
    openbsd_reboot_result_t     *result)
{
	nix_env_t *env = openbsd_us_syscall_get_nix_env(xus);

	LCLog(g_bsd_log, LCLogDebug, 0, "invoked", 0);

	nix_env_set_errno(env, 0);
	*result = nix_reboot(args->arg0, env);
	return (nix_env_get_errno(env));
}

int
openbsd_revoke(
    nix_us_syscall_if_t        *xus,
    nix_monitor_t              *xmon,
    openbsd_revoke_args_t const *args,
    openbsd_revoke_result_t     *result)
{
	nix_env_t *env = openbsd_us_syscall_get_nix_env(xus);

	LCLog(g_bsd_log, LCLogDebug, 0, "invoked", 0);

	nix_env_set_errno(env, 0);
	*result = nix_bsd_revoke((char const *)args->arg0, env);
	return (nix_env_get_errno(env));
}

int
openbsd_symlink(
    nix_us_syscall_if_t         *xus,
    nix_monitor_t               *xmon,
    openbsd_symlink_args_t const *args,
    openbsd_symlink_result_t     *result)
{
	nix_env_t *env = openbsd_us_syscall_get_nix_env(xus);

	LCLog(g_bsd_log, LCLogDebug, 0, "invoked", 0);

	nix_env_set_errno(env, 0);
	*result = nix_symlink((char const *)args->arg0, (char const *)args->arg1, env);
	return (nix_env_get_errno(env));
}

int
openbsd_readlink(
    nix_us_syscall_if_t          *xus,
    nix_monitor_t                *xmon,
    openbsd_readlink_args_t const *args,
    openbsd_readlink_result_t     *result)
{
	nix_env_t *env = openbsd_us_syscall_get_nix_env(xus);

	LCLog(g_bsd_log, LCLogDebug, 0, "invoked", 0);

	nix_env_set_errno(env, 0);
	*result = nix_readlink((char *)args->arg0, args->arg1, args->arg2, env);
	return (nix_env_get_errno(env));
}

int
openbsd_execve(
    nix_us_syscall_if_t        *xus,
    nix_monitor_t              *xmon,
    openbsd_execve_args_t const *args,
    openbsd_execve_result_t     *result)
{
	LCLog(g_bsd_log, LCLogDebug, 0, "invoked", 0);

	/* Insert here implementation code */

	return (ENOSYS);
}

int
openbsd_umask(
    nix_us_syscall_if_t       *xus,
    nix_monitor_t             *xmon,
    openbsd_umask_args_t const *args,
    openbsd_umask_result_t     *result)
{
	nix_env_t *env = openbsd_us_syscall_get_nix_env(xus);

	LCLog(g_bsd_log, LCLogDebug, 0, "invoked", 0);

	nix_env_set_errno(env, 0);
	*result = nix_umask(args->arg0, env);
	return (nix_env_get_errno(env));
}

int
openbsd_chroot(
    nix_us_syscall_if_t        *xus,
    nix_monitor_t              *xmon,
    openbsd_chroot_args_t const *args,
    openbsd_chroot_result_t     *result)
{
	nix_env_t *env = openbsd_us_syscall_get_nix_env(xus);

	LCLog(g_bsd_log, LCLogDebug, 0, "invoked", 0);

	nix_env_set_errno(env, 0);
	*result = nix_chroot((char const *)args->arg0, env);
	return (nix_env_get_errno(env));
}

int
openbsd_vfork(
    nix_us_syscall_if_t       *xus,
    nix_monitor_t             *xmon,
    openbsd_vfork_args_t const *args,
    openbsd_vfork_result_t     *result)
{
	nix_env_t *env = openbsd_us_syscall_get_nix_env(xus);

	LCLog(g_bsd_log, LCLogDebug, 0, "invoked", 0);

	nix_env_set_errno(env, 0);
	*result = nix_vfork(env);
	return (nix_env_get_errno(env));
}

int
openbsd_munmap(
    nix_us_syscall_if_t        *xus,
    nix_monitor_t              *xmon,
    openbsd_munmap_args_t const *args,
    openbsd_munmap_result_t     *result)
{
	nix_env_t *env = openbsd_us_syscall_get_nix_env(xus);

	LCLog(g_bsd_log, LCLogDebug, 0, "invoked", 0);

	nix_env_set_errno(env, 0);
	*result = nix_munmap((uintmax_t)(uintptr_t)args->arg0, args->arg1, env);
	return (nix_env_get_errno(env));
}

int
openbsd_mprotect(
    nix_us_syscall_if_t          *xus,
    nix_monitor_t                *xmon,
    openbsd_mprotect_args_t const *args,
    openbsd_mprotect_result_t     *result)
{
	nix_env_t *env = openbsd_us_syscall_get_nix_env(xus);

	LCLog(g_bsd_log, LCLogDebug, 0, "invoked", 0);

	nix_env_set_errno(env, 0);
	*result = nix_mprotect((uintmax_t)(uintptr_t)args->arg0, args->arg1, args->arg2, env);
	return (nix_env_get_errno(env));
}

int
openbsd_madvise(
    nix_us_syscall_if_t         *xus,
    nix_monitor_t               *xmon,
    openbsd_madvise_args_t const *args,
    openbsd_madvise_result_t     *result)
{
	nix_env_t *env = openbsd_us_syscall_get_nix_env(xus);

	LCLog(g_bsd_log, LCLogDebug, 0, "invoked", 0);

	nix_env_set_errno(env, 0);
	*result = nix_madvise((uintmax_t)(uintptr_t)args->arg0, args->arg1, args->arg2, env);
	return (nix_env_get_errno(env));
}

int
openbsd_getgroups(
    nix_us_syscall_if_t           *xus,
    nix_monitor_t                 *xmon,
    openbsd_getgroups_args_t const *args,
    openbsd_getgroups_result_t     *result)
{
	//	nix_env_t *env = openbsd_us_syscall_get_nix_env(xus);

	LCLog(g_bsd_log, LCLogDebug, 0, "invoked", 0);

	return (ENOSYS);
}

int
openbsd_setgroups(
    nix_us_syscall_if_t           *xus,
    nix_monitor_t                 *xmon,
    openbsd_setgroups_args_t const *args,
    openbsd_setgroups_result_t     *result)
{
	LCLog(g_bsd_log, LCLogDebug, 0, "invoked", 0);

	/* Insert here implementation code */

	return (ENOSYS);
}

int
openbsd_getpgrp(
    nix_us_syscall_if_t         *xus,
    nix_monitor_t               *xmon,
    openbsd_getpgrp_args_t const *args,
    openbsd_getpgrp_result_t     *result)
{
	nix_env_t *env = openbsd_us_syscall_get_nix_env(xus);

	LCLog(g_bsd_log, LCLogDebug, 0, "invoked", 0);

	nix_env_set_errno(env, 0);
	*result = nix_getpgrp(env);
	return (nix_env_get_errno(env));
}

int
openbsd_setpgid(
    nix_us_syscall_if_t         *xus,
    nix_monitor_t               *xmon,
    openbsd_setpgid_args_t const *args,
    openbsd_setpgid_result_t     *result)
{
	nix_env_t *env = openbsd_us_syscall_get_nix_env(xus);

	LCLog(g_bsd_log, LCLogDebug, 0, "invoked", 0);

	nix_env_set_errno(env, 0);
	*result = nix_setpgid(args->arg0, args->arg1, env);
	return (nix_env_get_errno(env));
}

int
openbsd_setitimer(
    nix_us_syscall_if_t           *xus,
    nix_monitor_t                 *xmon,
    openbsd_setitimer_args_t const *args,
    openbsd_setitimer_result_t     *result)
{
	LCLog(g_bsd_log, LCLogDebug, 0, "invoked", 0);

	/* Insert here implementation code */

	return (ENOSYS);
}

int
openbsd_getitimer(
    nix_us_syscall_if_t           *xus,
    nix_monitor_t                 *xmon,
    openbsd_getitimer_args_t const *args,
    openbsd_getitimer_result_t     *result)
{
	LCLog(g_bsd_log, LCLogDebug, 0, "invoked", 0);

	/* Insert here implementation code */

	return (ENOSYS);
}

int
openbsd_dup2(
    nix_us_syscall_if_t      *xus,
    nix_monitor_t            *xmon,
    openbsd_dup2_args_t const *args,
    openbsd_dup2_result_t     *result)
{
	nix_env_t *env = openbsd_us_syscall_get_nix_env(xus);

	LCLog(g_bsd_log, LCLogDebug, 0, "invoked", 0);

	nix_env_set_errno(env, 0);
	*result = nix_dup2(args->arg0, args->arg1, env);
	return (nix_env_get_errno(env));
}

int
openbsd_fcntl(
    nix_us_syscall_if_t       *xus,
    nix_monitor_t             *xmon,
    openbsd_fcntl_args_t const *args,
    openbsd_fcntl_result_t     *result)
{
	nix_env_t *env = openbsd_us_syscall_get_nix_env(xus);

	LCLog(g_bsd_log, LCLogDebug, 0, "invoked", 0);

	nix_env_set_errno(env, 0);
	*result = nix_fcntl(args->arg0, args->arg1, args->arg2, env);
	return (nix_env_get_errno(env));
}

int
openbsd_select(
    nix_us_syscall_if_t        *xus,
    nix_monitor_t              *xmon,
    openbsd_select_args_t const *args,
    openbsd_select_result_t     *result)
{
	nix_guest_info_t       gi;
	struct nix_timeval     ntv;
	nix_fd_set             fds[3];
	struct openbsd_timeval *ptv = args->arg4;
	struct nix_timeval    *pntv = ptv != NULL ? &ntv : NULL;
	nix_env_t             *env = openbsd_us_syscall_get_nix_env(xus);
	nix_fd_set            *pfds[3] = {NULL, NULL, NULL};

	nix_monitor_get_guest_info(xmon, &gi);

	LCLog(g_bsd_log, LCLogDebug, 0, "invoked", 0);

	if (ptv != NULL)
		openbsd_timeval_to_nix_timeval(gi.endian, ptv, pntv);

	__nix_try
	{
		if (args->arg1 != NULL) {
			pfds[0] = &fds[0];
			openbsd_fd_set_to_nix_fd_set(gi.endian, args->arg1, pfds[0]);
		}

		if (args->arg2 != NULL) {
			pfds[1] = &fds[1];
			openbsd_fd_set_to_nix_fd_set(gi.endian, args->arg2, pfds[1]);
		}

		if (args->arg3 != NULL) {
			pfds[2] = &fds[2];
			openbsd_fd_set_to_nix_fd_set(gi.endian, args->arg3, pfds[2]);
		}
	}
	__nix_catch_any
	{
		*result = -1;
		return (EFAULT);
	}
	__nix_end_try

	    nix_env_set_errno(env, 0);
	*result = nix_select(args->arg0, pfds[0], pfds[1], pfds[2], pntv, env);

	__nix_try
	{
		if (pfds[0] != NULL)
			nix_fd_set_to_openbsd_fd_set(gi.endian, pfds[0], args->arg1);
		if (pfds[1] != NULL)
			nix_fd_set_to_openbsd_fd_set(gi.endian, pfds[1], args->arg2);
		if (pfds[2] != NULL)
			nix_fd_set_to_openbsd_fd_set(gi.endian, pfds[2], args->arg3);
	}
	__nix_catch_any
	{
		*result = -1;
		nix_env_set_errno(env, EFAULT);
	}
	__nix_end_try

	    return (nix_env_get_errno(env));
}

int
openbsd_fsync(
    nix_us_syscall_if_t       *xus,
    nix_monitor_t             *xmon,
    openbsd_fsync_args_t const *args,
    openbsd_fsync_result_t     *result)
{
	nix_env_t *env = openbsd_us_syscall_get_nix_env(xus);

	LCLog(g_bsd_log, LCLogDebug, 0, "invoked", 0);

	nix_env_set_errno(env, 0);
	*result = nix_fsync(args->arg0, env);
	return (nix_env_get_errno(env));
}

int
openbsd_setpriority(
    nix_us_syscall_if_t             *xus,
    nix_monitor_t                   *xmon,
    openbsd_setpriority_args_t const *args,
    openbsd_setpriority_result_t     *result)
{
	nix_env_t *env = openbsd_us_syscall_get_nix_env(xus);

	LCLog(g_bsd_log, LCLogDebug, 0, "invoked", 0);

	nix_env_set_errno(env, 0);
	*result = nix_setpriority(args->arg0, args->arg1, args->arg2, env);
	return (nix_env_get_errno(env));
}

int
openbsd_socket(
    nix_us_syscall_if_t        *xus,
    nix_monitor_t              *xmon,
    openbsd_socket_args_t const *args,
    openbsd_socket_result_t     *result)
{
	nix_env_t *env = openbsd_us_syscall_get_nix_env(xus);

	LCLog(g_bsd_log, LCLogDebug, 0, "invoked", 0);

	nix_env_set_errno(env, 0);
	*result = nix_socket(args->arg0, args->arg1, args->arg2, env);
	return (nix_env_get_errno(env));
}

int
openbsd_connect(
    nix_us_syscall_if_t         *xus,
    nix_monitor_t               *xmon,
    openbsd_connect_args_t const *args,
    openbsd_connect_result_t     *result)
{
	nix_guest_info_t    gi;
	int                 rc;
	struct nix_sockaddr sa;
	nix_socklen_t       salen = sizeof(sa);
	nix_env_t          *env = openbsd_us_syscall_get_nix_env(xus);

	LCLog(g_bsd_log, LCLogDebug, 0, "invoked", 0);

	if (args->arg1 == NULL)
		return (EFAULT);

	nix_monitor_get_guest_info(xmon, &gi);

	rc = 0;

	__nix_try
	{
		if (!openbsd_sockaddr_to_nix_sockaddr(gi.endian, args->arg1, args->arg2, &sa, &salen)) {
			nix_env_set_errno(env, EAFNOSUPPORT);
			rc = -1;
		}
	}
	__nix_catch_any
	{
		nix_env_set_errno(env, EFAULT);
		rc = -1;
	}
	__nix_end_try

	    if (rc == 0)
	{
		nix_env_set_errno(env, 0);
		*result = nix_connect(args->arg0, &sa, salen, env);
	}
	else
	{
		*result = -1;
	}

	return (nix_env_get_errno(env));
}

int
openbsd_getpriority(
    nix_us_syscall_if_t             *xus,
    nix_monitor_t                   *xmon,
    openbsd_getpriority_args_t const *args,
    openbsd_getpriority_result_t     *result)
{

	LCLog(g_bsd_log, LCLogDebug, 0, "invoked", 0);

	/* Insert here implementation code */

	return (ENOSYS);
}

int
openbsd_sigreturn(
    nix_us_syscall_if_t           *xus,
    nix_monitor_t                 *xmon,
    openbsd_sigreturn_args_t const *args,
    openbsd_sigreturn_result_t     *result)
{
	LCLog(g_bsd_log, LCLogDebug, 0, "invoked", 0);

	/* Insert here implementation code */

	return (ENOSYS);
}

int
openbsd_bind(
    nix_us_syscall_if_t      *xus,
    nix_monitor_t            *xmon,
    openbsd_bind_args_t const *args,
    openbsd_bind_result_t     *result)
{
	nix_guest_info_t    gi;
	int                 rc;
	struct nix_sockaddr sa;
	nix_socklen_t       salen = sizeof(sa);
	nix_env_t          *env = openbsd_us_syscall_get_nix_env(xus);

	LCLog(g_bsd_log, LCLogDebug, 0, "invoked", 0);

	if (args->arg1 == NULL)
		return (EFAULT);

	nix_monitor_get_guest_info(xmon, &gi);

	rc = 0;

	__nix_try
	{
		if (!openbsd_sockaddr_to_nix_sockaddr(gi.endian, args->arg1, args->arg2, &sa, &salen)) {
			nix_env_set_errno(env, EINVAL);
			rc = -1;
		}
	}
	__nix_catch_any
	{
		nix_env_set_errno(env, EFAULT);
		rc = -1;
	}
	__nix_end_try

	    if (rc == 0)
	{
		nix_env_set_errno(env, 0);
		*result = nix_bind(args->arg0, &sa, salen, env);
	}
	else
	{
		*result = -1;
	}

	return (nix_env_get_errno(env));
}

int
openbsd_setsockopt(
    nix_us_syscall_if_t            *xus,
    nix_monitor_t                  *xmon,
    openbsd_setsockopt_args_t const *args,
    openbsd_setsockopt_result_t     *result)
{
	LCLog(g_bsd_log, LCLogDebug, 0, "invoked", 0);

	/* Insert here implementation code */

	*result = 0;
	return (0); // ENOSYS;
}

int
openbsd_listen(
    nix_us_syscall_if_t        *xus,
    nix_monitor_t              *xmon,
    openbsd_listen_args_t const *args,
    openbsd_listen_result_t     *result)
{
	nix_env_t *env = openbsd_us_syscall_get_nix_env(xus);

	LCLog(g_bsd_log, LCLogDebug, 0, "invoked", 0);

	nix_env_set_errno(env, 0);
	*result = nix_listen(args->arg0, args->arg1, env);
	return (nix_env_get_errno(env));
}

int
openbsd_sigsuspend(
    nix_us_syscall_if_t            *xus,
    nix_monitor_t                  *xmon,
    openbsd_sigsuspend_args_t const *args,
    openbsd_sigsuspend_result_t     *result)
{
	LCLog(g_bsd_log, LCLogDebug, 0, "invoked", 0);

	/* Insert here implementation code */

	return (ENOSYS);
}

int
openbsd_gettimeofday(
    nix_us_syscall_if_t              *xus,
    nix_monitor_t                    *xmon,
    openbsd_gettimeofday_args_t const *args,
    openbsd_gettimeofday_result_t     *result)
{
	nix_guest_info_t     gi;
	struct nix_timeval   tv;
	struct nix_timezone  tz;
	struct nix_timeval  *ptv = args->arg0 != NULL ? &tv : NULL;
	struct nix_timezone *ptz = args->arg1 != NULL ? &tz : NULL;
	nix_env_t           *env = openbsd_us_syscall_get_nix_env(xus);

	LCLog(g_bsd_log, LCLogDebug, 0, "invoked", 0);

	if (ptv == NULL && ptz == NULL)
		return (EINVAL);

	nix_monitor_get_guest_info(xmon, &gi);

	nix_env_set_errno(env, 0);
	__nix_try
	{
		*result = nix_gettimeofday(ptv, ptz, env);

		if (ptv != NULL)
			nix_timeval_to_openbsd_timeval(gi.endian, ptv, args->arg0);
		if (ptz != NULL)
			nix_timezone_to_openbsd_timezone(gi.endian, ptz, args->arg1);
	}
	__nix_catch_any
	{
		nix_env_set_errno(env, EFAULT);
	}
	__nix_end_try

	    return (nix_env_get_errno(env));
}

int
openbsd_getrusage(
    nix_us_syscall_if_t           *xus,
    nix_monitor_t                 *xmon,
    openbsd_getrusage_args_t const *args,
    openbsd_getrusage_result_t     *result)
{
	nix_guest_info_t   gi;
	struct nix_rusage  ru;
	struct nix_rusage *pru = args->arg1 != NULL ? &ru : NULL;
	nix_env_t         *env = openbsd_us_syscall_get_nix_env(xus);

	LCLog(g_bsd_log, LCLogDebug, 0, "invoked", 0);

	if (pru == NULL)
		return (EINVAL);

	nix_monitor_get_guest_info(xmon, &gi);

	nix_env_set_errno(env, 0);

	*result = nix_getrusage(args->arg0, pru, env);

	__nix_try
	{
		nix_rusage_to_openbsd_rusage(gi.endian, pru, args->arg1);
	}
	__nix_catch_any
	{
		nix_env_set_errno(env, EFAULT);
	}
	__nix_end_try

	    return (nix_env_get_errno(env));
}

int
openbsd_getsockopt(
    nix_us_syscall_if_t            *xus,
    nix_monitor_t                  *xmon,
    openbsd_getsockopt_args_t const *args,
    openbsd_getsockopt_result_t     *result)
{
	LCLog(g_bsd_log, LCLogDebug, 0, "invoked", 0);

	/* Insert here implementation code */
	*result = 0;
	return (0); // ENOSYS;
}

int
openbsd_readv(
    nix_us_syscall_if_t       *xus,
    nix_monitor_t             *xmon,
    openbsd_readv_args_t const *args,
    openbsd_readv_result_t     *result)
{
	nix_guest_info_t  gi;
	struct nix_iovec *xiov;
	nix_env_t        *env = openbsd_us_syscall_get_nix_env(xus);
	nix_mem_if_t     *mem = nix_monitor_get_memory(xmon);

	LCLog(g_bsd_log, LCLogDebug, 0, "invoked", 0);

#if 0	
	nix_monitor_get_guest_info (xmon, &gi);
#else
	/*XXX */
	gi.endian = NIX_ENDIAN_BIG;
#endif

	xiov = __openbsd_iovec32_copy_from(env, mem, &gi, args->arg1, args->arg2);
	if (xiov != NULL) {
		*result = nix_readv(args->arg0, xiov, args->arg2, env);
		nix_free(xiov);
	}

	return (nix_env_get_errno(env));
}

int
openbsd_writev(
    nix_us_syscall_if_t        *xus,
    nix_monitor_t              *xmon,
    openbsd_writev_args_t const *args,
    openbsd_writev_result_t     *result)
{
	nix_guest_info_t  gi;
	struct nix_iovec *xiov;
	nix_env_t        *env = openbsd_us_syscall_get_nix_env(xus);
	nix_mem_if_t     *mem = nix_monitor_get_memory(xmon);

	LCLog(g_bsd_log, LCLogDebug, 0, "invoked", 0);

	nix_monitor_get_guest_info(xmon, &gi);

	xiov = __openbsd_iovec32_copy_from(env, mem, &gi, args->arg1, args->arg2);
	if (xiov != NULL) {
		*result = nix_writev(args->arg0, xiov, args->arg2, env);
		nix_free(xiov);
	}

	return (nix_env_get_errno(env));
}

int
openbsd_settimeofday(
    nix_us_syscall_if_t              *xus,
    nix_monitor_t                    *xmon,
    openbsd_settimeofday_args_t const *args,
    openbsd_settimeofday_result_t     *result)
{
	LCLog(g_bsd_log, LCLogDebug, 0, "invoked", 0);

	/* Insert here implementation code */

	return (ENOSYS);
}

int
openbsd_fchown(
    nix_us_syscall_if_t        *xus,
    nix_monitor_t              *xmon,
    openbsd_fchown_args_t const *args,
    openbsd_fchown_result_t     *result)
{
	nix_env_t *env = openbsd_us_syscall_get_nix_env(xus);

	LCLog(g_bsd_log, LCLogDebug, 0, "invoked", 0);

	nix_env_set_errno(env, 0);
	*result = nix_fchown(args->arg0, args->arg1, args->arg2, env);
	return (nix_env_get_errno(env));
}

int
openbsd_fchmod(
    nix_us_syscall_if_t        *xus,
    nix_monitor_t              *xmon,
    openbsd_fchmod_args_t const *args,
    openbsd_fchmod_result_t     *result)
{
	nix_env_t *env = openbsd_us_syscall_get_nix_env(xus);

	LCLog(g_bsd_log, LCLogDebug, 0, "invoked", 0);

	nix_env_set_errno(env, 0);
	*result = nix_fchmod(args->arg0, args->arg1, env);
	return (nix_env_get_errno(env));
}

int
openbsd_setreuid(
    nix_us_syscall_if_t          *xus,
    nix_monitor_t                *xmon,
    openbsd_setreuid_args_t const *args,
    openbsd_setreuid_result_t     *result)
{
	nix_env_t *env = openbsd_us_syscall_get_nix_env(xus);

	LCLog(g_bsd_log, LCLogDebug, 0, "invoked", 0);

	nix_env_set_errno(env, 0);
	*result = nix_setreuid(args->arg0, args->arg1, env);
	return (nix_env_get_errno(env));
}

int
openbsd_setregid(
    nix_us_syscall_if_t          *xus,
    nix_monitor_t                *xmon,
    openbsd_setregid_args_t const *args,
    openbsd_setregid_result_t     *result)
{
	nix_env_t *env = openbsd_us_syscall_get_nix_env(xus);

	LCLog(g_bsd_log, LCLogDebug, 0, "invoked", 0);

	nix_env_set_errno(env, 0);
	*result = nix_setregid(args->arg0, args->arg1, env);
	return (nix_env_get_errno(env));
}

int
openbsd_rename(
    nix_us_syscall_if_t        *xus,
    nix_monitor_t              *xmon,
    openbsd_rename_args_t const *args,
    openbsd_rename_result_t     *result)
{
	nix_env_t *env = openbsd_us_syscall_get_nix_env(xus);

	LCLog(g_bsd_log, LCLogDebug, 0, "invoked", 0);

	nix_env_set_errno(env, 0);
	*result = nix_rename((char const *)args->arg0, (char const *)args->arg1, env);
	return (nix_env_get_errno(env));
}

int
openbsd_flock(
    nix_us_syscall_if_t       *xus,
    nix_monitor_t             *xmon,
    openbsd_flock_args_t const *args,
    openbsd_flock_result_t     *result)
{
	nix_env_t *env = openbsd_us_syscall_get_nix_env(xus);

	LCLog(g_bsd_log, LCLogDebug, 0, "invoked", 0);

	nix_env_set_errno(env, 0);
	*result = nix_flock(args->arg0, args->arg1, env);
	return (nix_env_get_errno(env));
}

int
openbsd_mkfifo(
    nix_us_syscall_if_t        *xus,
    nix_monitor_t              *xmon,
    openbsd_mkfifo_args_t const *args,
    openbsd_mkfifo_result_t     *result)
{
	nix_env_t *env = openbsd_us_syscall_get_nix_env(xus);

	LCLog(g_bsd_log, LCLogDebug, 0, "invoked", 0);

	nix_env_set_errno(env, 0);
	*result = nix_mkfifo((char const *)args->arg0, args->arg1, env);
	return (nix_env_get_errno(env));
}

int
openbsd_sendto(
    nix_us_syscall_if_t        *xus,
    nix_monitor_t              *xmon,
    openbsd_sendto_args_t const *args,
    openbsd_sendto_result_t     *result)
{
	nix_guest_info_t     gi;
	struct nix_sockaddr  sa;
	int                  rc = -1;
	struct nix_sockaddr *psa = args->arg4 != NULL ? &sa : NULL;
	nix_socklen_t        salen = sizeof(sa);
	nix_env_t           *env = openbsd_us_syscall_get_nix_env(xus);

	LCLog(g_bsd_log, LCLogDebug, 0, "invoked", 0);

	if (args->arg1 == NULL)
		return (EFAULT);

	nix_monitor_get_guest_info(xmon, &gi);

	if (args->arg2 == 0) {
		*result = 0;
		return (0);
	}

	if (psa != NULL) {
		__nix_try
		{
			if (!openbsd_sockaddr_to_nix_sockaddr(gi.endian, args->arg4, args->arg5, &sa, &salen)) {
				nix_env_set_errno(env, EINVAL);
				rc = -1;
			}
		}
		__nix_catch_any
		{
			nix_env_set_errno(env, EFAULT);
			rc = -1;
		}
		__nix_end_try
	} else {
		rc = 0;
		salen = 0;
	}

	if (rc == 0) {
		nix_env_set_errno(env, 0);
		*result = nix_sendto(args->arg0, args->arg1, args->arg2, args->arg3, psa, salen, env);
	} else {
		*result = -1;
	}

	return (nix_env_get_errno(env));
}

int
openbsd_shutdown(
    nix_us_syscall_if_t          *xus,
    nix_monitor_t                *xmon,
    openbsd_shutdown_args_t const *args,
    openbsd_shutdown_result_t     *result)
{
	nix_env_t *env = openbsd_us_syscall_get_nix_env(xus);

	LCLog(g_bsd_log, LCLogDebug, 0, "invoked", 0);

	nix_env_set_errno(env, 0);
	*result = nix_shutdown(args->arg0, args->arg1, env);
	return (nix_env_get_errno(env));
}

int
openbsd_socketpair(
    nix_us_syscall_if_t            *xus,
    nix_monitor_t                  *xmon,
    openbsd_socketpair_args_t const *args,
    openbsd_socketpair_result_t     *result)
{
	LCLog(g_bsd_log, LCLogDebug, 0, "invoked", 0);

	/* Insert here implementation code */

	return (ENOSYS);
}

int
openbsd_mkdir(
    nix_us_syscall_if_t       *xus,
    nix_monitor_t             *xmon,
    openbsd_mkdir_args_t const *args,
    openbsd_mkdir_result_t     *result)
{
	nix_env_t *env = openbsd_us_syscall_get_nix_env(xus);

	LCLog(g_bsd_log, LCLogDebug, 0, "invoked", 0);

	nix_env_set_errno(env, 0);
	*result = nix_mkdir((char const *)args->arg0, args->arg1, env);
	return (nix_env_get_errno(env));
}

int
openbsd_rmdir(
    nix_us_syscall_if_t       *xus,
    nix_monitor_t             *xmon,
    openbsd_rmdir_args_t const *args,
    openbsd_rmdir_result_t     *result)
{
	nix_env_t *env = openbsd_us_syscall_get_nix_env(xus);

	LCLog(g_bsd_log, LCLogDebug, 0, "invoked", 0);

	nix_env_set_errno(env, 0);
	*result = nix_rmdir((char const *)args->arg0, env);
	return (nix_env_get_errno(env));
}

int
openbsd_utimes(
    nix_us_syscall_if_t        *xus,
    nix_monitor_t              *xmon,
    openbsd_utimes_args_t const *args,
    openbsd_utimes_result_t     *result)
{
	LCLog(g_bsd_log, LCLogDebug, 0, "invoked", 0);

	/* Insert here implementation code */

	return (ENOSYS);
}

int
openbsd_adjtime(
    nix_us_syscall_if_t         *xus,
    nix_monitor_t               *xmon,
    openbsd_adjtime_args_t const *args,
    openbsd_adjtime_result_t     *result)
{
	LCLog(g_bsd_log, LCLogDebug, 0, "invoked", 0);

	/* Insert here implementation code */

	return (ENOSYS);
}

int
openbsd_setsid(
    nix_us_syscall_if_t        *xus,
    nix_monitor_t              *xmon,
    openbsd_setsid_args_t const *args,
    openbsd_setsid_result_t     *result)
{
	nix_env_t *env = openbsd_us_syscall_get_nix_env(xus);

	LCLog(g_bsd_log, LCLogDebug, 0, "invoked", 0);

	nix_env_set_errno(env, 0);
	*result = nix_setsid(env);
	return (nix_env_get_errno(env));
}

int
openbsd_quotactl(
    nix_us_syscall_if_t          *xus,
    nix_monitor_t                *xmon,
    openbsd_quotactl_args_t const *args,
    openbsd_quotactl_result_t     *result)
{
	LCLog(g_bsd_log, LCLogDebug, 0, "invoked", 0);

	/* Insert here implementation code */

	return (ENOSYS);
}

int
openbsd_nfssvc(
    nix_us_syscall_if_t        *xus,
    nix_monitor_t              *xmon,
    openbsd_nfssvc_args_t const *args,
    openbsd_nfssvc_result_t     *result)
{
	LCLog(g_bsd_log, LCLogDebug, 0, "invoked", 0);

	/* Insert here implementation code */

	return (ENOSYS);
}

int
openbsd_getfh(
    nix_us_syscall_if_t       *xus,
    nix_monitor_t             *xmon,
    openbsd_getfh_args_t const *args,
    openbsd_getfh_result_t     *result)
{
	LCLog(g_bsd_log, LCLogDebug, 0, "invoked", 0);

	/* Insert here implementation code */

	return (ENOSYS);
}

int
openbsd_sysarch(
    nix_us_syscall_if_t         *xus,
    nix_monitor_t               *xmon,
    openbsd_sysarch_args_t const *args,
    openbsd_sysarch_result_t     *result)
{
	LCLog(g_bsd_log, LCLogDebug, 0, "invoked", 0);

	/* Insert here implementation code */

	return (ENOSYS);
}

int
openbsd_pread(
    nix_us_syscall_if_t       *xus,
    nix_monitor_t             *xmon,
    openbsd_pread_args_t const *args,
    openbsd_pread_result_t     *result)
{
	nix_env_t *env = openbsd_us_syscall_get_nix_env(xus);

	LCLog(g_bsd_log, LCLogDebug, 0, "invoked", 0);

	nix_env_set_errno(env, 0);
	*result = nix_pread(args->arg0, args->arg1, args->arg2, args->arg3, env);
	return (nix_env_get_errno(env));
}

int
openbsd_pwrite(
    nix_us_syscall_if_t        *xus,
    nix_monitor_t              *xmon,
    openbsd_pwrite_args_t const *args,
    openbsd_pwrite_result_t     *result)
{
	nix_env_t *env = openbsd_us_syscall_get_nix_env(xus);

	LCLog(g_bsd_log, LCLogDebug, 0, "invoked", 0);

	nix_env_set_errno(env, 0);
	*result = nix_pwrite(args->arg0, args->arg1, args->arg2, args->arg3, env);
	return (nix_env_get_errno(env));
}

int
openbsd_setgid(
    nix_us_syscall_if_t        *xus,
    nix_monitor_t              *xmon,
    openbsd_setgid_args_t const *args,
    openbsd_setgid_result_t     *result)
{
	nix_env_t *env = openbsd_us_syscall_get_nix_env(xus);

	LCLog(g_bsd_log, LCLogDebug, 0, "invoked", 0);

	nix_env_set_errno(env, 0);
	*result = nix_setgid(args->arg0, env);
	return (nix_env_get_errno(env));
}

int
openbsd_setegid(
    nix_us_syscall_if_t         *xus,
    nix_monitor_t               *xmon,
    openbsd_setegid_args_t const *args,
    openbsd_setegid_result_t     *result)
{
	nix_env_t *env = openbsd_us_syscall_get_nix_env(xus);

	LCLog(g_bsd_log, LCLogDebug, 0, "invoked", 0);

	nix_env_set_errno(env, 0);
	*result = nix_setegid(args->arg0, env);
	return (nix_env_get_errno(env));
}

int
openbsd_seteuid(
    nix_us_syscall_if_t         *xus,
    nix_monitor_t               *xmon,
    openbsd_seteuid_args_t const *args,
    openbsd_seteuid_result_t     *result)
{
	nix_env_t *env = openbsd_us_syscall_get_nix_env(xus);

	LCLog(g_bsd_log, LCLogDebug, 0, "invoked", 0);

	nix_env_set_errno(env, 0);
	*result = nix_seteuid(args->arg0, env);
	return (nix_env_get_errno(env));
}

int
openbsd_pathconf(
    nix_us_syscall_if_t          *xus,
    nix_monitor_t                *xmon,
    openbsd_pathconf_args_t const *args,
    openbsd_pathconf_result_t     *result)
{
	nix_env_t *env = openbsd_us_syscall_get_nix_env(xus);

	LCLog(g_bsd_log, LCLogDebug, 0, "invoked", 0);

	nix_env_set_errno(env, 0);
	*result = nix_pathconf((char const *)args->arg0, args->arg1, env);
	return (nix_env_get_errno(env));
}

int
openbsd_fpathconf(
    nix_us_syscall_if_t           *xus,
    nix_monitor_t                 *xmon,
    openbsd_fpathconf_args_t const *args,
    openbsd_fpathconf_result_t     *result)
{
	nix_env_t *env = openbsd_us_syscall_get_nix_env(xus);

	LCLog(g_bsd_log, LCLogDebug, 0, "invoked", 0);

	nix_env_set_errno(env, 0);
	*result = nix_fpathconf(args->arg0, args->arg1, env);
	return (nix_env_get_errno(env));
}

int
openbsd_swapctl(
    nix_us_syscall_if_t         *xus,
    nix_monitor_t               *xmon,
    openbsd_swapctl_args_t const *args,
    openbsd_swapctl_result_t     *result)
{
	LCLog(g_bsd_log, LCLogDebug, 0, "invoked", 0);

	/* Insert here implementation code */

	return (ENOSYS);
}

int
openbsd_getrlimit(
    nix_us_syscall_if_t           *xus,
    nix_monitor_t                 *xmon,
    openbsd_getrlimit_args_t const *args,
    openbsd_getrlimit_result_t     *result)
{
	nix_guest_info_t  gi;
	int               resource;
	struct nix_rlimit rl;
	nix_env_t        *env = openbsd_us_syscall_get_nix_env(xus);

	LCLog(g_bsd_log, LCLogDebug, 0, "invoked", 0);

	if (args->arg1 == NULL)
		return (EFAULT);

	nix_monitor_get_guest_info(xmon, &gi);

	nix_env_set_errno(env, 0);
	switch (args->arg0) {
	case OPENBSD_RLIMIT_CPU:
		resource = NIX_RLIMIT_CPU;
		break;
	case OPENBSD_RLIMIT_CORE:
		resource = NIX_RLIMIT_CORE;
		break;
	case OPENBSD_RLIMIT_DATA:
		resource = NIX_RLIMIT_DATA;
		break;
	case OPENBSD_RLIMIT_FSIZE:
		resource = NIX_RLIMIT_FSIZE;
		break;
	case OPENBSD_RLIMIT_MEMLOCK:
		resource = NIX_RLIMIT_MEMLOCK;
		break;
	case OPENBSD_RLIMIT_NOFILE:
		resource = NIX_RLIMIT_NOFILE;
		break;
	case OPENBSD_RLIMIT_NPROC:
		resource = NIX_RLIMIT_NPROC;
		break;
	case OPENBSD_RLIMIT_RSS:
		resource = NIX_RLIMIT_RSS;
		break;
	case OPENBSD_RLIMIT_STACK:
		resource = NIX_RLIMIT_STACK;
		break;
	default:
		return (EINVAL);
	}

	*result = nix_getrlimit(resource, &rl, env);
	if (nix_env_get_errno(env))
		return (nix_env_get_errno(env));

	__nix_try
	{
		nix_rlimit_to_openbsd_rlimit(gi.endian, &rl, args->arg1);
	}
	__nix_catch_any
	{
		nix_env_set_errno(env, EFAULT);
	}
	__nix_end_try

	    return (nix_env_get_errno(env));
}

int
openbsd_setrlimit(
    nix_us_syscall_if_t           *xus,
    nix_monitor_t                 *xmon,
    openbsd_setrlimit_args_t const *args,
    openbsd_setrlimit_result_t     *result)
{
	LCLog(g_bsd_log, LCLogDebug, 0, "invoked", 0);

	/* Insert here implementation code */

	*result = 0;
	return (0); // XXX ENOSYS;
}

int
openbsd_mmap(
    nix_us_syscall_if_t      *xus,
    nix_monitor_t            *xmon,
    openbsd_mmap_args_t const *args,
    openbsd_mmap_result_t     *result)
{
	int        prot;
	int        flags;
	nix_env_t *env = openbsd_us_syscall_get_nix_env(xus);

	LCLog(g_bsd_log, LCLogDebug, 0, "invoked", 0);

	/*
	 * NIX PROT and FLAGS values are based on OBSD4.1.
	 */
	prot = args->arg2 & NIX_PROT_FLAGMASK;
	flags = args->arg3 & NIX_MAP_FLAGMASK;
	nix_env_set_errno(env, 0);

	*result = (uintptr_t)nix_mmap(args->arg0, args->arg1, prot, flags,
	                              args->arg4, args->arg5, env);
	LCLog(g_bsd_log, LCLogDebug, 0, "%x,%x -> %x,%x - res = %x",
	      args->arg2, args->arg3, prot, flags, *result);

	return (nix_env_get_errno(env));
}

int
openbsd_lseek(
    nix_us_syscall_if_t       *xus,
    nix_monitor_t             *xmon,
    openbsd_lseek_args_t const *args,
    openbsd_lseek_result_t     *result)
{
	nix_env_t *env = openbsd_us_syscall_get_nix_env(xus);

	LCLog(g_bsd_log, LCLogDebug, 0, "invoked", 0);

	nix_env_set_errno(env, 0);
	*result = nix_lseek(args->arg0, args->arg1, args->arg2, env);
	return (nix_env_get_errno(env));
}

int
openbsd_truncate(
    nix_us_syscall_if_t          *xus,
    nix_monitor_t                *xmon,
    openbsd_truncate_args_t const *args,
    openbsd_truncate_result_t     *result)
{
	nix_env_t *env = openbsd_us_syscall_get_nix_env(xus);

	LCLog(g_bsd_log, LCLogDebug, 0, "invoked", 0);

	nix_env_set_errno(env, 0);
	*result = nix_truncate((char const *)args->arg0, args->arg1, env);
	return (nix_env_get_errno(env));
}

int
openbsd_ftruncate(
    nix_us_syscall_if_t           *xus,
    nix_monitor_t                 *xmon,
    openbsd_ftruncate_args_t const *args,
    openbsd_ftruncate_result_t     *result)
{
	nix_env_t *env = openbsd_us_syscall_get_nix_env(xus);

	LCLog(g_bsd_log, LCLogDebug, 0, "invoked", 0);

	nix_env_set_errno(env, 0);
	*result = nix_ftruncate(args->arg0, args->arg1, env);
	return (nix_env_get_errno(env));
}

/// XXX move away
static __inline void
__str_copy(nix_guest_info_t const *gi, char *out,
           uint32_t *len, char const *in)
{
	size_t inlen = strlen(in);

	*len = NIX_MIN(inlen, GE32(gi, *len));
	if (*len == 0)
		return;

	memcpy(out, in, *len);
	out[*len] = 0;
	*len = GE32(gi, *len);
}

int
openbsd_mlock(
    nix_us_syscall_if_t       *xus,
    nix_monitor_t             *xmon,
    openbsd_mlock_args_t const *args,
    openbsd_mlock_result_t     *result)
{
	nix_env_t *env = openbsd_us_syscall_get_nix_env(xus);

	LCLog(g_bsd_log, LCLogDebug, 0, "invoked", 0);

	nix_env_set_errno(env, 0);
	*result = nix_mlock((uintmax_t)(uintptr_t)args->arg0, args->arg1, env);
	return (nix_env_get_errno(env));
}

int
openbsd_munlock(
    nix_us_syscall_if_t         *xus,
    nix_monitor_t               *xmon,
    openbsd_munlock_args_t const *args,
    openbsd_munlock_result_t     *result)
{
	nix_env_t *env = openbsd_us_syscall_get_nix_env(xus);

	LCLog(g_bsd_log, LCLogDebug, 0, "invoked", 0);

	nix_env_set_errno(env, 0);
	*result = nix_munlock((uintmax_t)(uintptr_t)args->arg0, args->arg1, env);
	return (nix_env_get_errno(env));
}

int
openbsd_futimes(
    nix_us_syscall_if_t         *xus,
    nix_monitor_t               *xmon,
    openbsd_futimes_args_t const *args,
    openbsd_futimes_result_t     *result)
{
	LCLog(g_bsd_log, LCLogDebug, 0, "invoked", 0);

	/* Insert here implementation code */

	return (ENOSYS);
}

int
openbsd_getpgid(
    nix_us_syscall_if_t         *xus,
    nix_monitor_t               *xmon,
    openbsd_getpgid_args_t const *args,
    openbsd_getpgid_result_t     *result)
{
	nix_env_t *env = openbsd_us_syscall_get_nix_env(xus);

	LCLog(g_bsd_log, LCLogDebug, 0, "invoked", 0);

	nix_env_set_errno(env, 0);
	*result = nix_getpgid(args->arg0, env);
	return (nix_env_get_errno(env));
}

int
openbsd_semget(
    nix_us_syscall_if_t        *xus,
    nix_monitor_t              *xmon,
    openbsd_semget_args_t const *args,
    openbsd_semget_result_t     *result)
{
	LCLog(g_bsd_log, LCLogDebug, 0, "invoked", 0);

	/* Insert here implementation code */

	return (ENOSYS);
}

int
openbsd_msgget(
    nix_us_syscall_if_t        *xus,
    nix_monitor_t              *xmon,
    openbsd_msgget_args_t const *args,
    openbsd_msgget_result_t     *result)
{
	LCLog(g_bsd_log, LCLogDebug, 0, "invoked", 0);

	/* Insert here implementation code */

	return (ENOSYS);
}

int
openbsd_msgsnd(
    nix_us_syscall_if_t        *xus,
    nix_monitor_t              *xmon,
    openbsd_msgsnd_args_t const *args,
    openbsd_msgsnd_result_t     *result)
{
	LCLog(g_bsd_log, LCLogDebug, 0, "invoked", 0);

	/* Insert here implementation code */

	return (ENOSYS);
}

int
openbsd_msgrcv(
    nix_us_syscall_if_t        *xus,
    nix_monitor_t              *xmon,
    openbsd_msgrcv_args_t const *args,
    openbsd_msgrcv_result_t     *result)
{
	LCLog(g_bsd_log, LCLogDebug, 0, "invoked", 0);

	/* Insert here implementation code */

	return (ENOSYS);
}

int
openbsd_shmat(
    nix_us_syscall_if_t       *xus,
    nix_monitor_t             *xmon,
    openbsd_shmat_args_t const *args,
    openbsd_shmat_result_t     *result)
{
	LCLog(g_bsd_log, LCLogDebug, 0, "invoked", 0);

	/* Insert here implementation code */

	return (ENOSYS);
}

int
openbsd_shmdt(
    nix_us_syscall_if_t       *xus,
    nix_monitor_t             *xmon,
    openbsd_shmdt_args_t const *args,
    openbsd_shmdt_result_t     *result)
{
	LCLog(g_bsd_log, LCLogDebug, 0, "invoked", 0);

	/* Insert here implementation code */

	return (ENOSYS);
}

int
openbsd_clock_gettime(
    nix_us_syscall_if_t               *xus,
    nix_monitor_t                     *xmon,
    openbsd_clock_gettime_args_t const *args,
    openbsd_clock_gettime_result_t     *result)
{
	LCLog(g_bsd_log, LCLogDebug, 0, "invoked", 0);

	/* Insert here implementation code */

	return (ENOSYS);
}

int
openbsd_clock_settime(
    nix_us_syscall_if_t               *xus,
    nix_monitor_t                     *xmon,
    openbsd_clock_settime_args_t const *args,
    openbsd_clock_settime_result_t     *result)
{
	LCLog(g_bsd_log, LCLogDebug, 0, "invoked", 0);

	/* Insert here implementation code */

	return (ENOSYS);
}

int
openbsd_clock_getres(
    nix_us_syscall_if_t              *xus,
    nix_monitor_t                    *xmon,
    openbsd_clock_getres_args_t const *args,
    openbsd_clock_getres_result_t     *result)
{
	LCLog(g_bsd_log, LCLogDebug, 0, "invoked", 0);

	/* Insert here implementation code */

	return (ENOSYS);
}

int
openbsd_nanosleep(
    nix_us_syscall_if_t           *xus,
    nix_monitor_t                 *xmon,
    openbsd_nanosleep_args_t const *args,
    openbsd_nanosleep_result_t     *result)
{
	nix_guest_info_t     gi;
	struct nix_timespec  rqt;
	struct nix_timespec  rmt;
	struct nix_timespec *rmtp = args->arg1 != NULL ? &rmt : NULL;
	nix_env_t           *env = openbsd_us_syscall_get_nix_env(xus);

	LCLog(g_bsd_log, LCLogDebug, 0, "invoked", 0);

	if (args->arg0 == NULL)
		return (EFAULT);

	nix_monitor_get_guest_info(xmon, &gi);

	nix_env_set_errno(env, 0);
	__nix_try
	{
		openbsd_timespec_to_nix_timespec(gi.endian, args->arg0, &rqt);

		*result = nix_nanosleep(&rqt, rmtp, env);

		if (rmtp != NULL)
			nix_timespec_to_openbsd_timespec(gi.endian, rmtp, args->arg1);
	}
	__nix_catch_any
	{
		nix_env_set_errno(env, EFAULT);
	}
	__nix_end_try

	    return (nix_env_get_errno(env));
}

int
openbsd_minherit(
    nix_us_syscall_if_t          *xus,
    nix_monitor_t                *xmon,
    openbsd_minherit_args_t const *args,
    openbsd_minherit_result_t     *result)
{
	nix_env_t *env = openbsd_us_syscall_get_nix_env(xus);

	LCLog(g_bsd_log, LCLogDebug, 0, "invoked", 0);

	nix_env_set_errno(env, 0);
	*result = nix_minherit((uintmax_t)(uintptr_t)args->arg0, args->arg1, args->arg2, env);
	return (nix_env_get_errno(env));
}

int
openbsd_poll(
    nix_us_syscall_if_t      *xus,
    nix_monitor_t            *xmon,
    openbsd_poll_args_t const *args,
    openbsd_poll_result_t     *result)
{
	nix_guest_info_t      gi;
	struct openbsd_pollfd *ofds = args->arg0;
	struct nix_pollfd    *fds = NULL;
	nix_env_t            *env = openbsd_us_syscall_get_nix_env(xus);

	LCLog(g_bsd_log, LCLogDebug, 0, "invoked", 0);

	nix_monitor_get_guest_info(xmon, &gi);

	nix_env_set_errno(env, 0);
	fds = __openbsd_pollfd_copy_from(env, &gi, ofds, args->arg1);
	if (fds != NULL) {
		*result = nix_poll(fds, args->arg1, args->arg2, env);

		if (nix_env_get_errno(env) == 0) {
			/* Copy back the results */
			__nix_try
			{
				size_t n;

				for (n = 0; n < args->arg1; n++)
					nix_pollfd_to_openbsd_pollfd(gi.endian, fds + n, ofds + n);
			}
			__nix_catch_any
			{
				nix_env_set_errno(env, EFAULT);
				*result = -1;
			}
			__nix_end_try
		}

		nix_free(fds);
	} else {
		*result = -1;
	}

	return (nix_env_get_errno(env));
}

int
openbsd_issetugid(
    nix_us_syscall_if_t           *xus,
    nix_monitor_t                 *xmon,
    openbsd_issetugid_args_t const *args,
    openbsd_issetugid_result_t     *result)
{
	nix_env_t *env = openbsd_us_syscall_get_nix_env(xus);

	LCLog(g_bsd_log, LCLogDebug, 0, "invoked", 0);

	nix_env_set_errno(env, 0);
	*result = nix_bsd_issetugid(env);
	return (nix_env_get_errno(env));
}

int
openbsd_lchown(
    nix_us_syscall_if_t        *xus,
    nix_monitor_t              *xmon,
    openbsd_lchown_args_t const *args,
    openbsd_lchown_result_t     *result)
{
	nix_env_t *env = openbsd_us_syscall_get_nix_env(xus);

	LCLog(g_bsd_log, LCLogDebug, 0, "invoked", 0);

	nix_env_set_errno(env, 0);
	*result = nix_lchown((char const *)args->arg0, args->arg1, args->arg2, env);
	return (nix_env_get_errno(env));
}

int
openbsd_getsid(
    nix_us_syscall_if_t        *xus,
    nix_monitor_t              *xmon,
    openbsd_getsid_args_t const *args,
    openbsd_getsid_result_t     *result)
{
	nix_env_t *env = openbsd_us_syscall_get_nix_env(xus);

	LCLog(g_bsd_log, LCLogDebug, 0, "invoked", 0);

	nix_env_set_errno(env, 0);
	*result = nix_getsid(args->arg0, env);
	return (nix_env_get_errno(env));
}

int
openbsd_msync(
    nix_us_syscall_if_t       *xus,
    nix_monitor_t             *xmon,
    openbsd_msync_args_t const *args,
    openbsd_msync_result_t     *result)
{
	nix_env_t *env = openbsd_us_syscall_get_nix_env(xus);

	LCLog(g_bsd_log, LCLogDebug, 0, "invoked", 0);

	nix_env_set_errno(env, 0);
	*result = nix_msync((uintmax_t)(uintptr_t)args->arg0, args->arg1, args->arg2, env);
	return (nix_env_get_errno(env));
}

int
openbsd_getfsstat(
    nix_us_syscall_if_t           *xus,
    nix_monitor_t                 *xmon,
    openbsd_getfsstat_args_t const *args,
    openbsd_getfsstat_result_t     *result)
{
	nix_guest_info_t      gi;
	int                   flags;
	size_t                maxcount;
	struct nix_statfs    *fss;
	struct openbsd_statfs *ofss;
	nix_env_t            *env = openbsd_us_syscall_get_nix_env(xus);

	LCLog(g_bsd_log, LCLogDebug, 0, "invoked", 0);

	nix_monitor_get_guest_info(xmon, &gi);

	nix_env_set_errno(env, 0);

	flags = 0;
	if (args->arg2 == OPENBSD_MNT_NOWAIT)
		flags = NIX_MNT_NOWAIT;

	if (args->arg0 == NULL) {
		*result = nix_bsd_getfsstat(NULL, 0, flags, env);
		return (nix_env_get_errno(env));
	}

	maxcount = args->arg1 / sizeof(struct openbsd_statfs);
	if (maxcount == 0)
		return (EINVAL);

	fss = nix_alloc_ntype(struct nix_statfs, maxcount, 0);
	if (fss == NULL)
		return (ENOMEM);

	*result = nix_bsd_getfsstat(fss, maxcount * sizeof(struct nix_statfs), flags, env);
	if ((int32_t)*result < 0) {
		nix_free(fss);
		return (nix_env_get_errno(env));
	}

	__nix_try
	{
		size_t n;

		ofss = (struct openbsd_statfs *)args->arg0;
		for (n = 0; n < *result; n++)
			nix_statfs_to_openbsd_statfs(gi.endian, fss + n, ofss + n);

		nix_free(fss);
	}
	__nix_catch_any
	{
		nix_free(fss);
		return (EFAULT);
	}
	__nix_end_try

	    return (nix_env_get_errno(env));
}

int
openbsd_statfs(
    nix_us_syscall_if_t        *xus,
    nix_monitor_t              *xmon,
    openbsd_statfs_args_t const *args,
    openbsd_statfs_result_t     *result)
{
	nix_guest_info_t  gi;
	struct nix_statfs fs;
	nix_env_t        *env = openbsd_us_syscall_get_nix_env(xus);

	LCLog(g_bsd_log, LCLogDebug, 0, "invoked", 0);

	if (args->arg0 == NULL)
		return (EFAULT);

	nix_monitor_get_guest_info(xmon, &gi);

	nix_env_set_errno(env, 0);
	*result = nix_bsd_statfs((char const *)args->arg0, &fs, env);
	if (*result)
		return (nix_env_get_errno(env));

	__nix_try
	{
		nix_statfs_to_openbsd_statfs(gi.endian, &fs, args->arg1);
	}
	__nix_catch_any
	{
		return (EFAULT);
	}
	__nix_end_try

	    return (nix_env_get_errno(env));
}

int
openbsd_fstatfs(
    nix_us_syscall_if_t         *xus,
    nix_monitor_t               *xmon,
    openbsd_fstatfs_args_t const *args,
    openbsd_fstatfs_result_t     *result)
{
	nix_guest_info_t  gi;
	struct nix_statfs fs;
	nix_env_t        *env = openbsd_us_syscall_get_nix_env(xus);

	LCLog(g_bsd_log, LCLogDebug, 0, "invoked", 0);

	nix_monitor_get_guest_info(xmon, &gi);

	nix_env_set_errno(env, 0);
	*result = nix_bsd_fstatfs(args->arg0, &fs, env);
	if (*result)
		return (nix_env_get_errno(env));

	__nix_try
	{
		nix_statfs_to_openbsd_statfs(gi.endian, &fs, args->arg1);
	}
	__nix_catch_any
	{
		return (EFAULT);
	}
	__nix_end_try

	    return (nix_env_get_errno(env));
}

int
openbsd_pipe(
    nix_us_syscall_if_t      *xus,
    nix_monitor_t            *xmon,
    openbsd_pipe_args_t const *args,
    openbsd_pipe_result_t     *result)
{
	int        fds[2];
	int32_t   *ofds;
	nix_env_t *env = openbsd_us_syscall_get_nix_env(xus);

	LCLog(g_bsd_log, LCLogDebug, 0, "invoked", 0);

	if (args->arg0 == NULL)
		return (EFAULT);

	nix_env_set_errno(env, 0);
	*result = nix_pipe(fds, env);
	if (*result)
		return (nix_env_get_errno(env));

	__nix_try
	{
		ofds = (int32_t *)args->arg0;

		ofds[0] = fds[0];
		ofds[1] = fds[1];
	}
	__nix_catch_any
	{
		nix_close(fds[1], env);
		nix_close(fds[0], env);
		nix_env_set_errno(env, EFAULT);
	}
	__nix_end_try

	    return (nix_env_get_errno(env));
}

int
openbsd_fhopen(
    nix_us_syscall_if_t        *xus,
    nix_monitor_t              *xmon,
    openbsd_fhopen_args_t const *args,
    openbsd_fhopen_result_t     *result)
{
	LCLog(g_bsd_log, LCLogDebug, 0, "invoked", 0);

	/* Insert here implementation code */

	return (ENOSYS);
}

int
openbsd_fhstatfs(
    nix_us_syscall_if_t          *xus,
    nix_monitor_t                *xmon,
    openbsd_fhstatfs_args_t const *args,
    openbsd_fhstatfs_result_t     *result)
{
	LCLog(g_bsd_log, LCLogDebug, 0, "invoked", 0);

	/* Insert here implementation code */

	return (ENOSYS);
}

int
openbsd_preadv(
    nix_us_syscall_if_t        *xus,
    nix_monitor_t              *xmon,
    openbsd_preadv_args_t const *args,
    openbsd_preadv_result_t     *result)
{
	LCLog(g_bsd_log, LCLogDebug, 0, "invoked", 0);

	/* Insert here implementation code */

	return (ENOSYS);
}

int
openbsd_pwritev(
    nix_us_syscall_if_t         *xus,
    nix_monitor_t               *xmon,
    openbsd_pwritev_args_t const *args,
    openbsd_pwritev_result_t     *result)
{
	LCLog(g_bsd_log, LCLogDebug, 0, "invoked", 0);

	/* Insert here implementation code */

	return (ENOSYS);
}

int
openbsd_kqueue(
    nix_us_syscall_if_t        *xus,
    nix_monitor_t              *xmon,
    openbsd_kqueue_args_t const *args,
    openbsd_kqueue_result_t     *result)
{
	nix_env_t *env = openbsd_us_syscall_get_nix_env(xus);

	LCLog(g_bsd_log, LCLogDebug, 0, "invoked", 0);

	nix_env_set_errno(env, 0);
	*result = nix_bsd_kqueue(env);
	return (nix_env_get_errno(env));
}

int
openbsd_kevent(
    nix_us_syscall_if_t        *xus,
    nix_monitor_t              *xmon,
    openbsd_kevent_args_t const *args,
    openbsd_kevent_result_t     *result)
{
	LCLog(g_bsd_log, LCLogDebug, 0, "invoked", 0);

	/* Insert here implementation code */

	return (ENOSYS);
}

int
openbsd_mlockall(
    nix_us_syscall_if_t          *xus,
    nix_monitor_t                *xmon,
    openbsd_mlockall_args_t const *args,
    openbsd_mlockall_result_t     *result)
{
	nix_env_t *env = openbsd_us_syscall_get_nix_env(xus);

	LCLog(g_bsd_log, LCLogDebug, 0, "invoked", 0);

	nix_env_set_errno(env, 0);
	*result = nix_mlockall(args->arg0, env);
	return (nix_env_get_errno(env));
}

int
openbsd_munlockall(
    nix_us_syscall_if_t            *xus,
    nix_monitor_t                  *xmon,
    openbsd_munlockall_args_t const *args,
    openbsd_munlockall_result_t     *result)
{
	nix_env_t *env = openbsd_us_syscall_get_nix_env(xus);

	LCLog(g_bsd_log, LCLogDebug, 0, "invoked", 0);

	nix_env_set_errno(env, 0);
	*result = nix_munlockall(env);
	return (nix_env_get_errno(env));
}

int
openbsd_getresuid(
    nix_us_syscall_if_t           *xus,
    nix_monitor_t                 *xmon,
    openbsd_getresuid_args_t const *args,
    openbsd_getresuid_result_t     *result)
{
	LCLog(g_bsd_log, LCLogDebug, 0, "invoked", 0);

	/* Insert here implementation code */

	return (ENOSYS);
}

int
openbsd_setresuid(
    nix_us_syscall_if_t           *xus,
    nix_monitor_t                 *xmon,
    openbsd_setresuid_args_t const *args,
    openbsd_setresuid_result_t     *result)
{
	nix_env_t *env = openbsd_us_syscall_get_nix_env(xus);

	LCLog(g_bsd_log, LCLogDebug, 0, "invoked", 0);

	nix_env_set_errno(env, 0);
	*result = nix_hpux_setresuid(args->arg0, args->arg1, args->arg2, env);
	return (nix_env_get_errno(env));
}

int
openbsd_getresgid(
    nix_us_syscall_if_t           *xus,
    nix_monitor_t                 *xmon,
    openbsd_getresgid_args_t const *args,
    openbsd_getresgid_result_t     *result)
{
	LCLog(g_bsd_log, LCLogDebug, 0, "invoked", 0);

	/* Insert here implementation code */

	return (ENOSYS);
}

int
openbsd_setresgid(
    nix_us_syscall_if_t           *xus,
    nix_monitor_t                 *xmon,
    openbsd_setresgid_args_t const *args,
    openbsd_setresgid_result_t     *result)
{
	nix_env_t *env = openbsd_us_syscall_get_nix_env(xus);

	LCLog(g_bsd_log, LCLogDebug, 0, "invoked", 0);

	nix_env_set_errno(env, 0);
	*result = nix_hpux_setresgid(args->arg0, args->arg1, args->arg2, env);
	return (nix_env_get_errno(env));
}

int
openbsd_mquery(
    nix_us_syscall_if_t        *xus,
    nix_monitor_t              *xmon,
    openbsd_mquery_args_t const *args,
    openbsd_mquery_result_t     *result)
{
	nix_env_t *env = openbsd_us_syscall_get_nix_env(xus);

	LCLog(g_bsd_log, LCLogDebug, 0, "invoked", 0);

	nix_env_set_errno(env, 0);
	*result = (openbsd_mquery_result_t)(uintptr_t)nix_bsd_mquery((uintmax_t)(uintptr_t)args->arg0, args->arg1, args->arg2, args->arg3, args->arg4, args->arg5, env);
	return (nix_env_get_errno(env));
}

int
openbsd_closefrom(
    nix_us_syscall_if_t           *xus,
    nix_monitor_t                 *xmon,
    openbsd_closefrom_args_t const *args,
    openbsd_closefrom_result_t     *result)
{
	nix_env_t *env = openbsd_us_syscall_get_nix_env(xus);

	LCLog(g_bsd_log, LCLogDebug, 0, "invoked", 0);

	nix_env_set_errno(env, 0);
	*result = nix_bsd_closefrom(args->arg0, env);
	return (nix_env_get_errno(env));
}

int
openbsd_sigaltstack(
    nix_us_syscall_if_t             *xus,
    nix_monitor_t                   *xmon,
    openbsd_sigaltstack_args_t const *args,
    openbsd_sigaltstack_result_t     *result)
{
	LCLog(g_bsd_log, LCLogDebug, 0, "invoked", 0);

	/* Insert here implementation code */

	return (ENOSYS);
}

int
openbsd_shmget(
    nix_us_syscall_if_t        *xus,
    nix_monitor_t              *xmon,
    openbsd_shmget_args_t const *args,
    openbsd_shmget_result_t     *result)
{
	LCLog(g_bsd_log, LCLogDebug, 0, "invoked", 0);

	/* Insert here implementation code */

	return (ENOSYS);
}

int
openbsd_semop(
    nix_us_syscall_if_t       *xus,
    nix_monitor_t             *xmon,
    openbsd_semop_args_t const *args,
    openbsd_semop_result_t     *result)
{
	LCLog(g_bsd_log, LCLogDebug, 0, "invoked", 0);

	/* Insert here implementation code */

	return (ENOSYS);
}

int
openbsd_stat(
    nix_us_syscall_if_t      *xus,
    nix_monitor_t            *xmon,
    openbsd_stat_args_t const *args,
    openbsd_stat_result_t     *result)
{
	nix_guest_info_t gi;
	struct nix_stat  st;
	nix_env_t       *env = openbsd_us_syscall_get_nix_env(xus);

	LCLog(g_bsd_log, LCLogDebug, 0, "invoked", 0);

	if (args->arg1 == NULL)
		return (EFAULT);

	nix_monitor_get_guest_info(xmon, &gi);

	nix_env_set_errno(env, 0);
	__nix_try
	{
		*result = nix_stat((char const *)args->arg0, &st, env);
	}
	__nix_catch_any
	{
		return (EFAULT);
	}
	__nix_end_try

	    if (nix_env_get_errno(env) != 0) return (nix_env_get_errno(env));

	__nix_try
	{
		nix_stat_to_openbsd_stat(gi.endian, &st, args->arg1);
	}
	__nix_catch_any
	{
		return (EFAULT);
	}
	__nix_end_try

	    return (0);
}

int
openbsd_fstat(
    nix_us_syscall_if_t       *xus,
    nix_monitor_t             *xmon,
    openbsd_fstat_args_t const *args,
    openbsd_fstat_result_t     *result)
{
	nix_guest_info_t gi;
	struct nix_stat  st;
	nix_env_t       *env = openbsd_us_syscall_get_nix_env(xus);

	LCLog(g_bsd_log, LCLogDebug, 0, "invoked", 0);

	if (args->arg1 == NULL)
		return (EFAULT);

	nix_monitor_get_guest_info(xmon, &gi);

	nix_env_set_errno(env, 0);
	*result = nix_fstat(args->arg0, &st, env);

	if (nix_env_get_errno(env) != 0)
		return (nix_env_get_errno(env));

	__nix_try
	{
		nix_stat_to_openbsd_stat(gi.endian, &st, args->arg1);
	}
	__nix_catch_any
	{
		return (EFAULT);
	}
	__nix_end_try

	    return (0);
}

int
openbsd_lstat(
    nix_us_syscall_if_t       *xus,
    nix_monitor_t             *xmon,
    openbsd_lstat_args_t const *args,
    openbsd_lstat_result_t     *result)
{
	nix_guest_info_t gi;
	struct nix_stat  st;
	nix_env_t       *env = openbsd_us_syscall_get_nix_env(xus);

	LCLog(g_bsd_log, LCLogDebug, 0, "invoked", 0);

	if (args->arg1 == NULL)
		return (EFAULT);

	nix_monitor_get_guest_info(xmon, &gi);

	nix_env_set_errno(env, 0);
	__nix_try
	{
		*result = nix_lstat((char const *)args->arg0, &st, env);
	}
	__nix_catch_any
	{
		return (EFAULT);
	}
	__nix_end_try

	    if (nix_env_get_errno(env) != 0) return (nix_env_get_errno(env));

	__nix_try
	{
		nix_stat_to_openbsd_stat(gi.endian, &st, args->arg1);
	}
	__nix_catch_any
	{
		return (EFAULT);
	}
	__nix_end_try

	    return (0);
}

int
openbsd_fhstat(
    nix_us_syscall_if_t        *xus,
    nix_monitor_t              *xmon,
    openbsd_fhstat_args_t const *args,
    openbsd_fhstat_result_t     *result)
{
	LCLog(g_bsd_log, LCLogDebug, 0, "invoked", 0);

	/* Insert here implementation code */

	return (ENOSYS);
}

int
openbsd___semctl(
    nix_us_syscall_if_t          *xus,
    nix_monitor_t                *xmon,
    openbsd___semctl_args_t const *args,
    openbsd___semctl_result_t     *result)
{
	LCLog(g_bsd_log, LCLogDebug, 0, "invoked", 0);

	/* Insert here implementation code */

	return (ENOSYS);
}

int
openbsd_shmctl(
    nix_us_syscall_if_t        *xus,
    nix_monitor_t              *xmon,
    openbsd_shmctl_args_t const *args,
    openbsd_shmctl_result_t     *result)
{
	LCLog(g_bsd_log, LCLogDebug, 0, "invoked", 0);

	/* Insert here implementation code */

	return (ENOSYS);
}

int
openbsd_msgctl(
    nix_us_syscall_if_t        *xus,
    nix_monitor_t              *xmon,
    openbsd_msgctl_args_t const *args,
    openbsd_msgctl_result_t     *result)
{
	LCLog(g_bsd_log, LCLogDebug, 0, "invoked", 0);

	/* Insert here implementation code */

	return (ENOSYS);
}

int
openbsd_sched_yield(
    nix_us_syscall_if_t             *xus,
    nix_monitor_t                   *xmon,
    openbsd_sched_yield_args_t const *args,
    openbsd_sched_yield_result_t     *result)
{
	nix_env_t *env = openbsd_us_syscall_get_nix_env(xus);

	LCLog(g_bsd_log, LCLogDebug, 0, "invoked", 0);

	nix_env_set_errno(env, 0);
	*result = nix_rt_sched_yield(env);
	return (nix_env_get_errno(env));
}

int
openbsd_getthrid(
    nix_us_syscall_if_t          *xus,
    nix_monitor_t                *xmon,
    openbsd_getthrid_args_t const *args,
    openbsd_getthrid_result_t     *result)
{
	LCLog(g_bsd_log, LCLogDebug, 0, "invoked", 0);

	/* Insert here implementation code */

	return (ENOSYS);
}

int
openbsd___getcwd(
    nix_us_syscall_if_t          *xus,
    nix_monitor_t                *xmon,
    openbsd___getcwd_args_t const *args,
    openbsd___getcwd_result_t     *result)
{
	nix_env_t *env = openbsd_us_syscall_get_nix_env(xus);

	LCLog(g_bsd_log, LCLogDebug, 0, "invoked", 0);

	nix_env_set_errno(env, 0);
	*result = nix_getcwd((char *)args->arg0, args->arg1, env);
	return (nix_env_get_errno(env));
}

int
openbsd_adjfreq(
    nix_us_syscall_if_t         *xus,
    nix_monitor_t               *xmon,
    openbsd_adjfreq_args_t const *args,
    openbsd_adjfreq_result_t     *result)
{
	LCLog(g_bsd_log, LCLogDebug, 0, "invoked", 0);

	/* Insert here implementation code */

	return (ENOSYS);
}

int
openbsd_obreak(
    nix_us_syscall_if_t        *xus,
    nix_monitor_t              *xmon,
    openbsd_obreak_args_t const *args,
    openbsd_obreak_result_t     *result)
{
	nix_env_t *env = openbsd_us_syscall_get_nix_env(xus);

	LCLog(g_bsd_log, LCLogDebug, 0, "invoked", 0);

	nix_env_set_errno(env, 0);
	nix_brk((uintmax_t)(uintptr_t)args->arg0, env);
	*result = 0;
	return (nix_env_get_errno(env));
}
int
openbsd_unmount(
    nix_us_syscall_if_t         *xus,
    nix_monitor_t               *xmon,
    openbsd_unmount_args_t const *args,
    openbsd_unmount_result_t     *result)
{
	nix_env_t *env = openbsd_us_syscall_get_nix_env(xus);

	LCLog(g_bsd_log, LCLogDebug, 0, "invoked", 0);

	nix_env_set_errno(env, 0);
	*result = nix_bsd_unmount((char const *)args->arg0, args->arg1, env);
	return (nix_env_get_errno(env));
}
int
openbsd_sysctl(
    nix_us_syscall_if_t        *xus,
    nix_monitor_t              *xmon,
    openbsd_sysctl_args_t const *args,
    openbsd_sysctl_result_t     *result)
{
	nix_guest_info_t gi;
	uint32_t         n;
	uint32_t        *mib = args->arg0;
	uint32_t         miblen = args->arg1;
	uint32_t        *oldp = args->arg2;
	uint32_t        *oldlenp = args->arg3;
	//	uint32_t		 *newp    = args->arg4;
	//	uint32_t		  newlen  = args->arg5;
	char buf[512], *p = buf;

	LCLog(g_bsd_log, LCLogDebug, 0, "invoked", 0);

	nix_monitor_get_guest_info(xmon, &gi);

	for (n = 0; n < miblen; n++) {
		if (n > 0)
			*p++ = '.';
		p += sprintf(p, "%u", GE32(&gi, mib[n]));
	}

	LCLog(g_bsd_log, LCLogDebug, 0, "mib=%s", buf);

	*result = 0;
	switch (GE32(&gi, mib[0])) {
	case OPENBSD_CTL_KERN:
		switch (GE32(&gi, mib[1])) {
		case OPENBSD_KERN_OSTYPE:
			LCAssert(g_bsd_log, miblen == 2);
			__nix_try
			{
				__str_copy(&gi, (char *)oldp, oldlenp, "OpenBSD");
			}
			__nix_catch_any
			{
				*result = -1;
				return (EFAULT);
			}
			__nix_end_try return (0);

		case OPENBSD_KERN_OSRELEASE:
			LCAssert(g_bsd_log, miblen == 2);
			__nix_try
			{
				__str_copy(&gi, (char *)oldp, oldlenp, "4.1");
			}
			__nix_catch_any
			{
				*result = -1;
				return (EFAULT);
			}
			__nix_end_try return (0);

		case OPENBSD_KERN_ARGMAX:
			LCAssert(g_bsd_log, miblen == 2);
			__nix_try
			{
				if (*oldlenp != GE32(&gi, 4))
					*result = -1;

				*(uint32_t *)oldp = GE32(&gi, 256 * 1024);
			}
			__nix_catch_any
			{
				*result = -1;
				return (EFAULT);
			}
			__nix_end_try return (0);

		case OPENBSD_KERN_OSVERSION:
			LCAssert(g_bsd_log, miblen == 2);
			__nix_try
			{
				__str_copy(&gi, (char *)oldp, oldlenp, "GENERIC");
			}
			__nix_catch_any
			{
				*result = -1;
				return (EFAULT);
			}
			__nix_end_try return (0);

		case OPENBSD_KERN_HOSTNAME: {
			char       hostname[512];
			nix_env_t *env = openbsd_us_syscall_get_nix_env(xus);

			LCAssert(g_bsd_log, miblen == 2);

			if (nix_gethostname(hostname, sizeof(hostname), env))
				return (nix_env_get_errno(env));

			__nix_try
			{
				__str_copy(&gi, (char *)oldp, oldlenp, hostname);
			}
			__nix_catch_any
			{
				*result = -1;
				return (EFAULT);
			}
			__nix_end_try

			    return (0);
		}

		case OPENBSD_KERN_DOMAINNAME: {
			char       domainname[512];
			nix_env_t *env = openbsd_us_syscall_get_nix_env(xus);

			LCAssert(g_bsd_log, miblen == 2);

			if (nix_getdomainname(domainname, sizeof(domainname), env))
				return (nix_env_get_errno(env));

			__nix_try
			{
				__str_copy(&gi, (char *)oldp, oldlenp, domainname);
			}
			__nix_catch_any
			{
				*result = -1;
				return (EFAULT);
			}
			__nix_end_try

			    return (0);
		}

		case OPENBSD_KERN_ARND: {
			LCAssert(g_bsd_log, miblen == 2);

			__nix_try
			{
				uint8_t buf[256];

				if (GE32(&gi, *oldlenp) > sizeof(buf))
					*oldlenp = GE32(&gi, sizeof(buf));

				if (oldp != NULL) {
					char   pbuf[1024], *p = pbuf;
					size_t x;

					bsd_arc4random_bytes(buf, GE32(&gi, *oldlenp));

					for (x = 0; x < GE32(&gi, *oldlenp); x++)
						p += sprintf(p, "%02x ", (uint8_t)buf[x]);

					LCLog(g_bsd_log, LCLogInfo, 0, "Generated RND sequence = %s", pbuf);
					memcpy(oldp, buf, GE32(&gi, *oldlenp));
				}
			}
			__nix_catch_any
			{
				*result = -1;
				return (EFAULT);
			}
			__nix_end_try

			    return (0);
		}

		default:
			LCLog(g_bsd_log, LCLogError, 0, "mib(%s) [CTL_KERN] not implemented", buf);
			*result = -1;
			return (EINVAL);
		}
		break;

	case OPENBSD_CTL_HW:
		switch (GE32(&gi, mib[1])) {
		case OPENBSD_HW_MACHINE:
			LCAssert(g_bsd_log, miblen == 2);
			__nix_try
			{
				__str_copy(&gi, (char *)oldp, oldlenp, OPENBSD_MACHINE_NAME);
			}
			__nix_catch_any
			{
				*result = -1;
				return (EFAULT);
			}
			__nix_end_try return (0);

		case OPENBSD_HW_NCPUS:
			LCAssert(g_bsd_log, miblen == 2);
			__nix_try
			{
				LCAssert(NULL, GE32(&gi, *oldlenp) >= sizeof(uint32_t));
				*oldp = GE32(&gi, 1);
			}
			__nix_catch_any
			{
				*result = -1;
				return (EFAULT);
			}
			__nix_end_try return (0);

		case OPENBSD_HW_PAGESIZE:
			LCAssert(NULL, miblen == 2);
			__nix_try
			{
				LCAssert(NULL, GE32(&gi, *oldlenp) >= sizeof(uint32_t));
				*oldp = GE32(&gi, gi.page_size);
			}
			__nix_catch_any
			{
				*result = -1;
				return (EFAULT);
			}
			__nix_end_try return (0);

		default:
			LCLog(g_bsd_log, LCLogError, 0, "mib(%s) [CTL_HW] not implemented", buf);
			*result = -1;
			return (EINVAL);
		}
		break;

	default:
		LCLog(g_bsd_log, LCLogError, 0, "mib(%s) not implemented", buf);
		*result = -1;
		return (EINVAL);
	}

	return (0);
}
int
openbsd___thrsleep(
    nix_us_syscall_if_t            *xus,
    nix_monitor_t                  *xmon,
    openbsd___thrsleep_args_t const *args,
    openbsd___thrsleep_result_t     *result)
{
	LCLog(g_bsd_log, LCLogDebug, 0, "invoked", 0);

	/* Insert here implementation code */

	return (ENOSYS);
}
int
openbsd___thrwakeup(
    nix_us_syscall_if_t             *xus,
    nix_monitor_t                   *xmon,
    openbsd___thrwakeup_args_t const *args,
    openbsd___thrwakeup_result_t     *result)
{
	LCLog(g_bsd_log, LCLogDebug, 0, "invoked", 0);

	/* Insert here implementation code */

	return (ENOSYS);
}
int
openbsd___threxit(
    nix_us_syscall_if_t           *xus,
    nix_monitor_t                 *xmon,
    openbsd___threxit_args_t const *args,
    openbsd___threxit_result_t     *result)
{
	LCLog(g_bsd_log, LCLogDebug, 0, "invoked", 0);

	/* Insert here implementation code */

	return (ENOSYS);
}
int
openbsd___thrsigdivert(
    nix_us_syscall_if_t                *xus,
    nix_monitor_t                      *xmon,
    openbsd___thrsigdivert_args_t const *args,
    openbsd___thrsigdivert_result_t     *result)
{
	LCLog(g_bsd_log, LCLogDebug, 0, "invoked", 0);

	/* Insert here implementation code */

	return (ENOSYS);
}

int
openbsd_getentropy(
    nix_us_syscall_if_t            *xus,
    nix_monitor_t                  *xmon,
    openbsd_getentropy_args_t const *args,
    openbsd_getentropy_result_t     *result)
{
	(void)xus;
	(void)xmon;
	(void)args;
	(void)result;

	LCLog(g_bsd_log, LCLogWarning, 0,
	      "unimplemented OpenBSD 7.9 syscall `getentropy'", 0);

	return (ENOSYS);
}

int
openbsd___tfork(
    nix_us_syscall_if_t         *xus,
    nix_monitor_t               *xmon,
    openbsd___tfork_args_t const *args,
    openbsd___tfork_result_t     *result)
{
	(void)xus;
	(void)xmon;
	(void)args;
	(void)result;

	LCLog(g_bsd_log, LCLogWarning, 0,
	      "unimplemented OpenBSD 7.9 syscall `__tfork'", 0);

	return (ENOSYS);
}

int
openbsd_getdtablecount(
    nix_us_syscall_if_t                *xus,
    nix_monitor_t                      *xmon,
    openbsd_getdtablecount_args_t const *args,
    openbsd_getdtablecount_result_t     *result)
{
	(void)xus;
	(void)xmon;
	(void)args;
	(void)result;

	LCLog(g_bsd_log, LCLogWarning, 0,
	      "unimplemented OpenBSD 7.9 syscall `getdtablecount'", 0);

	return (ENOSYS);
}

int
openbsd_fstatat(
    nix_us_syscall_if_t         *xus,
    nix_monitor_t               *xmon,
    openbsd_fstatat_args_t const *args,
    openbsd_fstatat_result_t     *result)
{
	(void)xus;
	(void)xmon;
	(void)args;
	(void)result;

	LCLog(g_bsd_log, LCLogWarning, 0,
	      "unimplemented OpenBSD 7.9 syscall `fstatat'", 0);

	return (ENOSYS);
}

int
openbsd_futex(
    nix_us_syscall_if_t       *xus,
    nix_monitor_t             *xmon,
    openbsd_futex_args_t const *args,
    openbsd_futex_result_t     *result)
{
	(void)xus;
	(void)xmon;
	(void)args;
	(void)result;

	LCLog(g_bsd_log, LCLogWarning, 0,
	      "unimplemented OpenBSD 7.9 syscall `futex'", 0);

	return (ENOSYS);
}

int
openbsd_utimensat(
    nix_us_syscall_if_t           *xus,
    nix_monitor_t                 *xmon,
    openbsd_utimensat_args_t const *args,
    openbsd_utimensat_result_t     *result)
{
	(void)xus;
	(void)xmon;
	(void)args;
	(void)result;

	LCLog(g_bsd_log, LCLogWarning, 0,
	      "unimplemented OpenBSD 7.9 syscall `utimensat'", 0);

	return (ENOSYS);
}

int
openbsd_futimens(
    nix_us_syscall_if_t          *xus,
    nix_monitor_t                *xmon,
    openbsd_futimens_args_t const *args,
    openbsd_futimens_result_t     *result)
{
	(void)xus;
	(void)xmon;
	(void)args;
	(void)result;

	LCLog(g_bsd_log, LCLogWarning, 0,
	      "unimplemented OpenBSD 7.9 syscall `futimens'", 0);

	return (ENOSYS);
}

int
openbsd_kbind(
    nix_us_syscall_if_t       *xus,
    nix_monitor_t             *xmon,
    openbsd_kbind_args_t const *args,
    openbsd_kbind_result_t     *result)
{
	(void)xus;
	(void)xmon;
	(void)args;
	(void)result;

	LCLog(g_bsd_log, LCLogWarning, 0,
	      "unimplemented OpenBSD 7.9 syscall `kbind'", 0);

	return (ENOSYS);
}

int
openbsd_accept4(
    nix_us_syscall_if_t         *xus,
    nix_monitor_t               *xmon,
    openbsd_accept4_args_t const *args,
    openbsd_accept4_result_t     *result)
{
	(void)xus;
	(void)xmon;
	(void)args;
	(void)result;

	LCLog(g_bsd_log, LCLogWarning, 0,
	      "unimplemented OpenBSD 7.9 syscall `accept4'", 0);

	return (ENOSYS);
}

int
openbsd_getdents(
    nix_us_syscall_if_t          *xus,
    nix_monitor_t                *xmon,
    openbsd_getdents_args_t const *args,
    openbsd_getdents_result_t     *result)
{
	(void)xus;
	(void)xmon;
	(void)args;
	(void)result;

	LCLog(g_bsd_log, LCLogWarning, 0,
	      "unimplemented OpenBSD 7.9 syscall `getdents'", 0);

	return (ENOSYS);
}

int
openbsd_pipe2(
    nix_us_syscall_if_t       *xus,
    nix_monitor_t             *xmon,
    openbsd_pipe2_args_t const *args,
    openbsd_pipe2_result_t     *result)
{
	(void)xus;
	(void)xmon;
	(void)args;
	(void)result;

	LCLog(g_bsd_log, LCLogWarning, 0,
	      "unimplemented OpenBSD 7.9 syscall `pipe2'", 0);

	return (ENOSYS);
}

int
openbsd_dup3(
    nix_us_syscall_if_t      *xus,
    nix_monitor_t            *xmon,
    openbsd_dup3_args_t const *args,
    openbsd_dup3_result_t     *result)
{
	(void)xus;
	(void)xmon;
	(void)args;
	(void)result;

	LCLog(g_bsd_log, LCLogWarning, 0,
	      "unimplemented OpenBSD 7.9 syscall `dup3'", 0);

	return (ENOSYS);
}

int
openbsd_chflagsat(
    nix_us_syscall_if_t           *xus,
    nix_monitor_t                 *xmon,
    openbsd_chflagsat_args_t const *args,
    openbsd_chflagsat_result_t     *result)
{
	(void)xus;
	(void)xmon;
	(void)args;
	(void)result;

	LCLog(g_bsd_log, LCLogWarning, 0,
	      "unimplemented OpenBSD 7.9 syscall `chflagsat'", 0);

	return (ENOSYS);
}

int
openbsd_pledge(
    nix_us_syscall_if_t        *xus,
    nix_monitor_t              *xmon,
    openbsd_pledge_args_t const *args,
    openbsd_pledge_result_t     *result)
{
	(void)xus;
	(void)xmon;
	(void)args;
	(void)result;

	LCLog(g_bsd_log, LCLogWarning, 0,
	      "unimplemented OpenBSD 7.9 syscall `pledge'", 0);

	return (ENOSYS);
}

int
openbsd_ppoll(
    nix_us_syscall_if_t       *xus,
    nix_monitor_t             *xmon,
    openbsd_ppoll_args_t const *args,
    openbsd_ppoll_result_t     *result)
{
	(void)xus;
	(void)xmon;
	(void)args;
	(void)result;

	LCLog(g_bsd_log, LCLogWarning, 0,
	      "unimplemented OpenBSD 7.9 syscall `ppoll'", 0);

	return (ENOSYS);
}

int
openbsd_pselect(
    nix_us_syscall_if_t         *xus,
    nix_monitor_t               *xmon,
    openbsd_pselect_args_t const *args,
    openbsd_pselect_result_t     *result)
{
	(void)xus;
	(void)xmon;
	(void)args;
	(void)result;

	LCLog(g_bsd_log, LCLogWarning, 0,
	      "unimplemented OpenBSD 7.9 syscall `pselect'", 0);

	return (ENOSYS);
}

int
openbsd_sendsyslog(
    nix_us_syscall_if_t            *xus,
    nix_monitor_t                  *xmon,
    openbsd_sendsyslog_args_t const *args,
    openbsd_sendsyslog_result_t     *result)
{
	(void)xus;
	(void)xmon;
	(void)args;
	(void)result;

	LCLog(g_bsd_log, LCLogWarning, 0,
	      "unimplemented OpenBSD 7.9 syscall `sendsyslog'", 0);

	return (ENOSYS);
}

int
openbsd_unveil(
    nix_us_syscall_if_t        *xus,
    nix_monitor_t              *xmon,
    openbsd_unveil_args_t const *args,
    openbsd_unveil_result_t     *result)
{
	(void)xus;
	(void)xmon;
	(void)args;
	(void)result;

	LCLog(g_bsd_log, LCLogWarning, 0,
	      "unimplemented OpenBSD 7.9 syscall `unveil'", 0);

	return (ENOSYS);
}

int
openbsd___realpath(
    nix_us_syscall_if_t            *xus,
    nix_monitor_t                  *xmon,
    openbsd___realpath_args_t const *args,
    openbsd___realpath_result_t     *result)
{
	(void)xus;
	(void)xmon;
	(void)args;
	(void)result;

	LCLog(g_bsd_log, LCLogWarning, 0,
	      "unimplemented OpenBSD 7.9 syscall `__realpath'", 0);

	return (ENOSYS);
}

int
openbsd_recvmmsg(
    nix_us_syscall_if_t          *xus,
    nix_monitor_t                *xmon,
    openbsd_recvmmsg_args_t const *args,
    openbsd_recvmmsg_result_t     *result)
{
	(void)xus;
	(void)xmon;
	(void)args;
	(void)result;

	LCLog(g_bsd_log, LCLogWarning, 0,
	      "unimplemented OpenBSD 7.9 syscall `recvmmsg'", 0);

	return (ENOSYS);
}

int
openbsd_sendmmsg(
    nix_us_syscall_if_t          *xus,
    nix_monitor_t                *xmon,
    openbsd_sendmmsg_args_t const *args,
    openbsd_sendmmsg_result_t     *result)
{
	(void)xus;
	(void)xmon;
	(void)args;
	(void)result;

	LCLog(g_bsd_log, LCLogWarning, 0,
	      "unimplemented OpenBSD 7.9 syscall `sendmmsg'", 0);

	return (ENOSYS);
}

int
openbsd_thrkill(
    nix_us_syscall_if_t         *xus,
    nix_monitor_t               *xmon,
    openbsd_thrkill_args_t const *args,
    openbsd_thrkill_result_t     *result)
{
	(void)xus;
	(void)xmon;
	(void)args;
	(void)result;

	LCLog(g_bsd_log, LCLogWarning, 0,
	      "unimplemented OpenBSD 7.9 syscall `thrkill'", 0);

	return (ENOSYS);
}

int
openbsd___pledge_open(
    nix_us_syscall_if_t               *xus,
    nix_monitor_t                     *xmon,
    openbsd___pledge_open_args_t const *args,
    openbsd___pledge_open_result_t     *result)
{
	(void)xus;
	(void)xmon;
	(void)args;
	(void)result;

	LCLog(g_bsd_log, LCLogWarning, 0,
	      "unimplemented OpenBSD 7.9 syscall `__pledge_open'", 0);

	return (ENOSYS);
}

int
openbsd_getlogin_r(
    nix_us_syscall_if_t            *xus,
    nix_monitor_t                  *xmon,
    openbsd_getlogin_r_args_t const *args,
    openbsd_getlogin_r_result_t     *result)
{
	(void)xus;
	(void)xmon;
	(void)args;
	(void)result;

	LCLog(g_bsd_log, LCLogWarning, 0,
	      "unimplemented OpenBSD 7.9 syscall `getlogin_r'", 0);

	return (ENOSYS);
}

int
openbsd_getthrname(
    nix_us_syscall_if_t            *xus,
    nix_monitor_t                  *xmon,
    openbsd_getthrname_args_t const *args,
    openbsd_getthrname_result_t     *result)
{
	(void)xus;
	(void)xmon;
	(void)args;
	(void)result;

	LCLog(g_bsd_log, LCLogWarning, 0,
	      "unimplemented OpenBSD 7.9 syscall `getthrname'", 0);

	return (ENOSYS);
}

int
openbsd_setthrname(
    nix_us_syscall_if_t            *xus,
    nix_monitor_t                  *xmon,
    openbsd_setthrname_args_t const *args,
    openbsd_setthrname_result_t     *result)
{
	(void)xus;
	(void)xmon;
	(void)args;
	(void)result;

	LCLog(g_bsd_log, LCLogWarning, 0,
	      "unimplemented OpenBSD 7.9 syscall `setthrname'", 0);

	return (ENOSYS);
}

int
openbsd_ypconnect(
    nix_us_syscall_if_t           *xus,
    nix_monitor_t                 *xmon,
    openbsd_ypconnect_args_t const *args,
    openbsd_ypconnect_result_t     *result)
{
	(void)xus;
	(void)xmon;
	(void)args;
	(void)result;

	LCLog(g_bsd_log, LCLogWarning, 0,
	      "unimplemented OpenBSD 7.9 syscall `ypconnect'", 0);

	return (ENOSYS);
}

int
openbsd_pinsyscalls(
    nix_us_syscall_if_t             *xus,
    nix_monitor_t                   *xmon,
    openbsd_pinsyscalls_args_t const *args,
    openbsd_pinsyscalls_result_t     *result)
{
	(void)xus;
	(void)xmon;
	(void)args;
	(void)result;

	LCLog(g_bsd_log, LCLogWarning, 0,
	      "unimplemented OpenBSD 7.9 syscall `pinsyscalls'", 0);

	return (ENOSYS);
}

int
openbsd_mimmutable(
    nix_us_syscall_if_t            *xus,
    nix_monitor_t                  *xmon,
    openbsd_mimmutable_args_t const *args,
    openbsd_mimmutable_result_t     *result)
{
	(void)xus;
	(void)xmon;
	(void)args;
	(void)result;

	LCLog(g_bsd_log, LCLogWarning, 0,
	      "unimplemented OpenBSD 7.9 syscall `mimmutable'", 0);

	return (ENOSYS);
}

int
openbsd_waitid(
    nix_us_syscall_if_t        *xus,
    nix_monitor_t              *xmon,
    openbsd_waitid_args_t const *args,
    openbsd_waitid_result_t     *result)
{
	(void)xus;
	(void)xmon;
	(void)args;
	(void)result;

	LCLog(g_bsd_log, LCLogWarning, 0,
	      "unimplemented OpenBSD 7.9 syscall `waitid'", 0);

	return (ENOSYS);
}

int
openbsd_pathconfat(
    nix_us_syscall_if_t            *xus,
    nix_monitor_t                  *xmon,
    openbsd_pathconfat_args_t const *args,
    openbsd_pathconfat_result_t     *result)
{
	(void)xus;
	(void)xmon;
	(void)args;
	(void)result;

	LCLog(g_bsd_log, LCLogWarning, 0,
	      "unimplemented OpenBSD 7.9 syscall `pathconfat'", 0);

	return (ENOSYS);
}

int
openbsd_utrace(
    nix_us_syscall_if_t        *xus,
    nix_monitor_t              *xmon,
    openbsd_utrace_args_t const *args,
    openbsd_utrace_result_t     *result)
{
	(void)xus;
	(void)xmon;
	(void)args;
	(void)result;

	LCLog(g_bsd_log, LCLogWarning, 0,
	      "unimplemented OpenBSD 7.9 syscall `utrace'", 0);

	return (ENOSYS);
}

int
openbsd_kqueue1(
    nix_us_syscall_if_t         *xus,
    nix_monitor_t               *xmon,
    openbsd_kqueue1_args_t const *args,
    openbsd_kqueue1_result_t     *result)
{
	(void)xus;
	(void)xmon;
	(void)args;
	(void)result;

	LCLog(g_bsd_log, LCLogWarning, 0,
	      "unimplemented OpenBSD 7.9 syscall `kqueue1'", 0);

	return (ENOSYS);
}

int
openbsd_setrtable(
    nix_us_syscall_if_t           *xus,
    nix_monitor_t                 *xmon,
    openbsd_setrtable_args_t const *args,
    openbsd_setrtable_result_t     *result)
{
	(void)xus;
	(void)xmon;
	(void)args;
	(void)result;

	LCLog(g_bsd_log, LCLogWarning, 0,
	      "unimplemented OpenBSD 7.9 syscall `setrtable'", 0);

	return (ENOSYS);
}

int
openbsd_getrtable(
    nix_us_syscall_if_t           *xus,
    nix_monitor_t                 *xmon,
    openbsd_getrtable_args_t const *args,
    openbsd_getrtable_result_t     *result)
{
	(void)xus;
	(void)xmon;
	(void)args;
	(void)result;

	LCLog(g_bsd_log, LCLogWarning, 0,
	      "unimplemented OpenBSD 7.9 syscall `getrtable'", 0);

	return (ENOSYS);
}

int
openbsd_faccessat(
    nix_us_syscall_if_t           *xus,
    nix_monitor_t                 *xmon,
    openbsd_faccessat_args_t const *args,
    openbsd_faccessat_result_t     *result)
{
	(void)xus;
	(void)xmon;
	(void)args;
	(void)result;

	LCLog(g_bsd_log, LCLogWarning, 0,
	      "unimplemented OpenBSD 7.9 syscall `faccessat'", 0);

	return (ENOSYS);
}

int
openbsd_fchmodat(
    nix_us_syscall_if_t          *xus,
    nix_monitor_t                *xmon,
    openbsd_fchmodat_args_t const *args,
    openbsd_fchmodat_result_t     *result)
{
	(void)xus;
	(void)xmon;
	(void)args;
	(void)result;

	LCLog(g_bsd_log, LCLogWarning, 0,
	      "unimplemented OpenBSD 7.9 syscall `fchmodat'", 0);

	return (ENOSYS);
}

int
openbsd_fchownat(
    nix_us_syscall_if_t          *xus,
    nix_monitor_t                *xmon,
    openbsd_fchownat_args_t const *args,
    openbsd_fchownat_result_t     *result)
{
	(void)xus;
	(void)xmon;
	(void)args;
	(void)result;

	LCLog(g_bsd_log, LCLogWarning, 0,
	      "unimplemented OpenBSD 7.9 syscall `fchownat'", 0);

	return (ENOSYS);
}

int
openbsd_linkat(
    nix_us_syscall_if_t        *xus,
    nix_monitor_t              *xmon,
    openbsd_linkat_args_t const *args,
    openbsd_linkat_result_t     *result)
{
	(void)xus;
	(void)xmon;
	(void)args;
	(void)result;

	LCLog(g_bsd_log, LCLogWarning, 0,
	      "unimplemented OpenBSD 7.9 syscall `linkat'", 0);

	return (ENOSYS);
}

int
openbsd_mkdirat(
    nix_us_syscall_if_t         *xus,
    nix_monitor_t               *xmon,
    openbsd_mkdirat_args_t const *args,
    openbsd_mkdirat_result_t     *result)
{
	(void)xus;
	(void)xmon;
	(void)args;
	(void)result;

	LCLog(g_bsd_log, LCLogWarning, 0,
	      "unimplemented OpenBSD 7.9 syscall `mkdirat'", 0);

	return (ENOSYS);
}

int
openbsd_mkfifoat(
    nix_us_syscall_if_t          *xus,
    nix_monitor_t                *xmon,
    openbsd_mkfifoat_args_t const *args,
    openbsd_mkfifoat_result_t     *result)
{
	(void)xus;
	(void)xmon;
	(void)args;
	(void)result;

	LCLog(g_bsd_log, LCLogWarning, 0,
	      "unimplemented OpenBSD 7.9 syscall `mkfifoat'", 0);

	return (ENOSYS);
}

int
openbsd_mknodat(
    nix_us_syscall_if_t         *xus,
    nix_monitor_t               *xmon,
    openbsd_mknodat_args_t const *args,
    openbsd_mknodat_result_t     *result)
{
	(void)xus;
	(void)xmon;
	(void)args;
	(void)result;

	LCLog(g_bsd_log, LCLogWarning, 0,
	      "unimplemented OpenBSD 7.9 syscall `mknodat'", 0);

	return (ENOSYS);
}

int
openbsd_openat(
    nix_us_syscall_if_t        *xus,
    nix_monitor_t              *xmon,
    openbsd_openat_args_t const *args,
    openbsd_openat_result_t     *result)
{
	(void)xus;
	(void)xmon;
	(void)args;
	(void)result;

	LCLog(g_bsd_log, LCLogWarning, 0,
	      "unimplemented OpenBSD 7.9 syscall `openat'", 0);

	return (ENOSYS);
}

int
openbsd_readlinkat(
    nix_us_syscall_if_t            *xus,
    nix_monitor_t                  *xmon,
    openbsd_readlinkat_args_t const *args,
    openbsd_readlinkat_result_t     *result)
{
	(void)xus;
	(void)xmon;
	(void)args;
	(void)result;

	LCLog(g_bsd_log, LCLogWarning, 0,
	      "unimplemented OpenBSD 7.9 syscall `readlinkat'", 0);

	return (ENOSYS);
}

int
openbsd_renameat(
    nix_us_syscall_if_t          *xus,
    nix_monitor_t                *xmon,
    openbsd_renameat_args_t const *args,
    openbsd_renameat_result_t     *result)
{
	(void)xus;
	(void)xmon;
	(void)args;
	(void)result;

	LCLog(g_bsd_log, LCLogWarning, 0,
	      "unimplemented OpenBSD 7.9 syscall `renameat'", 0);

	return (ENOSYS);
}

int
openbsd_symlinkat(
    nix_us_syscall_if_t           *xus,
    nix_monitor_t                 *xmon,
    openbsd_symlinkat_args_t const *args,
    openbsd_symlinkat_result_t     *result)
{
	(void)xus;
	(void)xmon;
	(void)args;
	(void)result;

	LCLog(g_bsd_log, LCLogWarning, 0,
	      "unimplemented OpenBSD 7.9 syscall `symlinkat'", 0);

	return (ENOSYS);
}

int
openbsd_unlinkat(
    nix_us_syscall_if_t          *xus,
    nix_monitor_t                *xmon,
    openbsd_unlinkat_args_t const *args,
    openbsd_unlinkat_result_t     *result)
{
	(void)xus;
	(void)xmon;
	(void)args;
	(void)result;

	LCLog(g_bsd_log, LCLogWarning, 0,
	      "unimplemented OpenBSD 7.9 syscall `unlinkat'", 0);

	return (ENOSYS);
}

int
openbsd___set_tcb(
    nix_us_syscall_if_t           *xus,
    nix_monitor_t                 *xmon,
    openbsd___set_tcb_args_t const *args,
    openbsd___set_tcb_result_t     *result)
{
	(void)xus;
	(void)xmon;
	(void)args;
	(void)result;

	LCLog(g_bsd_log, LCLogWarning, 0,
	      "unimplemented OpenBSD 7.9 syscall `__set_tcb'", 0);

	return (ENOSYS);
}

int
openbsd___get_tcb(
    nix_us_syscall_if_t           *xus,
    nix_monitor_t                 *xmon,
    openbsd___get_tcb_args_t const *args,
    openbsd___get_tcb_result_t     *result)
{
	(void)xus;
	(void)xmon;
	(void)args;
	(void)result;

	LCLog(g_bsd_log, LCLogWarning, 0,
	      "unimplemented OpenBSD 7.9 syscall `__get_tcb'", 0);

	return (ENOSYS);
}
