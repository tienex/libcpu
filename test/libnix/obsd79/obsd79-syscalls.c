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
#include "obsd79-syscalls.h"
#include "obsd79-us-syscall-priv.h"

#include "nix.h"
#include "nix-fd.h"
#include "openbsd79.h"
#include "arc4random.h"

#include "obsd79-args.h"
#include "obsd79-sysctl.h"
#include "obsd79-mman.h"

#include "nix-host.h" /* XXX */

#define GE32(gi, x) \
	(((gi)->endian != NIX_ENDIAN_NATIVE) ? nix_byte_swap_int32(x) : (x))

#define GE64(gi, x) \
	(((gi)->endian != NIX_ENDIAN_NATIVE) ? nix_byte_swap_int64(x) : (x))

void *g_bsd_log = NULL;

static __inline struct nix_iovec *
__obsd79_iovec32_copy_from(nix_env_t              *env,
                           nix_mem_if_t           *mem,
                           nix_guest_info_t const *gi,
                           struct obsd79_iovec32  *iov,
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
__obsd79_pollfd_copy_from(nix_env_t              *env,
                          nix_guest_info_t const *gi,
                          struct obsd79_pollfd   *fds,
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
				obsd79_pollfd_to_nix_pollfd(gi->endian, fds + n, xfds + n);
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
obsd79_exit(
    nix_us_syscall_if_t      *xus,
    nix_monitor_t            *xmon,
    obsd79_exit_args_t const *args,
    obsd79_exit_result_t     *result)
{
	LCLog(g_bsd_log, LCLogDebug, 0, "invoked", 0);

	nix_exit(args->arg0);
	return (0); /* Not reached (should)! */
}

int
obsd79_fork(
    nix_us_syscall_if_t      *xus,
    nix_monitor_t            *xmon,
    obsd79_fork_args_t const *args,
    obsd79_fork_result_t     *result)
{
	nix_env_t *env = obsd79_us_syscall_get_nix_env(xus);

	LCLog(g_bsd_log, LCLogDebug, 0, "invoked", 0);

	nix_env_set_errno(env, 0);
	*result = nix_fork(env);
	return (nix_env_get_errno(env));
}

int
obsd79_read(
    nix_us_syscall_if_t      *xus,
    nix_monitor_t            *xmon,
    obsd79_read_args_t const *args,
    obsd79_read_result_t     *result)
{
	nix_env_t *env = obsd79_us_syscall_get_nix_env(xus);

	LCLog(g_bsd_log, LCLogDebug, 0, "invoked", 0);

	nix_env_set_errno(env, 0);
	*result = nix_read(args->arg0, args->arg1, args->arg2, env);
	return (nix_env_get_errno(env));
}

int
obsd79_write(
    nix_us_syscall_if_t       *xus,
    nix_monitor_t             *xmon,
    obsd79_write_args_t const *args,
    obsd79_write_result_t     *result)
{
	nix_env_t *env = obsd79_us_syscall_get_nix_env(xus);

	LCLog(g_bsd_log, LCLogDebug, 0, "invoked", 0);

	nix_env_set_errno(env, 0);
	*result = nix_write(args->arg0, args->arg1, args->arg2, env);
	return (nix_env_get_errno(env));
}

int
obsd79_open(
    nix_us_syscall_if_t      *xus,
    nix_monitor_t            *xmon,
    obsd79_open_args_t const *args,
    obsd79_open_result_t     *result)
{
	nix_env_t *env = obsd79_us_syscall_get_nix_env(xus);

	LCLog(g_bsd_log, LCLogDebug, 0, "invoked", 0);

	nix_env_set_errno(env, 0);
	*result = nix_open((char const *)args->arg0, args->arg1, args->arg2, env);
	return (nix_env_get_errno(env));
}

int
obsd79_close(
    nix_us_syscall_if_t       *xus,
    nix_monitor_t             *xmon,
    obsd79_close_args_t const *args,
    obsd79_close_result_t     *result)
{
	nix_env_t *env = obsd79_us_syscall_get_nix_env(xus);

	LCLog(g_bsd_log, LCLogDebug, 0, "invoked", 0);

	nix_env_set_errno(env, 0);
	*result = nix_close(args->arg0, env);
	return (nix_env_get_errno(env));
}

int
obsd79_wait4(
    nix_us_syscall_if_t       *xus,
    nix_monitor_t             *xmon,
    obsd79_wait4_args_t const *args,
    obsd79_wait4_result_t     *result)
{
	nix_env_t *env = obsd79_us_syscall_get_nix_env(xus);

	LCLog(g_bsd_log, LCLogDebug, 0, "invoked", 0);

	nix_env_set_errno(env, 0);
	*result = nix_bsd_wait4(args->arg0, args->arg1, args->arg2, (struct nix_rusage *)(uintptr_t)args->arg3, env);
	return (nix_env_get_errno(env));
}

int
obsd79_link(
    nix_us_syscall_if_t      *xus,
    nix_monitor_t            *xmon,
    obsd79_link_args_t const *args,
    obsd79_link_result_t     *result)
{
	nix_env_t *env = obsd79_us_syscall_get_nix_env(xus);

	LCLog(g_bsd_log, LCLogDebug, 0, "invoked", 0);

	nix_env_set_errno(env, 0);
	*result = nix_link((char const *)args->arg0, (char const *)args->arg1, env);
	return (nix_env_get_errno(env));
}

int
obsd79_unlink(
    nix_us_syscall_if_t        *xus,
    nix_monitor_t              *xmon,
    obsd79_unlink_args_t const *args,
    obsd79_unlink_result_t     *result)
{
	nix_env_t *env = obsd79_us_syscall_get_nix_env(xus);

	LCLog(g_bsd_log, LCLogDebug, 0, "invoked", 0);

	nix_env_set_errno(env, 0);
	*result = nix_unlink((char const *)args->arg0, env);
	return (nix_env_get_errno(env));
}

int
obsd79_chdir(
    nix_us_syscall_if_t       *xus,
    nix_monitor_t             *xmon,
    obsd79_chdir_args_t const *args,
    obsd79_chdir_result_t     *result)
{
	nix_env_t *env = obsd79_us_syscall_get_nix_env(xus);

	LCLog(g_bsd_log, LCLogDebug, 0, "invoked", 0);

	nix_env_set_errno(env, 0);
	*result = nix_chdir((char const *)args->arg0, env);
	return (nix_env_get_errno(env));
}

int
obsd79_fchdir(
    nix_us_syscall_if_t        *xus,
    nix_monitor_t              *xmon,
    obsd79_fchdir_args_t const *args,
    obsd79_fchdir_result_t     *result)
{
	nix_env_t *env = obsd79_us_syscall_get_nix_env(xus);

	LCLog(g_bsd_log, LCLogDebug, 0, "invoked", 0);

	nix_env_set_errno(env, 0);
	*result = nix_fchdir(args->arg0, env);
	return (nix_env_get_errno(env));
}

int
obsd79_mknod(
    nix_us_syscall_if_t       *xus,
    nix_monitor_t             *xmon,
    obsd79_mknod_args_t const *args,
    obsd79_mknod_result_t     *result)
{
	nix_env_t *env = obsd79_us_syscall_get_nix_env(xus);

	LCLog(g_bsd_log, LCLogDebug, 0, "invoked", 0);

	nix_env_set_errno(env, 0);
	*result = nix_mknod((char const *)args->arg0, args->arg1, args->arg2, env);
	return (nix_env_get_errno(env));
}

int
obsd79_chmod(
    nix_us_syscall_if_t       *xus,
    nix_monitor_t             *xmon,
    obsd79_chmod_args_t const *args,
    obsd79_chmod_result_t     *result)
{
	nix_env_t *env = obsd79_us_syscall_get_nix_env(xus);

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
obsd79_chown(
    nix_us_syscall_if_t       *xus,
    nix_monitor_t             *xmon,
    obsd79_chown_args_t const *args,
    obsd79_chown_result_t     *result)
{
	nix_env_t *env = obsd79_us_syscall_get_nix_env(xus);

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
obsd79_getpid(
    nix_us_syscall_if_t        *xus,
    nix_monitor_t              *xmon,
    obsd79_getpid_args_t const *args,
    obsd79_getpid_result_t     *result)
{
	nix_env_t *env = obsd79_us_syscall_get_nix_env(xus);

	LCLog(g_bsd_log, LCLogDebug, 0, "invoked", 0);

	nix_env_set_errno(env, 0);
	*result = nix_getpid(env);
	return (nix_env_get_errno(env));
}

int
obsd79_mount(
    nix_us_syscall_if_t       *xus,
    nix_monitor_t             *xmon,
    obsd79_mount_args_t const *args,
    obsd79_mount_result_t     *result)
{
	nix_env_t *env = obsd79_us_syscall_get_nix_env(xus);

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
obsd79_setuid(
    nix_us_syscall_if_t        *xus,
    nix_monitor_t              *xmon,
    obsd79_setuid_args_t const *args,
    obsd79_setuid_result_t     *result)
{
	nix_env_t *env = obsd79_us_syscall_get_nix_env(xus);

	LCLog(g_bsd_log, LCLogDebug, 0, "invoked", 0);

	nix_env_set_errno(env, 0);
	*result = nix_setuid(args->arg0, env);
	return (nix_env_get_errno(env));
}

int
obsd79_getuid(
    nix_us_syscall_if_t        *xus,
    nix_monitor_t              *xmon,
    obsd79_getuid_args_t const *args,
    obsd79_getuid_result_t     *result)
{
	nix_env_t *env = obsd79_us_syscall_get_nix_env(xus);

	LCLog(g_bsd_log, LCLogDebug, 0, "invoked", 0);

	nix_env_set_errno(env, 0);
	*result = nix_getuid(env);
	return (nix_env_get_errno(env));
}

int
obsd79_geteuid(
    nix_us_syscall_if_t         *xus,
    nix_monitor_t               *xmon,
    obsd79_geteuid_args_t const *args,
    obsd79_geteuid_result_t     *result)
{
	nix_env_t *env = obsd79_us_syscall_get_nix_env(xus);

	LCLog(g_bsd_log, LCLogDebug, 0, "invoked", 0);

	nix_env_set_errno(env, 0);
	*result = nix_geteuid(env);
	return (nix_env_get_errno(env));
}

int
obsd79_ptrace(
    nix_us_syscall_if_t        *xus,
    nix_monitor_t              *xmon,
    obsd79_ptrace_args_t const *args,
    obsd79_ptrace_result_t     *result)
{
	nix_env_t *env = obsd79_us_syscall_get_nix_env(xus);

	LCLog(g_bsd_log, LCLogDebug, 0, "invoked", 0);

	nix_env_set_errno(env, 0);
	*result = nix_ptrace(args->arg0, args->arg1, (void *)(uintptr_t)args->arg2, args->arg3, env);
	return (nix_env_get_errno(env));
}

int
obsd79_recvmsg(
    nix_us_syscall_if_t         *xus,
    nix_monitor_t               *xmon,
    obsd79_recvmsg_args_t const *args,
    obsd79_recvmsg_result_t     *result)
{
	LCLog(g_bsd_log, LCLogDebug, 0, "invoked", 0);

	/* Insert here implementation code */

	return (ENOSYS);
}

int
obsd79_sendmsg(
    nix_us_syscall_if_t         *xus,
    nix_monitor_t               *xmon,
    obsd79_sendmsg_args_t const *args,
    obsd79_sendmsg_result_t     *result)
{
	LCLog(g_bsd_log, LCLogDebug, 0, "invoked", 0);

	/* Insert here implementation code */

	return (ENOSYS);
}

int
obsd79_recvfrom(
    nix_us_syscall_if_t          *xus,
    nix_monitor_t                *xmon,
    obsd79_recvfrom_args_t const *args,
    obsd79_recvfrom_result_t     *result)
{
	nix_guest_info_t     gi;
	struct nix_sockaddr  sa;
	int                  rc = -1;
	nix_socklen_t        salen = sizeof(sa);
	struct nix_sockaddr *psa = args->arg4 != NULL ? &sa : NULL;
	nix_socklen_t       *psalen = args->arg4 != NULL ? &salen : NULL;
	nix_env_t           *env = obsd79_us_syscall_get_nix_env(xus);

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
			if (!nix_sockaddr_to_obsd79_sockaddr(gi.endian, psa, *psalen,
			                                     args->arg4, (obsd79_socklen_t *)args->arg5)) {
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
obsd79_accept(
    nix_us_syscall_if_t        *xus,
    nix_monitor_t              *xmon,
    obsd79_accept_args_t const *args,
    obsd79_accept_result_t     *result)
{
	LCLog(g_bsd_log, LCLogDebug, 0, "invoked", 0);

	/* Insert here implementation code */

	return (ENOSYS);
}

int
obsd79_getpeername(
    nix_us_syscall_if_t             *xus,
    nix_monitor_t                   *xmon,
    obsd79_getpeername_args_t const *args,
    obsd79_getpeername_result_t     *result)
{
	nix_guest_info_t    gi;
	struct nix_sockaddr sa;
	nix_socklen_t       salen = sizeof(sa);
	nix_env_t          *env = obsd79_us_syscall_get_nix_env(xus);

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
		if (!nix_sockaddr_to_obsd79_sockaddr(gi.endian, &sa, salen,
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
obsd79_getsockname(
    nix_us_syscall_if_t             *xus,
    nix_monitor_t                   *xmon,
    obsd79_getsockname_args_t const *args,
    obsd79_getsockname_result_t     *result)
{
	nix_guest_info_t    gi;
	struct nix_sockaddr sa;
	nix_socklen_t       salen = sizeof(sa);
	nix_env_t          *env = obsd79_us_syscall_get_nix_env(xus);

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
		if (!nix_sockaddr_to_obsd79_sockaddr(gi.endian, &sa, salen, args->arg1, args->arg2)) {
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
obsd79_access(
    nix_us_syscall_if_t        *xus,
    nix_monitor_t              *xmon,
    obsd79_access_args_t const *args,
    obsd79_access_result_t     *result)
{
	nix_env_t *env = obsd79_us_syscall_get_nix_env(xus);

	LCLog(g_bsd_log, LCLogDebug, 0, "invoked", 0);

	nix_env_set_errno(env, 0);
	*result = nix_access((char const *)args->arg0, args->arg1, env);
	return (nix_env_get_errno(env));
}

int
obsd79_chflags(
    nix_us_syscall_if_t         *xus,
    nix_monitor_t               *xmon,
    obsd79_chflags_args_t const *args,
    obsd79_chflags_result_t     *result)
{
	nix_env_t *env = obsd79_us_syscall_get_nix_env(xus);

	LCLog(g_bsd_log, LCLogDebug, 0, "invoked", 0);

	nix_env_set_errno(env, 0);
	*result = nix_bsd_chflags((char const *)args->arg0, args->arg1, env);
	return (nix_env_get_errno(env));
}

int
obsd79_fchflags(
    nix_us_syscall_if_t          *xus,
    nix_monitor_t                *xmon,
    obsd79_fchflags_args_t const *args,
    obsd79_fchflags_result_t     *result)
{
	nix_env_t *env = obsd79_us_syscall_get_nix_env(xus);

	LCLog(g_bsd_log, LCLogDebug, 0, "invoked", 0);

	nix_env_set_errno(env, 0);
	*result = nix_bsd_fchflags(args->arg0, args->arg1, env);
	return (nix_env_get_errno(env));
}

int
obsd79_sync(
    nix_us_syscall_if_t      *xus,
    nix_monitor_t            *xmon,
    obsd79_sync_args_t const *args,
    obsd79_sync_result_t     *result)
{
	nix_env_t *env = obsd79_us_syscall_get_nix_env(xus);

	LCLog(g_bsd_log, LCLogDebug, 0, "invoked", 0);

	nix_env_set_errno(env, 0);
	nix_sync(env);
	return (nix_env_get_errno(env));
}

int
obsd79_kill(
    nix_us_syscall_if_t      *xus,
    nix_monitor_t            *xmon,
    obsd79_kill_args_t const *args,
    obsd79_kill_result_t     *result)
{
	nix_env_t *env = obsd79_us_syscall_get_nix_env(xus);

	LCLog(g_bsd_log, LCLogDebug, 0, "invoked", 0);

	nix_env_set_errno(env, 0);
	*result = nix_kill(args->arg0, args->arg1, env);
	return (nix_env_get_errno(env));
}

int
obsd79_getppid(
    nix_us_syscall_if_t         *xus,
    nix_monitor_t               *xmon,
    obsd79_getppid_args_t const *args,
    obsd79_getppid_result_t     *result)
{
	nix_env_t *env = obsd79_us_syscall_get_nix_env(xus);

	LCLog(g_bsd_log, LCLogDebug, 0, "invoked", 0);

	nix_env_set_errno(env, 0);
	*result = nix_getppid(env);
	return (nix_env_get_errno(env));
}

int
obsd79_dup(
    nix_us_syscall_if_t     *xus,
    nix_monitor_t           *xmon,
    obsd79_dup_args_t const *args,
    obsd79_dup_result_t     *result)
{
	nix_env_t *env = obsd79_us_syscall_get_nix_env(xus);

	LCLog(g_bsd_log, LCLogDebug, 0, "invoked", 0);

	nix_env_set_errno(env, 0);
	*result = nix_dup(args->arg0, env);
	return (nix_env_get_errno(env));
}

int
obsd79_getegid(
    nix_us_syscall_if_t         *xus,
    nix_monitor_t               *xmon,
    obsd79_getegid_args_t const *args,
    obsd79_getegid_result_t     *result)
{
	nix_env_t *env = obsd79_us_syscall_get_nix_env(xus);

	LCLog(g_bsd_log, LCLogDebug, 0, "invoked", 0);

	nix_env_set_errno(env, 0);
	*result = nix_getegid(env);
	return (nix_env_get_errno(env));
}

int
obsd79_profil(
    nix_us_syscall_if_t        *xus,
    nix_monitor_t              *xmon,
    obsd79_profil_args_t const *args,
    obsd79_profil_result_t     *result)
{
	nix_env_t *env = obsd79_us_syscall_get_nix_env(xus);

	LCLog(g_bsd_log, LCLogDebug, 0, "invoked", 0);

	nix_env_set_errno(env, 0);
	*result = nix_bsd_profil(args->arg0, args->arg1, args->arg2, args->arg3, env);
	return (nix_env_get_errno(env));
}

int
obsd79_ktrace(
    nix_us_syscall_if_t        *xus,
    nix_monitor_t              *xmon,
    obsd79_ktrace_args_t const *args,
    obsd79_ktrace_result_t     *result)
{
	nix_env_t *env = obsd79_us_syscall_get_nix_env(xus);

	LCLog(g_bsd_log, LCLogDebug, 0, "invoked", 0);

	nix_env_set_errno(env, 0);
	*result = nix_bsd_ktrace(args->arg0, args->arg1, args->arg2, args->arg3, env);
	return (nix_env_get_errno(env));
}

int
obsd79_sigaction(
    nix_us_syscall_if_t           *xus,
    nix_monitor_t                 *xmon,
    obsd79_sigaction_args_t const *args,
    obsd79_sigaction_result_t     *result)
{
	nix_guest_info_t      gi;
	struct nix_sigaction  sa;
	struct nix_sigaction  osa;
	struct nix_sigaction *psa = args->arg1 != NULL ? &sa : NULL;
	struct nix_sigaction *posa = args->arg2 != NULL ? &osa : NULL;
	nix_env_t            *env = obsd79_us_syscall_get_nix_env(xus);

	LCLog(g_bsd_log, LCLogDebug, 0, "invoked", 0);

	if (psa == NULL && posa == NULL)
		return (EINVAL);

	nix_monitor_get_guest_info(xmon, &gi);

	if (psa != NULL) {
		__nix_try
		{
			obsd79_sigaction32_to_nix_sigaction(gi.endian, args->arg1, psa);
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
			nix_sigaction_to_obsd79_sigaction32(gi.endian, posa, args->arg2);
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
obsd79_getgid(
    nix_us_syscall_if_t        *xus,
    nix_monitor_t              *xmon,
    obsd79_getgid_args_t const *args,
    obsd79_getgid_result_t     *result)
{
	nix_env_t *env = obsd79_us_syscall_get_nix_env(xus);

	LCLog(g_bsd_log, LCLogDebug, 0, "invoked", 0);

	nix_env_set_errno(env, 0);
	*result = nix_getgid(env);
	return (nix_env_get_errno(env));
}

int
obsd79_sigprocmask(
    nix_us_syscall_if_t             *xus,
    nix_monitor_t                   *xmon,
    obsd79_sigprocmask_args_t const *args,
    obsd79_sigprocmask_result_t     *result)
{
	int          howto;
	nix_sigset_t oset;
	nix_sigset_t nset = args->arg1;
	nix_env_t   *env = obsd79_us_syscall_get_nix_env(xus);

	LCLog(g_bsd_log, LCLogDebug, 0, "invoked", 0);

	nix_env_set_errno(env, 0);
	switch (args->arg0) {
	case OBSD79_SIG_BLOCK:
		howto = NIX_SIG_BLOCK;
		break;
	case OBSD79_SIG_UNBLOCK:
		howto = NIX_SIG_UNBLOCK;
		break;
	case OBSD79_SIG_SETMASK:
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
obsd79_setlogin(
    nix_us_syscall_if_t          *xus,
    nix_monitor_t                *xmon,
    obsd79_setlogin_args_t const *args,
    obsd79_setlogin_result_t     *result)
{
	nix_env_t *env = obsd79_us_syscall_get_nix_env(xus);

	LCLog(g_bsd_log, LCLogDebug, 0, "invoked", 0);

	nix_env_set_errno(env, 0);
	*result = nix_bsd_setlogin((char const *)args->arg0, env);
	return (nix_env_get_errno(env));
}

int
obsd79_acct(
    nix_us_syscall_if_t      *xus,
    nix_monitor_t            *xmon,
    obsd79_acct_args_t const *args,
    obsd79_acct_result_t     *result)
{
	nix_env_t *env = obsd79_us_syscall_get_nix_env(xus);

	LCLog(g_bsd_log, LCLogDebug, 0, "invoked", 0);

	nix_env_set_errno(env, 0);
	*result = nix_bsd_acct((char const *)args->arg0, env);
	return (nix_env_get_errno(env));
}

int
obsd79_sigpending(
    nix_us_syscall_if_t            *xus,
    nix_monitor_t                  *xmon,
    obsd79_sigpending_args_t const *args,
    obsd79_sigpending_result_t     *result)
{
	nix_env_t *env = obsd79_us_syscall_get_nix_env(xus);

	LCLog(g_bsd_log, LCLogDebug, 0, "invoked", 0);

	nix_env_set_errno(env, 0);
	*result = 0; /* XXX */
	return (nix_env_get_errno(env));
}

int
obsd79_ioctl(
    nix_us_syscall_if_t       *xus,
    nix_monitor_t             *xmon,
    obsd79_ioctl_args_t const *args,
    obsd79_ioctl_result_t     *result)
{
	nix_guest_info_t gi;
	nix_env_t       *env = obsd79_us_syscall_get_nix_env(xus);

	LCLog(g_bsd_log, LCLogDebug, 0, "invoked", 0);

	nix_monitor_get_guest_info(xmon, &gi);

	nix_env_set_errno(env, 0);
	*result = obsd79_ioctl_dispatch(env, gi.endian, args->arg0, args->arg1, args->arg2);
	return (nix_env_get_errno(env));
}

int
obsd79_reboot(
    nix_us_syscall_if_t        *xus,
    nix_monitor_t              *xmon,
    obsd79_reboot_args_t const *args,
    obsd79_reboot_result_t     *result)
{
	nix_env_t *env = obsd79_us_syscall_get_nix_env(xus);

	LCLog(g_bsd_log, LCLogDebug, 0, "invoked", 0);

	nix_env_set_errno(env, 0);
	*result = nix_reboot(args->arg0, env);
	return (nix_env_get_errno(env));
}

int
obsd79_revoke(
    nix_us_syscall_if_t        *xus,
    nix_monitor_t              *xmon,
    obsd79_revoke_args_t const *args,
    obsd79_revoke_result_t     *result)
{
	nix_env_t *env = obsd79_us_syscall_get_nix_env(xus);

	LCLog(g_bsd_log, LCLogDebug, 0, "invoked", 0);

	nix_env_set_errno(env, 0);
	*result = nix_bsd_revoke((char const *)args->arg0, env);
	return (nix_env_get_errno(env));
}

int
obsd79_symlink(
    nix_us_syscall_if_t         *xus,
    nix_monitor_t               *xmon,
    obsd79_symlink_args_t const *args,
    obsd79_symlink_result_t     *result)
{
	nix_env_t *env = obsd79_us_syscall_get_nix_env(xus);

	LCLog(g_bsd_log, LCLogDebug, 0, "invoked", 0);

	nix_env_set_errno(env, 0);
	*result = nix_symlink((char const *)args->arg0, (char const *)args->arg1, env);
	return (nix_env_get_errno(env));
}

int
obsd79_readlink(
    nix_us_syscall_if_t          *xus,
    nix_monitor_t                *xmon,
    obsd79_readlink_args_t const *args,
    obsd79_readlink_result_t     *result)
{
	nix_env_t *env = obsd79_us_syscall_get_nix_env(xus);

	LCLog(g_bsd_log, LCLogDebug, 0, "invoked", 0);

	nix_env_set_errno(env, 0);
	*result = nix_readlink((char *)args->arg0, args->arg1, args->arg2, env);
	return (nix_env_get_errno(env));
}

int
obsd79_execve(
    nix_us_syscall_if_t        *xus,
    nix_monitor_t              *xmon,
    obsd79_execve_args_t const *args,
    obsd79_execve_result_t     *result)
{
	LCLog(g_bsd_log, LCLogDebug, 0, "invoked", 0);

	/* Insert here implementation code */

	return (ENOSYS);
}

int
obsd79_umask(
    nix_us_syscall_if_t       *xus,
    nix_monitor_t             *xmon,
    obsd79_umask_args_t const *args,
    obsd79_umask_result_t     *result)
{
	nix_env_t *env = obsd79_us_syscall_get_nix_env(xus);

	LCLog(g_bsd_log, LCLogDebug, 0, "invoked", 0);

	nix_env_set_errno(env, 0);
	*result = nix_umask(args->arg0, env);
	return (nix_env_get_errno(env));
}

int
obsd79_chroot(
    nix_us_syscall_if_t        *xus,
    nix_monitor_t              *xmon,
    obsd79_chroot_args_t const *args,
    obsd79_chroot_result_t     *result)
{
	nix_env_t *env = obsd79_us_syscall_get_nix_env(xus);

	LCLog(g_bsd_log, LCLogDebug, 0, "invoked", 0);

	nix_env_set_errno(env, 0);
	*result = nix_chroot((char const *)args->arg0, env);
	return (nix_env_get_errno(env));
}

int
obsd79_vfork(
    nix_us_syscall_if_t       *xus,
    nix_monitor_t             *xmon,
    obsd79_vfork_args_t const *args,
    obsd79_vfork_result_t     *result)
{
	nix_env_t *env = obsd79_us_syscall_get_nix_env(xus);

	LCLog(g_bsd_log, LCLogDebug, 0, "invoked", 0);

	nix_env_set_errno(env, 0);
	*result = nix_vfork(env);
	return (nix_env_get_errno(env));
}

int
obsd79_munmap(
    nix_us_syscall_if_t        *xus,
    nix_monitor_t              *xmon,
    obsd79_munmap_args_t const *args,
    obsd79_munmap_result_t     *result)
{
	nix_env_t *env = obsd79_us_syscall_get_nix_env(xus);

	LCLog(g_bsd_log, LCLogDebug, 0, "invoked", 0);

	nix_env_set_errno(env, 0);
	*result = nix_munmap((uintmax_t)(uintptr_t)args->arg0, args->arg1, env);
	return (nix_env_get_errno(env));
}

int
obsd79_mprotect(
    nix_us_syscall_if_t          *xus,
    nix_monitor_t                *xmon,
    obsd79_mprotect_args_t const *args,
    obsd79_mprotect_result_t     *result)
{
	nix_env_t *env = obsd79_us_syscall_get_nix_env(xus);

	LCLog(g_bsd_log, LCLogDebug, 0, "invoked", 0);

	nix_env_set_errno(env, 0);
	*result = nix_mprotect((uintmax_t)(uintptr_t)args->arg0, args->arg1, args->arg2, env);
	return (nix_env_get_errno(env));
}

int
obsd79_madvise(
    nix_us_syscall_if_t         *xus,
    nix_monitor_t               *xmon,
    obsd79_madvise_args_t const *args,
    obsd79_madvise_result_t     *result)
{
	nix_env_t *env = obsd79_us_syscall_get_nix_env(xus);

	LCLog(g_bsd_log, LCLogDebug, 0, "invoked", 0);

	nix_env_set_errno(env, 0);
	*result = nix_madvise((uintmax_t)(uintptr_t)args->arg0, args->arg1, args->arg2, env);
	return (nix_env_get_errno(env));
}

int
obsd79_getgroups(
    nix_us_syscall_if_t           *xus,
    nix_monitor_t                 *xmon,
    obsd79_getgroups_args_t const *args,
    obsd79_getgroups_result_t     *result)
{
	//	nix_env_t *env = obsd79_us_syscall_get_nix_env(xus);

	LCLog(g_bsd_log, LCLogDebug, 0, "invoked", 0);

	return (ENOSYS);
}

int
obsd79_setgroups(
    nix_us_syscall_if_t           *xus,
    nix_monitor_t                 *xmon,
    obsd79_setgroups_args_t const *args,
    obsd79_setgroups_result_t     *result)
{
	LCLog(g_bsd_log, LCLogDebug, 0, "invoked", 0);

	/* Insert here implementation code */

	return (ENOSYS);
}

int
obsd79_getpgrp(
    nix_us_syscall_if_t         *xus,
    nix_monitor_t               *xmon,
    obsd79_getpgrp_args_t const *args,
    obsd79_getpgrp_result_t     *result)
{
	nix_env_t *env = obsd79_us_syscall_get_nix_env(xus);

	LCLog(g_bsd_log, LCLogDebug, 0, "invoked", 0);

	nix_env_set_errno(env, 0);
	*result = nix_getpgrp(env);
	return (nix_env_get_errno(env));
}

int
obsd79_setpgid(
    nix_us_syscall_if_t         *xus,
    nix_monitor_t               *xmon,
    obsd79_setpgid_args_t const *args,
    obsd79_setpgid_result_t     *result)
{
	nix_env_t *env = obsd79_us_syscall_get_nix_env(xus);

	LCLog(g_bsd_log, LCLogDebug, 0, "invoked", 0);

	nix_env_set_errno(env, 0);
	*result = nix_setpgid(args->arg0, args->arg1, env);
	return (nix_env_get_errno(env));
}

int
obsd79_setitimer(
    nix_us_syscall_if_t           *xus,
    nix_monitor_t                 *xmon,
    obsd79_setitimer_args_t const *args,
    obsd79_setitimer_result_t     *result)
{
	LCLog(g_bsd_log, LCLogDebug, 0, "invoked", 0);

	/* Insert here implementation code */

	return (ENOSYS);
}

int
obsd79_getitimer(
    nix_us_syscall_if_t           *xus,
    nix_monitor_t                 *xmon,
    obsd79_getitimer_args_t const *args,
    obsd79_getitimer_result_t     *result)
{
	LCLog(g_bsd_log, LCLogDebug, 0, "invoked", 0);

	/* Insert here implementation code */

	return (ENOSYS);
}

int
obsd79_dup2(
    nix_us_syscall_if_t      *xus,
    nix_monitor_t            *xmon,
    obsd79_dup2_args_t const *args,
    obsd79_dup2_result_t     *result)
{
	nix_env_t *env = obsd79_us_syscall_get_nix_env(xus);

	LCLog(g_bsd_log, LCLogDebug, 0, "invoked", 0);

	nix_env_set_errno(env, 0);
	*result = nix_dup2(args->arg0, args->arg1, env);
	return (nix_env_get_errno(env));
}

int
obsd79_fcntl(
    nix_us_syscall_if_t       *xus,
    nix_monitor_t             *xmon,
    obsd79_fcntl_args_t const *args,
    obsd79_fcntl_result_t     *result)
{
	nix_env_t *env = obsd79_us_syscall_get_nix_env(xus);

	LCLog(g_bsd_log, LCLogDebug, 0, "invoked", 0);

	nix_env_set_errno(env, 0);
	*result = nix_fcntl(args->arg0, args->arg1, args->arg2, env);
	return (nix_env_get_errno(env));
}

int
obsd79_select(
    nix_us_syscall_if_t        *xus,
    nix_monitor_t              *xmon,
    obsd79_select_args_t const *args,
    obsd79_select_result_t     *result)
{
	nix_guest_info_t       gi;
	struct nix_timeval     ntv;
	nix_fd_set             fds[3];
	struct obsd79_timeval *ptv = args->arg4;
	struct nix_timeval    *pntv = ptv != NULL ? &ntv : NULL;
	nix_env_t             *env = obsd79_us_syscall_get_nix_env(xus);
	nix_fd_set            *pfds[3] = {NULL, NULL, NULL};

	nix_monitor_get_guest_info(xmon, &gi);

	LCLog(g_bsd_log, LCLogDebug, 0, "invoked", 0);

	if (ptv != NULL)
		obsd79_timeval_to_nix_timeval(gi.endian, ptv, pntv);

	__nix_try
	{
		if (args->arg1 != NULL) {
			pfds[0] = &fds[0];
			obsd79_fd_set_to_nix_fd_set(gi.endian, args->arg1, pfds[0]);
		}

		if (args->arg2 != NULL) {
			pfds[1] = &fds[1];
			obsd79_fd_set_to_nix_fd_set(gi.endian, args->arg2, pfds[1]);
		}

		if (args->arg3 != NULL) {
			pfds[2] = &fds[2];
			obsd79_fd_set_to_nix_fd_set(gi.endian, args->arg3, pfds[2]);
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
			nix_fd_set_to_obsd79_fd_set(gi.endian, pfds[0], args->arg1);
		if (pfds[1] != NULL)
			nix_fd_set_to_obsd79_fd_set(gi.endian, pfds[1], args->arg2);
		if (pfds[2] != NULL)
			nix_fd_set_to_obsd79_fd_set(gi.endian, pfds[2], args->arg3);
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
obsd79_fsync(
    nix_us_syscall_if_t       *xus,
    nix_monitor_t             *xmon,
    obsd79_fsync_args_t const *args,
    obsd79_fsync_result_t     *result)
{
	nix_env_t *env = obsd79_us_syscall_get_nix_env(xus);

	LCLog(g_bsd_log, LCLogDebug, 0, "invoked", 0);

	nix_env_set_errno(env, 0);
	*result = nix_fsync(args->arg0, env);
	return (nix_env_get_errno(env));
}

int
obsd79_setpriority(
    nix_us_syscall_if_t             *xus,
    nix_monitor_t                   *xmon,
    obsd79_setpriority_args_t const *args,
    obsd79_setpriority_result_t     *result)
{
	nix_env_t *env = obsd79_us_syscall_get_nix_env(xus);

	LCLog(g_bsd_log, LCLogDebug, 0, "invoked", 0);

	nix_env_set_errno(env, 0);
	*result = nix_setpriority(args->arg0, args->arg1, args->arg2, env);
	return (nix_env_get_errno(env));
}

int
obsd79_socket(
    nix_us_syscall_if_t        *xus,
    nix_monitor_t              *xmon,
    obsd79_socket_args_t const *args,
    obsd79_socket_result_t     *result)
{
	nix_env_t *env = obsd79_us_syscall_get_nix_env(xus);

	LCLog(g_bsd_log, LCLogDebug, 0, "invoked", 0);

	nix_env_set_errno(env, 0);
	*result = nix_socket(args->arg0, args->arg1, args->arg2, env);
	return (nix_env_get_errno(env));
}

int
obsd79_connect(
    nix_us_syscall_if_t         *xus,
    nix_monitor_t               *xmon,
    obsd79_connect_args_t const *args,
    obsd79_connect_result_t     *result)
{
	nix_guest_info_t    gi;
	int                 rc;
	struct nix_sockaddr sa;
	nix_socklen_t       salen = sizeof(sa);
	nix_env_t          *env = obsd79_us_syscall_get_nix_env(xus);

	LCLog(g_bsd_log, LCLogDebug, 0, "invoked", 0);

	if (args->arg1 == NULL)
		return (EFAULT);

	nix_monitor_get_guest_info(xmon, &gi);

	rc = 0;

	__nix_try
	{
		if (!obsd79_sockaddr_to_nix_sockaddr(gi.endian, args->arg1, args->arg2, &sa, &salen)) {
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
obsd79_getpriority(
    nix_us_syscall_if_t             *xus,
    nix_monitor_t                   *xmon,
    obsd79_getpriority_args_t const *args,
    obsd79_getpriority_result_t     *result)
{

	LCLog(g_bsd_log, LCLogDebug, 0, "invoked", 0);

	/* Insert here implementation code */

	return (ENOSYS);
}

int
obsd79_sigreturn(
    nix_us_syscall_if_t           *xus,
    nix_monitor_t                 *xmon,
    obsd79_sigreturn_args_t const *args,
    obsd79_sigreturn_result_t     *result)
{
	LCLog(g_bsd_log, LCLogDebug, 0, "invoked", 0);

	/* Insert here implementation code */

	return (ENOSYS);
}

int
obsd79_bind(
    nix_us_syscall_if_t      *xus,
    nix_monitor_t            *xmon,
    obsd79_bind_args_t const *args,
    obsd79_bind_result_t     *result)
{
	nix_guest_info_t    gi;
	int                 rc;
	struct nix_sockaddr sa;
	nix_socklen_t       salen = sizeof(sa);
	nix_env_t          *env = obsd79_us_syscall_get_nix_env(xus);

	LCLog(g_bsd_log, LCLogDebug, 0, "invoked", 0);

	if (args->arg1 == NULL)
		return (EFAULT);

	nix_monitor_get_guest_info(xmon, &gi);

	rc = 0;

	__nix_try
	{
		if (!obsd79_sockaddr_to_nix_sockaddr(gi.endian, args->arg1, args->arg2, &sa, &salen)) {
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
obsd79_setsockopt(
    nix_us_syscall_if_t            *xus,
    nix_monitor_t                  *xmon,
    obsd79_setsockopt_args_t const *args,
    obsd79_setsockopt_result_t     *result)
{
	LCLog(g_bsd_log, LCLogDebug, 0, "invoked", 0);

	/* Insert here implementation code */

	*result = 0;
	return (0); // ENOSYS;
}

int
obsd79_listen(
    nix_us_syscall_if_t        *xus,
    nix_monitor_t              *xmon,
    obsd79_listen_args_t const *args,
    obsd79_listen_result_t     *result)
{
	nix_env_t *env = obsd79_us_syscall_get_nix_env(xus);

	LCLog(g_bsd_log, LCLogDebug, 0, "invoked", 0);

	nix_env_set_errno(env, 0);
	*result = nix_listen(args->arg0, args->arg1, env);
	return (nix_env_get_errno(env));
}

int
obsd79_sigsuspend(
    nix_us_syscall_if_t            *xus,
    nix_monitor_t                  *xmon,
    obsd79_sigsuspend_args_t const *args,
    obsd79_sigsuspend_result_t     *result)
{
	LCLog(g_bsd_log, LCLogDebug, 0, "invoked", 0);

	/* Insert here implementation code */

	return (ENOSYS);
}

int
obsd79_gettimeofday(
    nix_us_syscall_if_t              *xus,
    nix_monitor_t                    *xmon,
    obsd79_gettimeofday_args_t const *args,
    obsd79_gettimeofday_result_t     *result)
{
	nix_guest_info_t     gi;
	struct nix_timeval   tv;
	struct nix_timezone  tz;
	struct nix_timeval  *ptv = args->arg0 != NULL ? &tv : NULL;
	struct nix_timezone *ptz = args->arg1 != NULL ? &tz : NULL;
	nix_env_t           *env = obsd79_us_syscall_get_nix_env(xus);

	LCLog(g_bsd_log, LCLogDebug, 0, "invoked", 0);

	if (ptv == NULL && ptz == NULL)
		return (EINVAL);

	nix_monitor_get_guest_info(xmon, &gi);

	nix_env_set_errno(env, 0);
	__nix_try
	{
		*result = nix_gettimeofday(ptv, ptz, env);

		if (ptv != NULL)
			nix_timeval_to_obsd79_timeval(gi.endian, ptv, args->arg0);
		if (ptz != NULL)
			nix_timezone_to_obsd79_timezone(gi.endian, ptz, args->arg1);
	}
	__nix_catch_any
	{
		nix_env_set_errno(env, EFAULT);
	}
	__nix_end_try

	    return (nix_env_get_errno(env));
}

int
obsd79_getrusage(
    nix_us_syscall_if_t           *xus,
    nix_monitor_t                 *xmon,
    obsd79_getrusage_args_t const *args,
    obsd79_getrusage_result_t     *result)
{
	nix_guest_info_t   gi;
	struct nix_rusage  ru;
	struct nix_rusage *pru = args->arg1 != NULL ? &ru : NULL;
	nix_env_t         *env = obsd79_us_syscall_get_nix_env(xus);

	LCLog(g_bsd_log, LCLogDebug, 0, "invoked", 0);

	if (pru == NULL)
		return (EINVAL);

	nix_monitor_get_guest_info(xmon, &gi);

	nix_env_set_errno(env, 0);

	*result = nix_getrusage(args->arg0, pru, env);

	__nix_try
	{
		nix_rusage_to_obsd79_rusage(gi.endian, pru, args->arg1);
	}
	__nix_catch_any
	{
		nix_env_set_errno(env, EFAULT);
	}
	__nix_end_try

	    return (nix_env_get_errno(env));
}

int
obsd79_getsockopt(
    nix_us_syscall_if_t            *xus,
    nix_monitor_t                  *xmon,
    obsd79_getsockopt_args_t const *args,
    obsd79_getsockopt_result_t     *result)
{
	LCLog(g_bsd_log, LCLogDebug, 0, "invoked", 0);

	/* Insert here implementation code */
	*result = 0;
	return (0); // ENOSYS;
}

int
obsd79_readv(
    nix_us_syscall_if_t       *xus,
    nix_monitor_t             *xmon,
    obsd79_readv_args_t const *args,
    obsd79_readv_result_t     *result)
{
	nix_guest_info_t  gi;
	struct nix_iovec *xiov;
	nix_env_t        *env = obsd79_us_syscall_get_nix_env(xus);
	nix_mem_if_t     *mem = nix_monitor_get_memory(xmon);

	LCLog(g_bsd_log, LCLogDebug, 0, "invoked", 0);

#if 0	
	nix_monitor_get_guest_info (xmon, &gi);
#else
	/*XXX */
	gi.endian = NIX_ENDIAN_BIG;
#endif

	xiov = __obsd79_iovec32_copy_from(env, mem, &gi, args->arg1, args->arg2);
	if (xiov != NULL) {
		*result = nix_readv(args->arg0, xiov, args->arg2, env);
		nix_free(xiov);
	}

	return (nix_env_get_errno(env));
}

int
obsd79_writev(
    nix_us_syscall_if_t        *xus,
    nix_monitor_t              *xmon,
    obsd79_writev_args_t const *args,
    obsd79_writev_result_t     *result)
{
	nix_guest_info_t  gi;
	struct nix_iovec *xiov;
	nix_env_t        *env = obsd79_us_syscall_get_nix_env(xus);
	nix_mem_if_t     *mem = nix_monitor_get_memory(xmon);

	LCLog(g_bsd_log, LCLogDebug, 0, "invoked", 0);

	nix_monitor_get_guest_info(xmon, &gi);

	xiov = __obsd79_iovec32_copy_from(env, mem, &gi, args->arg1, args->arg2);
	if (xiov != NULL) {
		*result = nix_writev(args->arg0, xiov, args->arg2, env);
		nix_free(xiov);
	}

	return (nix_env_get_errno(env));
}

int
obsd79_settimeofday(
    nix_us_syscall_if_t              *xus,
    nix_monitor_t                    *xmon,
    obsd79_settimeofday_args_t const *args,
    obsd79_settimeofday_result_t     *result)
{
	LCLog(g_bsd_log, LCLogDebug, 0, "invoked", 0);

	/* Insert here implementation code */

	return (ENOSYS);
}

int
obsd79_fchown(
    nix_us_syscall_if_t        *xus,
    nix_monitor_t              *xmon,
    obsd79_fchown_args_t const *args,
    obsd79_fchown_result_t     *result)
{
	nix_env_t *env = obsd79_us_syscall_get_nix_env(xus);

	LCLog(g_bsd_log, LCLogDebug, 0, "invoked", 0);

	nix_env_set_errno(env, 0);
	*result = nix_fchown(args->arg0, args->arg1, args->arg2, env);
	return (nix_env_get_errno(env));
}

int
obsd79_fchmod(
    nix_us_syscall_if_t        *xus,
    nix_monitor_t              *xmon,
    obsd79_fchmod_args_t const *args,
    obsd79_fchmod_result_t     *result)
{
	nix_env_t *env = obsd79_us_syscall_get_nix_env(xus);

	LCLog(g_bsd_log, LCLogDebug, 0, "invoked", 0);

	nix_env_set_errno(env, 0);
	*result = nix_fchmod(args->arg0, args->arg1, env);
	return (nix_env_get_errno(env));
}

int
obsd79_setreuid(
    nix_us_syscall_if_t          *xus,
    nix_monitor_t                *xmon,
    obsd79_setreuid_args_t const *args,
    obsd79_setreuid_result_t     *result)
{
	nix_env_t *env = obsd79_us_syscall_get_nix_env(xus);

	LCLog(g_bsd_log, LCLogDebug, 0, "invoked", 0);

	nix_env_set_errno(env, 0);
	*result = nix_setreuid(args->arg0, args->arg1, env);
	return (nix_env_get_errno(env));
}

int
obsd79_setregid(
    nix_us_syscall_if_t          *xus,
    nix_monitor_t                *xmon,
    obsd79_setregid_args_t const *args,
    obsd79_setregid_result_t     *result)
{
	nix_env_t *env = obsd79_us_syscall_get_nix_env(xus);

	LCLog(g_bsd_log, LCLogDebug, 0, "invoked", 0);

	nix_env_set_errno(env, 0);
	*result = nix_setregid(args->arg0, args->arg1, env);
	return (nix_env_get_errno(env));
}

int
obsd79_rename(
    nix_us_syscall_if_t        *xus,
    nix_monitor_t              *xmon,
    obsd79_rename_args_t const *args,
    obsd79_rename_result_t     *result)
{
	nix_env_t *env = obsd79_us_syscall_get_nix_env(xus);

	LCLog(g_bsd_log, LCLogDebug, 0, "invoked", 0);

	nix_env_set_errno(env, 0);
	*result = nix_rename((char const *)args->arg0, (char const *)args->arg1, env);
	return (nix_env_get_errno(env));
}

int
obsd79_flock(
    nix_us_syscall_if_t       *xus,
    nix_monitor_t             *xmon,
    obsd79_flock_args_t const *args,
    obsd79_flock_result_t     *result)
{
	nix_env_t *env = obsd79_us_syscall_get_nix_env(xus);

	LCLog(g_bsd_log, LCLogDebug, 0, "invoked", 0);

	nix_env_set_errno(env, 0);
	*result = nix_flock(args->arg0, args->arg1, env);
	return (nix_env_get_errno(env));
}

int
obsd79_mkfifo(
    nix_us_syscall_if_t        *xus,
    nix_monitor_t              *xmon,
    obsd79_mkfifo_args_t const *args,
    obsd79_mkfifo_result_t     *result)
{
	nix_env_t *env = obsd79_us_syscall_get_nix_env(xus);

	LCLog(g_bsd_log, LCLogDebug, 0, "invoked", 0);

	nix_env_set_errno(env, 0);
	*result = nix_mkfifo((char const *)args->arg0, args->arg1, env);
	return (nix_env_get_errno(env));
}

int
obsd79_sendto(
    nix_us_syscall_if_t        *xus,
    nix_monitor_t              *xmon,
    obsd79_sendto_args_t const *args,
    obsd79_sendto_result_t     *result)
{
	nix_guest_info_t     gi;
	struct nix_sockaddr  sa;
	int                  rc = -1;
	struct nix_sockaddr *psa = args->arg4 != NULL ? &sa : NULL;
	nix_socklen_t        salen = sizeof(sa);
	nix_env_t           *env = obsd79_us_syscall_get_nix_env(xus);

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
			if (!obsd79_sockaddr_to_nix_sockaddr(gi.endian, args->arg4, args->arg5, &sa, &salen)) {
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
obsd79_shutdown(
    nix_us_syscall_if_t          *xus,
    nix_monitor_t                *xmon,
    obsd79_shutdown_args_t const *args,
    obsd79_shutdown_result_t     *result)
{
	nix_env_t *env = obsd79_us_syscall_get_nix_env(xus);

	LCLog(g_bsd_log, LCLogDebug, 0, "invoked", 0);

	nix_env_set_errno(env, 0);
	*result = nix_shutdown(args->arg0, args->arg1, env);
	return (nix_env_get_errno(env));
}

int
obsd79_socketpair(
    nix_us_syscall_if_t            *xus,
    nix_monitor_t                  *xmon,
    obsd79_socketpair_args_t const *args,
    obsd79_socketpair_result_t     *result)
{
	LCLog(g_bsd_log, LCLogDebug, 0, "invoked", 0);

	/* Insert here implementation code */

	return (ENOSYS);
}

int
obsd79_mkdir(
    nix_us_syscall_if_t       *xus,
    nix_monitor_t             *xmon,
    obsd79_mkdir_args_t const *args,
    obsd79_mkdir_result_t     *result)
{
	nix_env_t *env = obsd79_us_syscall_get_nix_env(xus);

	LCLog(g_bsd_log, LCLogDebug, 0, "invoked", 0);

	nix_env_set_errno(env, 0);
	*result = nix_mkdir((char const *)args->arg0, args->arg1, env);
	return (nix_env_get_errno(env));
}

int
obsd79_rmdir(
    nix_us_syscall_if_t       *xus,
    nix_monitor_t             *xmon,
    obsd79_rmdir_args_t const *args,
    obsd79_rmdir_result_t     *result)
{
	nix_env_t *env = obsd79_us_syscall_get_nix_env(xus);

	LCLog(g_bsd_log, LCLogDebug, 0, "invoked", 0);

	nix_env_set_errno(env, 0);
	*result = nix_rmdir((char const *)args->arg0, env);
	return (nix_env_get_errno(env));
}

int
obsd79_utimes(
    nix_us_syscall_if_t        *xus,
    nix_monitor_t              *xmon,
    obsd79_utimes_args_t const *args,
    obsd79_utimes_result_t     *result)
{
	LCLog(g_bsd_log, LCLogDebug, 0, "invoked", 0);

	/* Insert here implementation code */

	return (ENOSYS);
}

int
obsd79_adjtime(
    nix_us_syscall_if_t         *xus,
    nix_monitor_t               *xmon,
    obsd79_adjtime_args_t const *args,
    obsd79_adjtime_result_t     *result)
{
	LCLog(g_bsd_log, LCLogDebug, 0, "invoked", 0);

	/* Insert here implementation code */

	return (ENOSYS);
}

int
obsd79_setsid(
    nix_us_syscall_if_t        *xus,
    nix_monitor_t              *xmon,
    obsd79_setsid_args_t const *args,
    obsd79_setsid_result_t     *result)
{
	nix_env_t *env = obsd79_us_syscall_get_nix_env(xus);

	LCLog(g_bsd_log, LCLogDebug, 0, "invoked", 0);

	nix_env_set_errno(env, 0);
	*result = nix_setsid(env);
	return (nix_env_get_errno(env));
}

int
obsd79_quotactl(
    nix_us_syscall_if_t          *xus,
    nix_monitor_t                *xmon,
    obsd79_quotactl_args_t const *args,
    obsd79_quotactl_result_t     *result)
{
	LCLog(g_bsd_log, LCLogDebug, 0, "invoked", 0);

	/* Insert here implementation code */

	return (ENOSYS);
}

int
obsd79_nfssvc(
    nix_us_syscall_if_t        *xus,
    nix_monitor_t              *xmon,
    obsd79_nfssvc_args_t const *args,
    obsd79_nfssvc_result_t     *result)
{
	LCLog(g_bsd_log, LCLogDebug, 0, "invoked", 0);

	/* Insert here implementation code */

	return (ENOSYS);
}

int
obsd79_getfh(
    nix_us_syscall_if_t       *xus,
    nix_monitor_t             *xmon,
    obsd79_getfh_args_t const *args,
    obsd79_getfh_result_t     *result)
{
	LCLog(g_bsd_log, LCLogDebug, 0, "invoked", 0);

	/* Insert here implementation code */

	return (ENOSYS);
}

int
obsd79_sysarch(
    nix_us_syscall_if_t         *xus,
    nix_monitor_t               *xmon,
    obsd79_sysarch_args_t const *args,
    obsd79_sysarch_result_t     *result)
{
	LCLog(g_bsd_log, LCLogDebug, 0, "invoked", 0);

	/* Insert here implementation code */

	return (ENOSYS);
}

int
obsd79_pread(
    nix_us_syscall_if_t       *xus,
    nix_monitor_t             *xmon,
    obsd79_pread_args_t const *args,
    obsd79_pread_result_t     *result)
{
	nix_env_t *env = obsd79_us_syscall_get_nix_env(xus);

	LCLog(g_bsd_log, LCLogDebug, 0, "invoked", 0);

	nix_env_set_errno(env, 0);
	*result = nix_pread(args->arg0, args->arg1, args->arg2, args->arg3, env);
	return (nix_env_get_errno(env));
}

int
obsd79_pwrite(
    nix_us_syscall_if_t        *xus,
    nix_monitor_t              *xmon,
    obsd79_pwrite_args_t const *args,
    obsd79_pwrite_result_t     *result)
{
	nix_env_t *env = obsd79_us_syscall_get_nix_env(xus);

	LCLog(g_bsd_log, LCLogDebug, 0, "invoked", 0);

	nix_env_set_errno(env, 0);
	*result = nix_pwrite(args->arg0, args->arg1, args->arg2, args->arg3, env);
	return (nix_env_get_errno(env));
}

int
obsd79_setgid(
    nix_us_syscall_if_t        *xus,
    nix_monitor_t              *xmon,
    obsd79_setgid_args_t const *args,
    obsd79_setgid_result_t     *result)
{
	nix_env_t *env = obsd79_us_syscall_get_nix_env(xus);

	LCLog(g_bsd_log, LCLogDebug, 0, "invoked", 0);

	nix_env_set_errno(env, 0);
	*result = nix_setgid(args->arg0, env);
	return (nix_env_get_errno(env));
}

int
obsd79_setegid(
    nix_us_syscall_if_t         *xus,
    nix_monitor_t               *xmon,
    obsd79_setegid_args_t const *args,
    obsd79_setegid_result_t     *result)
{
	nix_env_t *env = obsd79_us_syscall_get_nix_env(xus);

	LCLog(g_bsd_log, LCLogDebug, 0, "invoked", 0);

	nix_env_set_errno(env, 0);
	*result = nix_setegid(args->arg0, env);
	return (nix_env_get_errno(env));
}

int
obsd79_seteuid(
    nix_us_syscall_if_t         *xus,
    nix_monitor_t               *xmon,
    obsd79_seteuid_args_t const *args,
    obsd79_seteuid_result_t     *result)
{
	nix_env_t *env = obsd79_us_syscall_get_nix_env(xus);

	LCLog(g_bsd_log, LCLogDebug, 0, "invoked", 0);

	nix_env_set_errno(env, 0);
	*result = nix_seteuid(args->arg0, env);
	return (nix_env_get_errno(env));
}

int
obsd79_pathconf(
    nix_us_syscall_if_t          *xus,
    nix_monitor_t                *xmon,
    obsd79_pathconf_args_t const *args,
    obsd79_pathconf_result_t     *result)
{
	nix_env_t *env = obsd79_us_syscall_get_nix_env(xus);

	LCLog(g_bsd_log, LCLogDebug, 0, "invoked", 0);

	nix_env_set_errno(env, 0);
	*result = nix_pathconf((char const *)args->arg0, args->arg1, env);
	return (nix_env_get_errno(env));
}

int
obsd79_fpathconf(
    nix_us_syscall_if_t           *xus,
    nix_monitor_t                 *xmon,
    obsd79_fpathconf_args_t const *args,
    obsd79_fpathconf_result_t     *result)
{
	nix_env_t *env = obsd79_us_syscall_get_nix_env(xus);

	LCLog(g_bsd_log, LCLogDebug, 0, "invoked", 0);

	nix_env_set_errno(env, 0);
	*result = nix_fpathconf(args->arg0, args->arg1, env);
	return (nix_env_get_errno(env));
}

int
obsd79_swapctl(
    nix_us_syscall_if_t         *xus,
    nix_monitor_t               *xmon,
    obsd79_swapctl_args_t const *args,
    obsd79_swapctl_result_t     *result)
{
	LCLog(g_bsd_log, LCLogDebug, 0, "invoked", 0);

	/* Insert here implementation code */

	return (ENOSYS);
}

int
obsd79_getrlimit(
    nix_us_syscall_if_t           *xus,
    nix_monitor_t                 *xmon,
    obsd79_getrlimit_args_t const *args,
    obsd79_getrlimit_result_t     *result)
{
	nix_guest_info_t  gi;
	int               resource;
	struct nix_rlimit rl;
	nix_env_t        *env = obsd79_us_syscall_get_nix_env(xus);

	LCLog(g_bsd_log, LCLogDebug, 0, "invoked", 0);

	if (args->arg1 == NULL)
		return (EFAULT);

	nix_monitor_get_guest_info(xmon, &gi);

	nix_env_set_errno(env, 0);
	switch (args->arg0) {
	case OBSD79_RLIMIT_CPU:
		resource = NIX_RLIMIT_CPU;
		break;
	case OBSD79_RLIMIT_CORE:
		resource = NIX_RLIMIT_CORE;
		break;
	case OBSD79_RLIMIT_DATA:
		resource = NIX_RLIMIT_DATA;
		break;
	case OBSD79_RLIMIT_FSIZE:
		resource = NIX_RLIMIT_FSIZE;
		break;
	case OBSD79_RLIMIT_MEMLOCK:
		resource = NIX_RLIMIT_MEMLOCK;
		break;
	case OBSD79_RLIMIT_NOFILE:
		resource = NIX_RLIMIT_NOFILE;
		break;
	case OBSD79_RLIMIT_NPROC:
		resource = NIX_RLIMIT_NPROC;
		break;
	case OBSD79_RLIMIT_RSS:
		resource = NIX_RLIMIT_RSS;
		break;
	case OBSD79_RLIMIT_STACK:
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
		nix_rlimit_to_obsd79_rlimit(gi.endian, &rl, args->arg1);
	}
	__nix_catch_any
	{
		nix_env_set_errno(env, EFAULT);
	}
	__nix_end_try

	    return (nix_env_get_errno(env));
}

int
obsd79_setrlimit(
    nix_us_syscall_if_t           *xus,
    nix_monitor_t                 *xmon,
    obsd79_setrlimit_args_t const *args,
    obsd79_setrlimit_result_t     *result)
{
	LCLog(g_bsd_log, LCLogDebug, 0, "invoked", 0);

	/* Insert here implementation code */

	*result = 0;
	return (0); // XXX ENOSYS;
}

int
obsd79_mmap(
    nix_us_syscall_if_t      *xus,
    nix_monitor_t            *xmon,
    obsd79_mmap_args_t const *args,
    obsd79_mmap_result_t     *result)
{
	int        prot;
	int        flags;
	nix_env_t *env = obsd79_us_syscall_get_nix_env(xus);

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
obsd79_lseek(
    nix_us_syscall_if_t       *xus,
    nix_monitor_t             *xmon,
    obsd79_lseek_args_t const *args,
    obsd79_lseek_result_t     *result)
{
	nix_env_t *env = obsd79_us_syscall_get_nix_env(xus);

	LCLog(g_bsd_log, LCLogDebug, 0, "invoked", 0);

	nix_env_set_errno(env, 0);
	*result = nix_lseek(args->arg0, args->arg1, args->arg2, env);
	return (nix_env_get_errno(env));
}

int
obsd79_truncate(
    nix_us_syscall_if_t          *xus,
    nix_monitor_t                *xmon,
    obsd79_truncate_args_t const *args,
    obsd79_truncate_result_t     *result)
{
	nix_env_t *env = obsd79_us_syscall_get_nix_env(xus);

	LCLog(g_bsd_log, LCLogDebug, 0, "invoked", 0);

	nix_env_set_errno(env, 0);
	*result = nix_truncate((char const *)args->arg0, args->arg1, env);
	return (nix_env_get_errno(env));
}

int
obsd79_ftruncate(
    nix_us_syscall_if_t           *xus,
    nix_monitor_t                 *xmon,
    obsd79_ftruncate_args_t const *args,
    obsd79_ftruncate_result_t     *result)
{
	nix_env_t *env = obsd79_us_syscall_get_nix_env(xus);

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
obsd79_mlock(
    nix_us_syscall_if_t       *xus,
    nix_monitor_t             *xmon,
    obsd79_mlock_args_t const *args,
    obsd79_mlock_result_t     *result)
{
	nix_env_t *env = obsd79_us_syscall_get_nix_env(xus);

	LCLog(g_bsd_log, LCLogDebug, 0, "invoked", 0);

	nix_env_set_errno(env, 0);
	*result = nix_mlock((uintmax_t)(uintptr_t)args->arg0, args->arg1, env);
	return (nix_env_get_errno(env));
}

int
obsd79_munlock(
    nix_us_syscall_if_t         *xus,
    nix_monitor_t               *xmon,
    obsd79_munlock_args_t const *args,
    obsd79_munlock_result_t     *result)
{
	nix_env_t *env = obsd79_us_syscall_get_nix_env(xus);

	LCLog(g_bsd_log, LCLogDebug, 0, "invoked", 0);

	nix_env_set_errno(env, 0);
	*result = nix_munlock((uintmax_t)(uintptr_t)args->arg0, args->arg1, env);
	return (nix_env_get_errno(env));
}

int
obsd79_futimes(
    nix_us_syscall_if_t         *xus,
    nix_monitor_t               *xmon,
    obsd79_futimes_args_t const *args,
    obsd79_futimes_result_t     *result)
{
	LCLog(g_bsd_log, LCLogDebug, 0, "invoked", 0);

	/* Insert here implementation code */

	return (ENOSYS);
}

int
obsd79_getpgid(
    nix_us_syscall_if_t         *xus,
    nix_monitor_t               *xmon,
    obsd79_getpgid_args_t const *args,
    obsd79_getpgid_result_t     *result)
{
	nix_env_t *env = obsd79_us_syscall_get_nix_env(xus);

	LCLog(g_bsd_log, LCLogDebug, 0, "invoked", 0);

	nix_env_set_errno(env, 0);
	*result = nix_getpgid(args->arg0, env);
	return (nix_env_get_errno(env));
}

int
obsd79_semget(
    nix_us_syscall_if_t        *xus,
    nix_monitor_t              *xmon,
    obsd79_semget_args_t const *args,
    obsd79_semget_result_t     *result)
{
	LCLog(g_bsd_log, LCLogDebug, 0, "invoked", 0);

	/* Insert here implementation code */

	return (ENOSYS);
}

int
obsd79_msgget(
    nix_us_syscall_if_t        *xus,
    nix_monitor_t              *xmon,
    obsd79_msgget_args_t const *args,
    obsd79_msgget_result_t     *result)
{
	LCLog(g_bsd_log, LCLogDebug, 0, "invoked", 0);

	/* Insert here implementation code */

	return (ENOSYS);
}

int
obsd79_msgsnd(
    nix_us_syscall_if_t        *xus,
    nix_monitor_t              *xmon,
    obsd79_msgsnd_args_t const *args,
    obsd79_msgsnd_result_t     *result)
{
	LCLog(g_bsd_log, LCLogDebug, 0, "invoked", 0);

	/* Insert here implementation code */

	return (ENOSYS);
}

int
obsd79_msgrcv(
    nix_us_syscall_if_t        *xus,
    nix_monitor_t              *xmon,
    obsd79_msgrcv_args_t const *args,
    obsd79_msgrcv_result_t     *result)
{
	LCLog(g_bsd_log, LCLogDebug, 0, "invoked", 0);

	/* Insert here implementation code */

	return (ENOSYS);
}

int
obsd79_shmat(
    nix_us_syscall_if_t       *xus,
    nix_monitor_t             *xmon,
    obsd79_shmat_args_t const *args,
    obsd79_shmat_result_t     *result)
{
	LCLog(g_bsd_log, LCLogDebug, 0, "invoked", 0);

	/* Insert here implementation code */

	return (ENOSYS);
}

int
obsd79_shmdt(
    nix_us_syscall_if_t       *xus,
    nix_monitor_t             *xmon,
    obsd79_shmdt_args_t const *args,
    obsd79_shmdt_result_t     *result)
{
	LCLog(g_bsd_log, LCLogDebug, 0, "invoked", 0);

	/* Insert here implementation code */

	return (ENOSYS);
}

int
obsd79_clock_gettime(
    nix_us_syscall_if_t               *xus,
    nix_monitor_t                     *xmon,
    obsd79_clock_gettime_args_t const *args,
    obsd79_clock_gettime_result_t     *result)
{
	LCLog(g_bsd_log, LCLogDebug, 0, "invoked", 0);

	/* Insert here implementation code */

	return (ENOSYS);
}

int
obsd79_clock_settime(
    nix_us_syscall_if_t               *xus,
    nix_monitor_t                     *xmon,
    obsd79_clock_settime_args_t const *args,
    obsd79_clock_settime_result_t     *result)
{
	LCLog(g_bsd_log, LCLogDebug, 0, "invoked", 0);

	/* Insert here implementation code */

	return (ENOSYS);
}

int
obsd79_clock_getres(
    nix_us_syscall_if_t              *xus,
    nix_monitor_t                    *xmon,
    obsd79_clock_getres_args_t const *args,
    obsd79_clock_getres_result_t     *result)
{
	LCLog(g_bsd_log, LCLogDebug, 0, "invoked", 0);

	/* Insert here implementation code */

	return (ENOSYS);
}

int
obsd79_nanosleep(
    nix_us_syscall_if_t           *xus,
    nix_monitor_t                 *xmon,
    obsd79_nanosleep_args_t const *args,
    obsd79_nanosleep_result_t     *result)
{
	nix_guest_info_t     gi;
	struct nix_timespec  rqt;
	struct nix_timespec  rmt;
	struct nix_timespec *rmtp = args->arg1 != NULL ? &rmt : NULL;
	nix_env_t           *env = obsd79_us_syscall_get_nix_env(xus);

	LCLog(g_bsd_log, LCLogDebug, 0, "invoked", 0);

	if (args->arg0 == NULL)
		return (EFAULT);

	nix_monitor_get_guest_info(xmon, &gi);

	nix_env_set_errno(env, 0);
	__nix_try
	{
		obsd79_timespec_to_nix_timespec(gi.endian, args->arg0, &rqt);

		*result = nix_nanosleep(&rqt, rmtp, env);

		if (rmtp != NULL)
			nix_timespec_to_obsd79_timespec(gi.endian, rmtp, args->arg1);
	}
	__nix_catch_any
	{
		nix_env_set_errno(env, EFAULT);
	}
	__nix_end_try

	    return (nix_env_get_errno(env));
}

int
obsd79_minherit(
    nix_us_syscall_if_t          *xus,
    nix_monitor_t                *xmon,
    obsd79_minherit_args_t const *args,
    obsd79_minherit_result_t     *result)
{
	nix_env_t *env = obsd79_us_syscall_get_nix_env(xus);

	LCLog(g_bsd_log, LCLogDebug, 0, "invoked", 0);

	nix_env_set_errno(env, 0);
	*result = nix_minherit((uintmax_t)(uintptr_t)args->arg0, args->arg1, args->arg2, env);
	return (nix_env_get_errno(env));
}

int
obsd79_poll(
    nix_us_syscall_if_t      *xus,
    nix_monitor_t            *xmon,
    obsd79_poll_args_t const *args,
    obsd79_poll_result_t     *result)
{
	nix_guest_info_t      gi;
	struct obsd79_pollfd *ofds = args->arg0;
	struct nix_pollfd    *fds = NULL;
	nix_env_t            *env = obsd79_us_syscall_get_nix_env(xus);

	LCLog(g_bsd_log, LCLogDebug, 0, "invoked", 0);

	nix_monitor_get_guest_info(xmon, &gi);

	nix_env_set_errno(env, 0);
	fds = __obsd79_pollfd_copy_from(env, &gi, ofds, args->arg1);
	if (fds != NULL) {
		*result = nix_poll(fds, args->arg1, args->arg2, env);

		if (nix_env_get_errno(env) == 0) {
			/* Copy back the results */
			__nix_try
			{
				size_t n;

				for (n = 0; n < args->arg1; n++)
					nix_pollfd_to_obsd79_pollfd(gi.endian, fds + n, ofds + n);
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
obsd79_issetugid(
    nix_us_syscall_if_t           *xus,
    nix_monitor_t                 *xmon,
    obsd79_issetugid_args_t const *args,
    obsd79_issetugid_result_t     *result)
{
	nix_env_t *env = obsd79_us_syscall_get_nix_env(xus);

	LCLog(g_bsd_log, LCLogDebug, 0, "invoked", 0);

	nix_env_set_errno(env, 0);
	*result = nix_bsd_issetugid(env);
	return (nix_env_get_errno(env));
}

int
obsd79_lchown(
    nix_us_syscall_if_t        *xus,
    nix_monitor_t              *xmon,
    obsd79_lchown_args_t const *args,
    obsd79_lchown_result_t     *result)
{
	nix_env_t *env = obsd79_us_syscall_get_nix_env(xus);

	LCLog(g_bsd_log, LCLogDebug, 0, "invoked", 0);

	nix_env_set_errno(env, 0);
	*result = nix_lchown((char const *)args->arg0, args->arg1, args->arg2, env);
	return (nix_env_get_errno(env));
}

int
obsd79_getsid(
    nix_us_syscall_if_t        *xus,
    nix_monitor_t              *xmon,
    obsd79_getsid_args_t const *args,
    obsd79_getsid_result_t     *result)
{
	nix_env_t *env = obsd79_us_syscall_get_nix_env(xus);

	LCLog(g_bsd_log, LCLogDebug, 0, "invoked", 0);

	nix_env_set_errno(env, 0);
	*result = nix_getsid(args->arg0, env);
	return (nix_env_get_errno(env));
}

int
obsd79_msync(
    nix_us_syscall_if_t       *xus,
    nix_monitor_t             *xmon,
    obsd79_msync_args_t const *args,
    obsd79_msync_result_t     *result)
{
	nix_env_t *env = obsd79_us_syscall_get_nix_env(xus);

	LCLog(g_bsd_log, LCLogDebug, 0, "invoked", 0);

	nix_env_set_errno(env, 0);
	*result = nix_msync((uintmax_t)(uintptr_t)args->arg0, args->arg1, args->arg2, env);
	return (nix_env_get_errno(env));
}

int
obsd79_getfsstat(
    nix_us_syscall_if_t           *xus,
    nix_monitor_t                 *xmon,
    obsd79_getfsstat_args_t const *args,
    obsd79_getfsstat_result_t     *result)
{
	nix_guest_info_t      gi;
	int                   flags;
	size_t                maxcount;
	struct nix_statfs    *fss;
	struct obsd79_statfs *ofss;
	nix_env_t            *env = obsd79_us_syscall_get_nix_env(xus);

	LCLog(g_bsd_log, LCLogDebug, 0, "invoked", 0);

	nix_monitor_get_guest_info(xmon, &gi);

	nix_env_set_errno(env, 0);

	flags = 0;
	if (args->arg2 == OBSD79_MNT_NOWAIT)
		flags = NIX_MNT_NOWAIT;

	if (args->arg0 == NULL) {
		*result = nix_bsd_getfsstat(NULL, 0, flags, env);
		return (nix_env_get_errno(env));
	}

	maxcount = args->arg1 / sizeof(struct obsd79_statfs);
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

		ofss = (struct obsd79_statfs *)args->arg0;
		for (n = 0; n < *result; n++)
			nix_statfs_to_obsd79_statfs(gi.endian, fss + n, ofss + n);

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
obsd79_statfs(
    nix_us_syscall_if_t        *xus,
    nix_monitor_t              *xmon,
    obsd79_statfs_args_t const *args,
    obsd79_statfs_result_t     *result)
{
	nix_guest_info_t  gi;
	struct nix_statfs fs;
	nix_env_t        *env = obsd79_us_syscall_get_nix_env(xus);

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
		nix_statfs_to_obsd79_statfs(gi.endian, &fs, args->arg1);
	}
	__nix_catch_any
	{
		return (EFAULT);
	}
	__nix_end_try

	    return (nix_env_get_errno(env));
}

int
obsd79_fstatfs(
    nix_us_syscall_if_t         *xus,
    nix_monitor_t               *xmon,
    obsd79_fstatfs_args_t const *args,
    obsd79_fstatfs_result_t     *result)
{
	nix_guest_info_t  gi;
	struct nix_statfs fs;
	nix_env_t        *env = obsd79_us_syscall_get_nix_env(xus);

	LCLog(g_bsd_log, LCLogDebug, 0, "invoked", 0);

	nix_monitor_get_guest_info(xmon, &gi);

	nix_env_set_errno(env, 0);
	*result = nix_bsd_fstatfs(args->arg0, &fs, env);
	if (*result)
		return (nix_env_get_errno(env));

	__nix_try
	{
		nix_statfs_to_obsd79_statfs(gi.endian, &fs, args->arg1);
	}
	__nix_catch_any
	{
		return (EFAULT);
	}
	__nix_end_try

	    return (nix_env_get_errno(env));
}

int
obsd79_pipe(
    nix_us_syscall_if_t      *xus,
    nix_monitor_t            *xmon,
    obsd79_pipe_args_t const *args,
    obsd79_pipe_result_t     *result)
{
	int        fds[2];
	int32_t   *ofds;
	nix_env_t *env = obsd79_us_syscall_get_nix_env(xus);

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
obsd79_fhopen(
    nix_us_syscall_if_t        *xus,
    nix_monitor_t              *xmon,
    obsd79_fhopen_args_t const *args,
    obsd79_fhopen_result_t     *result)
{
	LCLog(g_bsd_log, LCLogDebug, 0, "invoked", 0);

	/* Insert here implementation code */

	return (ENOSYS);
}

int
obsd79_fhstatfs(
    nix_us_syscall_if_t          *xus,
    nix_monitor_t                *xmon,
    obsd79_fhstatfs_args_t const *args,
    obsd79_fhstatfs_result_t     *result)
{
	LCLog(g_bsd_log, LCLogDebug, 0, "invoked", 0);

	/* Insert here implementation code */

	return (ENOSYS);
}

int
obsd79_preadv(
    nix_us_syscall_if_t        *xus,
    nix_monitor_t              *xmon,
    obsd79_preadv_args_t const *args,
    obsd79_preadv_result_t     *result)
{
	LCLog(g_bsd_log, LCLogDebug, 0, "invoked", 0);

	/* Insert here implementation code */

	return (ENOSYS);
}

int
obsd79_pwritev(
    nix_us_syscall_if_t         *xus,
    nix_monitor_t               *xmon,
    obsd79_pwritev_args_t const *args,
    obsd79_pwritev_result_t     *result)
{
	LCLog(g_bsd_log, LCLogDebug, 0, "invoked", 0);

	/* Insert here implementation code */

	return (ENOSYS);
}

int
obsd79_kqueue(
    nix_us_syscall_if_t        *xus,
    nix_monitor_t              *xmon,
    obsd79_kqueue_args_t const *args,
    obsd79_kqueue_result_t     *result)
{
	nix_env_t *env = obsd79_us_syscall_get_nix_env(xus);

	LCLog(g_bsd_log, LCLogDebug, 0, "invoked", 0);

	nix_env_set_errno(env, 0);
	*result = nix_bsd_kqueue(env);
	return (nix_env_get_errno(env));
}

int
obsd79_kevent(
    nix_us_syscall_if_t        *xus,
    nix_monitor_t              *xmon,
    obsd79_kevent_args_t const *args,
    obsd79_kevent_result_t     *result)
{
	LCLog(g_bsd_log, LCLogDebug, 0, "invoked", 0);

	/* Insert here implementation code */

	return (ENOSYS);
}

int
obsd79_mlockall(
    nix_us_syscall_if_t          *xus,
    nix_monitor_t                *xmon,
    obsd79_mlockall_args_t const *args,
    obsd79_mlockall_result_t     *result)
{
	nix_env_t *env = obsd79_us_syscall_get_nix_env(xus);

	LCLog(g_bsd_log, LCLogDebug, 0, "invoked", 0);

	nix_env_set_errno(env, 0);
	*result = nix_mlockall(args->arg0, env);
	return (nix_env_get_errno(env));
}

int
obsd79_munlockall(
    nix_us_syscall_if_t            *xus,
    nix_monitor_t                  *xmon,
    obsd79_munlockall_args_t const *args,
    obsd79_munlockall_result_t     *result)
{
	nix_env_t *env = obsd79_us_syscall_get_nix_env(xus);

	LCLog(g_bsd_log, LCLogDebug, 0, "invoked", 0);

	nix_env_set_errno(env, 0);
	*result = nix_munlockall(env);
	return (nix_env_get_errno(env));
}

int
obsd79_getresuid(
    nix_us_syscall_if_t           *xus,
    nix_monitor_t                 *xmon,
    obsd79_getresuid_args_t const *args,
    obsd79_getresuid_result_t     *result)
{
	LCLog(g_bsd_log, LCLogDebug, 0, "invoked", 0);

	/* Insert here implementation code */

	return (ENOSYS);
}

int
obsd79_setresuid(
    nix_us_syscall_if_t           *xus,
    nix_monitor_t                 *xmon,
    obsd79_setresuid_args_t const *args,
    obsd79_setresuid_result_t     *result)
{
	nix_env_t *env = obsd79_us_syscall_get_nix_env(xus);

	LCLog(g_bsd_log, LCLogDebug, 0, "invoked", 0);

	nix_env_set_errno(env, 0);
	*result = nix_hpux_setresuid(args->arg0, args->arg1, args->arg2, env);
	return (nix_env_get_errno(env));
}

int
obsd79_getresgid(
    nix_us_syscall_if_t           *xus,
    nix_monitor_t                 *xmon,
    obsd79_getresgid_args_t const *args,
    obsd79_getresgid_result_t     *result)
{
	LCLog(g_bsd_log, LCLogDebug, 0, "invoked", 0);

	/* Insert here implementation code */

	return (ENOSYS);
}

int
obsd79_setresgid(
    nix_us_syscall_if_t           *xus,
    nix_monitor_t                 *xmon,
    obsd79_setresgid_args_t const *args,
    obsd79_setresgid_result_t     *result)
{
	nix_env_t *env = obsd79_us_syscall_get_nix_env(xus);

	LCLog(g_bsd_log, LCLogDebug, 0, "invoked", 0);

	nix_env_set_errno(env, 0);
	*result = nix_hpux_setresgid(args->arg0, args->arg1, args->arg2, env);
	return (nix_env_get_errno(env));
}

int
obsd79_mquery(
    nix_us_syscall_if_t        *xus,
    nix_monitor_t              *xmon,
    obsd79_mquery_args_t const *args,
    obsd79_mquery_result_t     *result)
{
	nix_env_t *env = obsd79_us_syscall_get_nix_env(xus);

	LCLog(g_bsd_log, LCLogDebug, 0, "invoked", 0);

	nix_env_set_errno(env, 0);
	*result = (obsd79_mquery_result_t)(uintptr_t)nix_bsd_mquery((uintmax_t)(uintptr_t)args->arg0, args->arg1, args->arg2, args->arg3, args->arg4, args->arg5, env);
	return (nix_env_get_errno(env));
}

int
obsd79_closefrom(
    nix_us_syscall_if_t           *xus,
    nix_monitor_t                 *xmon,
    obsd79_closefrom_args_t const *args,
    obsd79_closefrom_result_t     *result)
{
	nix_env_t *env = obsd79_us_syscall_get_nix_env(xus);

	LCLog(g_bsd_log, LCLogDebug, 0, "invoked", 0);

	nix_env_set_errno(env, 0);
	*result = nix_bsd_closefrom(args->arg0, env);
	return (nix_env_get_errno(env));
}

int
obsd79_sigaltstack(
    nix_us_syscall_if_t             *xus,
    nix_monitor_t                   *xmon,
    obsd79_sigaltstack_args_t const *args,
    obsd79_sigaltstack_result_t     *result)
{
	LCLog(g_bsd_log, LCLogDebug, 0, "invoked", 0);

	/* Insert here implementation code */

	return (ENOSYS);
}

int
obsd79_shmget(
    nix_us_syscall_if_t        *xus,
    nix_monitor_t              *xmon,
    obsd79_shmget_args_t const *args,
    obsd79_shmget_result_t     *result)
{
	LCLog(g_bsd_log, LCLogDebug, 0, "invoked", 0);

	/* Insert here implementation code */

	return (ENOSYS);
}

int
obsd79_semop(
    nix_us_syscall_if_t       *xus,
    nix_monitor_t             *xmon,
    obsd79_semop_args_t const *args,
    obsd79_semop_result_t     *result)
{
	LCLog(g_bsd_log, LCLogDebug, 0, "invoked", 0);

	/* Insert here implementation code */

	return (ENOSYS);
}

int
obsd79_stat(
    nix_us_syscall_if_t      *xus,
    nix_monitor_t            *xmon,
    obsd79_stat_args_t const *args,
    obsd79_stat_result_t     *result)
{
	nix_guest_info_t gi;
	struct nix_stat  st;
	nix_env_t       *env = obsd79_us_syscall_get_nix_env(xus);

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
		nix_stat_to_obsd79_stat(gi.endian, &st, args->arg1);
	}
	__nix_catch_any
	{
		return (EFAULT);
	}
	__nix_end_try

	    return (0);
}

int
obsd79_fstat(
    nix_us_syscall_if_t       *xus,
    nix_monitor_t             *xmon,
    obsd79_fstat_args_t const *args,
    obsd79_fstat_result_t     *result)
{
	nix_guest_info_t gi;
	struct nix_stat  st;
	nix_env_t       *env = obsd79_us_syscall_get_nix_env(xus);

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
		nix_stat_to_obsd79_stat(gi.endian, &st, args->arg1);
	}
	__nix_catch_any
	{
		return (EFAULT);
	}
	__nix_end_try

	    return (0);
}

int
obsd79_lstat(
    nix_us_syscall_if_t       *xus,
    nix_monitor_t             *xmon,
    obsd79_lstat_args_t const *args,
    obsd79_lstat_result_t     *result)
{
	nix_guest_info_t gi;
	struct nix_stat  st;
	nix_env_t       *env = obsd79_us_syscall_get_nix_env(xus);

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
		nix_stat_to_obsd79_stat(gi.endian, &st, args->arg1);
	}
	__nix_catch_any
	{
		return (EFAULT);
	}
	__nix_end_try

	    return (0);
}

int
obsd79_fhstat(
    nix_us_syscall_if_t        *xus,
    nix_monitor_t              *xmon,
    obsd79_fhstat_args_t const *args,
    obsd79_fhstat_result_t     *result)
{
	LCLog(g_bsd_log, LCLogDebug, 0, "invoked", 0);

	/* Insert here implementation code */

	return (ENOSYS);
}

int
obsd79___semctl(
    nix_us_syscall_if_t          *xus,
    nix_monitor_t                *xmon,
    obsd79___semctl_args_t const *args,
    obsd79___semctl_result_t     *result)
{
	LCLog(g_bsd_log, LCLogDebug, 0, "invoked", 0);

	/* Insert here implementation code */

	return (ENOSYS);
}

int
obsd79_shmctl(
    nix_us_syscall_if_t        *xus,
    nix_monitor_t              *xmon,
    obsd79_shmctl_args_t const *args,
    obsd79_shmctl_result_t     *result)
{
	LCLog(g_bsd_log, LCLogDebug, 0, "invoked", 0);

	/* Insert here implementation code */

	return (ENOSYS);
}

int
obsd79_msgctl(
    nix_us_syscall_if_t        *xus,
    nix_monitor_t              *xmon,
    obsd79_msgctl_args_t const *args,
    obsd79_msgctl_result_t     *result)
{
	LCLog(g_bsd_log, LCLogDebug, 0, "invoked", 0);

	/* Insert here implementation code */

	return (ENOSYS);
}

int
obsd79_sched_yield(
    nix_us_syscall_if_t             *xus,
    nix_monitor_t                   *xmon,
    obsd79_sched_yield_args_t const *args,
    obsd79_sched_yield_result_t     *result)
{
	nix_env_t *env = obsd79_us_syscall_get_nix_env(xus);

	LCLog(g_bsd_log, LCLogDebug, 0, "invoked", 0);

	nix_env_set_errno(env, 0);
	*result = nix_rt_sched_yield(env);
	return (nix_env_get_errno(env));
}

int
obsd79_getthrid(
    nix_us_syscall_if_t          *xus,
    nix_monitor_t                *xmon,
    obsd79_getthrid_args_t const *args,
    obsd79_getthrid_result_t     *result)
{
	LCLog(g_bsd_log, LCLogDebug, 0, "invoked", 0);

	/* Insert here implementation code */

	return (ENOSYS);
}

int
obsd79___getcwd(
    nix_us_syscall_if_t          *xus,
    nix_monitor_t                *xmon,
    obsd79___getcwd_args_t const *args,
    obsd79___getcwd_result_t     *result)
{
	nix_env_t *env = obsd79_us_syscall_get_nix_env(xus);

	LCLog(g_bsd_log, LCLogDebug, 0, "invoked", 0);

	nix_env_set_errno(env, 0);
	*result = nix_getcwd((char *)args->arg0, args->arg1, env);
	return (nix_env_get_errno(env));
}

int
obsd79_adjfreq(
    nix_us_syscall_if_t         *xus,
    nix_monitor_t               *xmon,
    obsd79_adjfreq_args_t const *args,
    obsd79_adjfreq_result_t     *result)
{
	LCLog(g_bsd_log, LCLogDebug, 0, "invoked", 0);

	/* Insert here implementation code */

	return (ENOSYS);
}

int
obsd79_obreak(
    nix_us_syscall_if_t        *xus,
    nix_monitor_t              *xmon,
    obsd79_obreak_args_t const *args,
    obsd79_obreak_result_t     *result)
{
	nix_env_t *env = obsd79_us_syscall_get_nix_env(xus);

	LCLog(g_bsd_log, LCLogDebug, 0, "invoked", 0);

	nix_env_set_errno(env, 0);
	nix_brk((uintmax_t)(uintptr_t)args->arg0, env);
	*result = 0;
	return (nix_env_get_errno(env));
}
int
obsd79_unmount(
    nix_us_syscall_if_t         *xus,
    nix_monitor_t               *xmon,
    obsd79_unmount_args_t const *args,
    obsd79_unmount_result_t     *result)
{
	nix_env_t *env = obsd79_us_syscall_get_nix_env(xus);

	LCLog(g_bsd_log, LCLogDebug, 0, "invoked", 0);

	nix_env_set_errno(env, 0);
	*result = nix_bsd_unmount((char const *)args->arg0, args->arg1, env);
	return (nix_env_get_errno(env));
}
int
obsd79_sysctl(
    nix_us_syscall_if_t        *xus,
    nix_monitor_t              *xmon,
    obsd79_sysctl_args_t const *args,
    obsd79_sysctl_result_t     *result)
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
	case OBSD79_CTL_KERN:
		switch (GE32(&gi, mib[1])) {
		case OBSD79_KERN_OSTYPE:
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

		case OBSD79_KERN_OSRELEASE:
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

		case OBSD79_KERN_ARGMAX:
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

		case OBSD79_KERN_OSVERSION:
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

		case OBSD79_KERN_HOSTNAME: {
			char       hostname[512];
			nix_env_t *env = obsd79_us_syscall_get_nix_env(xus);

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

		case OBSD79_KERN_DOMAINNAME: {
			char       domainname[512];
			nix_env_t *env = obsd79_us_syscall_get_nix_env(xus);

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

		case OBSD79_KERN_ARND: {
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

	case OBSD79_CTL_HW:
		switch (GE32(&gi, mib[1])) {
		case OBSD79_HW_MACHINE:
			LCAssert(g_bsd_log, miblen == 2);
			__nix_try
			{
				__str_copy(&gi, (char *)oldp, oldlenp, OBSD79_MACHINE_NAME);
			}
			__nix_catch_any
			{
				*result = -1;
				return (EFAULT);
			}
			__nix_end_try return (0);

		case OBSD79_HW_NCPUS:
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

		case OBSD79_HW_PAGESIZE:
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
obsd79___thrsleep(
    nix_us_syscall_if_t            *xus,
    nix_monitor_t                  *xmon,
    obsd79___thrsleep_args_t const *args,
    obsd79___thrsleep_result_t     *result)
{
	LCLog(g_bsd_log, LCLogDebug, 0, "invoked", 0);

	/* Insert here implementation code */

	return (ENOSYS);
}
int
obsd79___thrwakeup(
    nix_us_syscall_if_t             *xus,
    nix_monitor_t                   *xmon,
    obsd79___thrwakeup_args_t const *args,
    obsd79___thrwakeup_result_t     *result)
{
	LCLog(g_bsd_log, LCLogDebug, 0, "invoked", 0);

	/* Insert here implementation code */

	return (ENOSYS);
}
int
obsd79___threxit(
    nix_us_syscall_if_t           *xus,
    nix_monitor_t                 *xmon,
    obsd79___threxit_args_t const *args,
    obsd79___threxit_result_t     *result)
{
	LCLog(g_bsd_log, LCLogDebug, 0, "invoked", 0);

	/* Insert here implementation code */

	return (ENOSYS);
}
int
obsd79___thrsigdivert(
    nix_us_syscall_if_t                *xus,
    nix_monitor_t                      *xmon,
    obsd79___thrsigdivert_args_t const *args,
    obsd79___thrsigdivert_result_t     *result)
{
	LCLog(g_bsd_log, LCLogDebug, 0, "invoked", 0);

	/* Insert here implementation code */

	return (ENOSYS);
}

int
obsd79_getentropy(
    nix_us_syscall_if_t            *xus,
    nix_monitor_t                  *xmon,
    obsd79_getentropy_args_t const *args,
    obsd79_getentropy_result_t     *result)
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
obsd79___tfork(
    nix_us_syscall_if_t         *xus,
    nix_monitor_t               *xmon,
    obsd79___tfork_args_t const *args,
    obsd79___tfork_result_t     *result)
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
obsd79_getdtablecount(
    nix_us_syscall_if_t                *xus,
    nix_monitor_t                      *xmon,
    obsd79_getdtablecount_args_t const *args,
    obsd79_getdtablecount_result_t     *result)
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
obsd79_fstatat(
    nix_us_syscall_if_t         *xus,
    nix_monitor_t               *xmon,
    obsd79_fstatat_args_t const *args,
    obsd79_fstatat_result_t     *result)
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
obsd79_futex(
    nix_us_syscall_if_t       *xus,
    nix_monitor_t             *xmon,
    obsd79_futex_args_t const *args,
    obsd79_futex_result_t     *result)
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
obsd79_utimensat(
    nix_us_syscall_if_t           *xus,
    nix_monitor_t                 *xmon,
    obsd79_utimensat_args_t const *args,
    obsd79_utimensat_result_t     *result)
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
obsd79_futimens(
    nix_us_syscall_if_t          *xus,
    nix_monitor_t                *xmon,
    obsd79_futimens_args_t const *args,
    obsd79_futimens_result_t     *result)
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
obsd79_kbind(
    nix_us_syscall_if_t       *xus,
    nix_monitor_t             *xmon,
    obsd79_kbind_args_t const *args,
    obsd79_kbind_result_t     *result)
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
obsd79_accept4(
    nix_us_syscall_if_t         *xus,
    nix_monitor_t               *xmon,
    obsd79_accept4_args_t const *args,
    obsd79_accept4_result_t     *result)
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
obsd79_getdents(
    nix_us_syscall_if_t          *xus,
    nix_monitor_t                *xmon,
    obsd79_getdents_args_t const *args,
    obsd79_getdents_result_t     *result)
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
obsd79_pipe2(
    nix_us_syscall_if_t       *xus,
    nix_monitor_t             *xmon,
    obsd79_pipe2_args_t const *args,
    obsd79_pipe2_result_t     *result)
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
obsd79_dup3(
    nix_us_syscall_if_t      *xus,
    nix_monitor_t            *xmon,
    obsd79_dup3_args_t const *args,
    obsd79_dup3_result_t     *result)
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
obsd79_chflagsat(
    nix_us_syscall_if_t           *xus,
    nix_monitor_t                 *xmon,
    obsd79_chflagsat_args_t const *args,
    obsd79_chflagsat_result_t     *result)
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
obsd79_pledge(
    nix_us_syscall_if_t        *xus,
    nix_monitor_t              *xmon,
    obsd79_pledge_args_t const *args,
    obsd79_pledge_result_t     *result)
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
obsd79_ppoll(
    nix_us_syscall_if_t       *xus,
    nix_monitor_t             *xmon,
    obsd79_ppoll_args_t const *args,
    obsd79_ppoll_result_t     *result)
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
obsd79_pselect(
    nix_us_syscall_if_t         *xus,
    nix_monitor_t               *xmon,
    obsd79_pselect_args_t const *args,
    obsd79_pselect_result_t     *result)
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
obsd79_sendsyslog(
    nix_us_syscall_if_t            *xus,
    nix_monitor_t                  *xmon,
    obsd79_sendsyslog_args_t const *args,
    obsd79_sendsyslog_result_t     *result)
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
obsd79_unveil(
    nix_us_syscall_if_t        *xus,
    nix_monitor_t              *xmon,
    obsd79_unveil_args_t const *args,
    obsd79_unveil_result_t     *result)
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
obsd79___realpath(
    nix_us_syscall_if_t            *xus,
    nix_monitor_t                  *xmon,
    obsd79___realpath_args_t const *args,
    obsd79___realpath_result_t     *result)
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
obsd79_recvmmsg(
    nix_us_syscall_if_t          *xus,
    nix_monitor_t                *xmon,
    obsd79_recvmmsg_args_t const *args,
    obsd79_recvmmsg_result_t     *result)
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
obsd79_sendmmsg(
    nix_us_syscall_if_t          *xus,
    nix_monitor_t                *xmon,
    obsd79_sendmmsg_args_t const *args,
    obsd79_sendmmsg_result_t     *result)
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
obsd79_thrkill(
    nix_us_syscall_if_t         *xus,
    nix_monitor_t               *xmon,
    obsd79_thrkill_args_t const *args,
    obsd79_thrkill_result_t     *result)
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
obsd79___pledge_open(
    nix_us_syscall_if_t               *xus,
    nix_monitor_t                     *xmon,
    obsd79___pledge_open_args_t const *args,
    obsd79___pledge_open_result_t     *result)
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
obsd79_getlogin_r(
    nix_us_syscall_if_t            *xus,
    nix_monitor_t                  *xmon,
    obsd79_getlogin_r_args_t const *args,
    obsd79_getlogin_r_result_t     *result)
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
obsd79_getthrname(
    nix_us_syscall_if_t            *xus,
    nix_monitor_t                  *xmon,
    obsd79_getthrname_args_t const *args,
    obsd79_getthrname_result_t     *result)
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
obsd79_setthrname(
    nix_us_syscall_if_t            *xus,
    nix_monitor_t                  *xmon,
    obsd79_setthrname_args_t const *args,
    obsd79_setthrname_result_t     *result)
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
obsd79_ypconnect(
    nix_us_syscall_if_t           *xus,
    nix_monitor_t                 *xmon,
    obsd79_ypconnect_args_t const *args,
    obsd79_ypconnect_result_t     *result)
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
obsd79_pinsyscalls(
    nix_us_syscall_if_t             *xus,
    nix_monitor_t                   *xmon,
    obsd79_pinsyscalls_args_t const *args,
    obsd79_pinsyscalls_result_t     *result)
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
obsd79_mimmutable(
    nix_us_syscall_if_t            *xus,
    nix_monitor_t                  *xmon,
    obsd79_mimmutable_args_t const *args,
    obsd79_mimmutable_result_t     *result)
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
obsd79_waitid(
    nix_us_syscall_if_t        *xus,
    nix_monitor_t              *xmon,
    obsd79_waitid_args_t const *args,
    obsd79_waitid_result_t     *result)
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
obsd79_pathconfat(
    nix_us_syscall_if_t            *xus,
    nix_monitor_t                  *xmon,
    obsd79_pathconfat_args_t const *args,
    obsd79_pathconfat_result_t     *result)
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
obsd79_utrace(
    nix_us_syscall_if_t        *xus,
    nix_monitor_t              *xmon,
    obsd79_utrace_args_t const *args,
    obsd79_utrace_result_t     *result)
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
obsd79_kqueue1(
    nix_us_syscall_if_t         *xus,
    nix_monitor_t               *xmon,
    obsd79_kqueue1_args_t const *args,
    obsd79_kqueue1_result_t     *result)
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
obsd79_setrtable(
    nix_us_syscall_if_t           *xus,
    nix_monitor_t                 *xmon,
    obsd79_setrtable_args_t const *args,
    obsd79_setrtable_result_t     *result)
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
obsd79_getrtable(
    nix_us_syscall_if_t           *xus,
    nix_monitor_t                 *xmon,
    obsd79_getrtable_args_t const *args,
    obsd79_getrtable_result_t     *result)
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
obsd79_faccessat(
    nix_us_syscall_if_t           *xus,
    nix_monitor_t                 *xmon,
    obsd79_faccessat_args_t const *args,
    obsd79_faccessat_result_t     *result)
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
obsd79_fchmodat(
    nix_us_syscall_if_t          *xus,
    nix_monitor_t                *xmon,
    obsd79_fchmodat_args_t const *args,
    obsd79_fchmodat_result_t     *result)
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
obsd79_fchownat(
    nix_us_syscall_if_t          *xus,
    nix_monitor_t                *xmon,
    obsd79_fchownat_args_t const *args,
    obsd79_fchownat_result_t     *result)
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
obsd79_linkat(
    nix_us_syscall_if_t        *xus,
    nix_monitor_t              *xmon,
    obsd79_linkat_args_t const *args,
    obsd79_linkat_result_t     *result)
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
obsd79_mkdirat(
    nix_us_syscall_if_t         *xus,
    nix_monitor_t               *xmon,
    obsd79_mkdirat_args_t const *args,
    obsd79_mkdirat_result_t     *result)
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
obsd79_mkfifoat(
    nix_us_syscall_if_t          *xus,
    nix_monitor_t                *xmon,
    obsd79_mkfifoat_args_t const *args,
    obsd79_mkfifoat_result_t     *result)
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
obsd79_mknodat(
    nix_us_syscall_if_t         *xus,
    nix_monitor_t               *xmon,
    obsd79_mknodat_args_t const *args,
    obsd79_mknodat_result_t     *result)
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
obsd79_openat(
    nix_us_syscall_if_t        *xus,
    nix_monitor_t              *xmon,
    obsd79_openat_args_t const *args,
    obsd79_openat_result_t     *result)
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
obsd79_readlinkat(
    nix_us_syscall_if_t            *xus,
    nix_monitor_t                  *xmon,
    obsd79_readlinkat_args_t const *args,
    obsd79_readlinkat_result_t     *result)
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
obsd79_renameat(
    nix_us_syscall_if_t          *xus,
    nix_monitor_t                *xmon,
    obsd79_renameat_args_t const *args,
    obsd79_renameat_result_t     *result)
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
obsd79_symlinkat(
    nix_us_syscall_if_t           *xus,
    nix_monitor_t                 *xmon,
    obsd79_symlinkat_args_t const *args,
    obsd79_symlinkat_result_t     *result)
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
obsd79_unlinkat(
    nix_us_syscall_if_t          *xus,
    nix_monitor_t                *xmon,
    obsd79_unlinkat_args_t const *args,
    obsd79_unlinkat_result_t     *result)
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
obsd79___set_tcb(
    nix_us_syscall_if_t           *xus,
    nix_monitor_t                 *xmon,
    obsd79___set_tcb_args_t const *args,
    obsd79___set_tcb_result_t     *result)
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
obsd79___get_tcb(
    nix_us_syscall_if_t           *xus,
    nix_monitor_t                 *xmon,
    obsd79___get_tcb_args_t const *args,
    obsd79___get_tcb_result_t     *result)
{
	(void)xus;
	(void)xmon;
	(void)args;
	(void)result;

	LCLog(g_bsd_log, LCLogWarning, 0,
	      "unimplemented OpenBSD 7.9 syscall `__get_tcb'", 0);

	return (ENOSYS);
}
