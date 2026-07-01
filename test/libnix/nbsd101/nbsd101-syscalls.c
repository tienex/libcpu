/*
 * NetBSD 10.1 System Calls Implementation
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
#include "nbsd101-syscalls.h"
#include "nbsd101-us-syscall-priv.h"

#include "nix.h"
#include "nix-fd.h"
#include "netbsd101.h"
#include "arc4random.h"

#include "nbsd101-args.h"
#include "nbsd101-sysctl.h"
#include "nbsd101-mman.h"

#include "nix-host.h" /* XXX */

#define GE32(gi, x) \
	(((gi)->endian != NIX_ENDIAN_NATIVE) ? nix_byte_swap_int32(x) : (x))

#define GE64(gi, x) \
	(((gi)->endian != NIX_ENDIAN_NATIVE) ? nix_byte_swap_int64(x) : (x))

void *g_bsd_log = NULL;

static __inline struct nix_iovec *
__nbsd101_iovec32_copy_from(nix_env_t              *env,
                           nix_mem_if_t           *mem,
                           nix_guest_info_t const *gi,
                           struct nbsd101_iovec32  *iov,
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
__nbsd101_pollfd_copy_from(nix_env_t              *env,
                          nix_guest_info_t const *gi,
                          struct nbsd101_pollfd   *fds,
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
				nbsd101_pollfd_to_nix_pollfd(gi->endian, fds + n, xfds + n);
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
nbsd101_exit(
    nix_us_syscall_if_t      *xus,
    nix_monitor_t            *xmon,
    nbsd101_exit_args_t const *args,
    nbsd101_exit_result_t     *result)
{
	LCLog(g_bsd_log, LCLogDebug, 0, "invoked", 0);

	nix_exit(args->arg0);
	return (0); /* Not reached (should)! */
}

int
nbsd101_fork(
    nix_us_syscall_if_t      *xus,
    nix_monitor_t            *xmon,
    nbsd101_fork_args_t const *args,
    nbsd101_fork_result_t     *result)
{
	nix_env_t *env = nbsd101_us_syscall_get_nix_env(xus);

	LCLog(g_bsd_log, LCLogDebug, 0, "invoked", 0);

	nix_env_set_errno(env, 0);
	*result = nix_fork(env);
	return (nix_env_get_errno(env));
}

int
nbsd101_read(
    nix_us_syscall_if_t      *xus,
    nix_monitor_t            *xmon,
    nbsd101_read_args_t const *args,
    nbsd101_read_result_t     *result)
{
	nix_env_t *env = nbsd101_us_syscall_get_nix_env(xus);

	LCLog(g_bsd_log, LCLogDebug, 0, "invoked", 0);

	nix_env_set_errno(env, 0);
	*result = nix_read(args->arg0, args->arg1, args->arg2, env);
	return (nix_env_get_errno(env));
}

int
nbsd101_write(
    nix_us_syscall_if_t       *xus,
    nix_monitor_t             *xmon,
    nbsd101_write_args_t const *args,
    nbsd101_write_result_t     *result)
{
	nix_env_t *env = nbsd101_us_syscall_get_nix_env(xus);

	LCLog(g_bsd_log, LCLogDebug, 0, "invoked", 0);

	nix_env_set_errno(env, 0);
	*result = nix_write(args->arg0, args->arg1, args->arg2, env);
	return (nix_env_get_errno(env));
}

int
nbsd101_open(
    nix_us_syscall_if_t      *xus,
    nix_monitor_t            *xmon,
    nbsd101_open_args_t const *args,
    nbsd101_open_result_t     *result)
{
	nix_env_t *env = nbsd101_us_syscall_get_nix_env(xus);

	LCLog(g_bsd_log, LCLogDebug, 0, "invoked", 0);

	nix_env_set_errno(env, 0);
	*result = nix_open((char const *)args->arg0, args->arg1, args->arg2, env);
	return (nix_env_get_errno(env));
}

int
nbsd101_close(
    nix_us_syscall_if_t       *xus,
    nix_monitor_t             *xmon,
    nbsd101_close_args_t const *args,
    nbsd101_close_result_t     *result)
{
	nix_env_t *env = nbsd101_us_syscall_get_nix_env(xus);

	LCLog(g_bsd_log, LCLogDebug, 0, "invoked", 0);

	nix_env_set_errno(env, 0);
	*result = nix_close(args->arg0, env);
	return (nix_env_get_errno(env));
}

int
nbsd101_link(
    nix_us_syscall_if_t      *xus,
    nix_monitor_t            *xmon,
    nbsd101_link_args_t const *args,
    nbsd101_link_result_t     *result)
{
	nix_env_t *env = nbsd101_us_syscall_get_nix_env(xus);

	LCLog(g_bsd_log, LCLogDebug, 0, "invoked", 0);

	nix_env_set_errno(env, 0);
	*result = nix_link((char const *)args->arg0, (char const *)args->arg1, env);
	return (nix_env_get_errno(env));
}

int
nbsd101_unlink(
    nix_us_syscall_if_t        *xus,
    nix_monitor_t              *xmon,
    nbsd101_unlink_args_t const *args,
    nbsd101_unlink_result_t     *result)
{
	nix_env_t *env = nbsd101_us_syscall_get_nix_env(xus);

	LCLog(g_bsd_log, LCLogDebug, 0, "invoked", 0);

	nix_env_set_errno(env, 0);
	*result = nix_unlink((char const *)args->arg0, env);
	return (nix_env_get_errno(env));
}

int
nbsd101_chdir(
    nix_us_syscall_if_t       *xus,
    nix_monitor_t             *xmon,
    nbsd101_chdir_args_t const *args,
    nbsd101_chdir_result_t     *result)
{
	nix_env_t *env = nbsd101_us_syscall_get_nix_env(xus);

	LCLog(g_bsd_log, LCLogDebug, 0, "invoked", 0);

	nix_env_set_errno(env, 0);
	*result = nix_chdir((char const *)args->arg0, env);
	return (nix_env_get_errno(env));
}

int
nbsd101_fchdir(
    nix_us_syscall_if_t        *xus,
    nix_monitor_t              *xmon,
    nbsd101_fchdir_args_t const *args,
    nbsd101_fchdir_result_t     *result)
{
	nix_env_t *env = nbsd101_us_syscall_get_nix_env(xus);

	LCLog(g_bsd_log, LCLogDebug, 0, "invoked", 0);

	nix_env_set_errno(env, 0);
	*result = nix_fchdir(args->arg0, env);
	return (nix_env_get_errno(env));
}

int
nbsd101_chmod(
    nix_us_syscall_if_t       *xus,
    nix_monitor_t             *xmon,
    nbsd101_chmod_args_t const *args,
    nbsd101_chmod_result_t     *result)
{
	nix_env_t *env = nbsd101_us_syscall_get_nix_env(xus);

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
nbsd101_chown(
    nix_us_syscall_if_t       *xus,
    nix_monitor_t             *xmon,
    nbsd101_chown_args_t const *args,
    nbsd101_chown_result_t     *result)
{
	nix_env_t *env = nbsd101_us_syscall_get_nix_env(xus);

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
nbsd101_setuid(
    nix_us_syscall_if_t        *xus,
    nix_monitor_t              *xmon,
    nbsd101_setuid_args_t const *args,
    nbsd101_setuid_result_t     *result)
{
	nix_env_t *env = nbsd101_us_syscall_get_nix_env(xus);

	LCLog(g_bsd_log, LCLogDebug, 0, "invoked", 0);

	nix_env_set_errno(env, 0);
	*result = nix_setuid(args->arg0, env);
	return (nix_env_get_errno(env));
}

int
nbsd101_ptrace(
    nix_us_syscall_if_t        *xus,
    nix_monitor_t              *xmon,
    nbsd101_ptrace_args_t const *args,
    nbsd101_ptrace_result_t     *result)
{
	nix_env_t *env = nbsd101_us_syscall_get_nix_env(xus);

	LCLog(g_bsd_log, LCLogDebug, 0, "invoked", 0);

	nix_env_set_errno(env, 0);
	*result = nix_ptrace(args->arg0, args->arg1, args->arg2, args->arg3, env);
	return (nix_env_get_errno(env));
}

int
nbsd101_recvmsg(
    nix_us_syscall_if_t         *xus,
    nix_monitor_t               *xmon,
    nbsd101_recvmsg_args_t const *args,
    nbsd101_recvmsg_result_t     *result)
{
	LCLog(g_bsd_log, LCLogDebug, 0, "invoked", 0);

	/* Insert here implementation code */

	return (ENOSYS);
}

int
nbsd101_sendmsg(
    nix_us_syscall_if_t         *xus,
    nix_monitor_t               *xmon,
    nbsd101_sendmsg_args_t const *args,
    nbsd101_sendmsg_result_t     *result)
{
	LCLog(g_bsd_log, LCLogDebug, 0, "invoked", 0);

	/* Insert here implementation code */

	return (ENOSYS);
}

int
nbsd101_recvfrom(
    nix_us_syscall_if_t          *xus,
    nix_monitor_t                *xmon,
    nbsd101_recvfrom_args_t const *args,
    nbsd101_recvfrom_result_t     *result)
{
	nix_guest_info_t     gi;
	struct nix_sockaddr  sa;
	int                  rc = -1;
	nix_socklen_t        salen = sizeof(sa);
	struct nix_sockaddr *psa = args->arg4 != NULL ? &sa : NULL;
	nix_socklen_t       *psalen = args->arg4 != NULL ? &salen : NULL;
	nix_env_t           *env = nbsd101_us_syscall_get_nix_env(xus);

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
			if (!nix_sockaddr_to_nbsd101_sockaddr(gi.endian, psa, *psalen,
			                                     args->arg4, (nbsd101_socklen_t *)args->arg5)) {
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
nbsd101_accept(
    nix_us_syscall_if_t        *xus,
    nix_monitor_t              *xmon,
    nbsd101_accept_args_t const *args,
    nbsd101_accept_result_t     *result)
{
	LCLog(g_bsd_log, LCLogDebug, 0, "invoked", 0);

	/* Insert here implementation code */

	return (ENOSYS);
}

int
nbsd101_getpeername(
    nix_us_syscall_if_t             *xus,
    nix_monitor_t                   *xmon,
    nbsd101_getpeername_args_t const *args,
    nbsd101_getpeername_result_t     *result)
{
	nix_guest_info_t    gi;
	struct nix_sockaddr sa;
	nix_socklen_t       salen = sizeof(sa);
	nix_env_t          *env = nbsd101_us_syscall_get_nix_env(xus);

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
		if (!nix_sockaddr_to_nbsd101_sockaddr(gi.endian, &sa, salen,
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
nbsd101_getsockname(
    nix_us_syscall_if_t             *xus,
    nix_monitor_t                   *xmon,
    nbsd101_getsockname_args_t const *args,
    nbsd101_getsockname_result_t     *result)
{
	nix_guest_info_t    gi;
	struct nix_sockaddr sa;
	nix_socklen_t       salen = sizeof(sa);
	nix_env_t          *env = nbsd101_us_syscall_get_nix_env(xus);

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
		if (!nix_sockaddr_to_nbsd101_sockaddr(gi.endian, &sa, salen, args->arg1, args->arg2)) {
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
nbsd101_access(
    nix_us_syscall_if_t        *xus,
    nix_monitor_t              *xmon,
    nbsd101_access_args_t const *args,
    nbsd101_access_result_t     *result)
{
	nix_env_t *env = nbsd101_us_syscall_get_nix_env(xus);

	LCLog(g_bsd_log, LCLogDebug, 0, "invoked", 0);

	nix_env_set_errno(env, 0);
	*result = nix_access((char const *)args->arg0, args->arg1, env);
	return (nix_env_get_errno(env));
}

int
nbsd101_chflags(
    nix_us_syscall_if_t         *xus,
    nix_monitor_t               *xmon,
    nbsd101_chflags_args_t const *args,
    nbsd101_chflags_result_t     *result)
{
	nix_env_t *env = nbsd101_us_syscall_get_nix_env(xus);

	LCLog(g_bsd_log, LCLogDebug, 0, "invoked", 0);

	nix_env_set_errno(env, 0);
	*result = nix_bsd_chflags((char const *)args->arg0, args->arg1, env);
	return (nix_env_get_errno(env));
}

int
nbsd101_fchflags(
    nix_us_syscall_if_t          *xus,
    nix_monitor_t                *xmon,
    nbsd101_fchflags_args_t const *args,
    nbsd101_fchflags_result_t     *result)
{
	nix_env_t *env = nbsd101_us_syscall_get_nix_env(xus);

	LCLog(g_bsd_log, LCLogDebug, 0, "invoked", 0);

	nix_env_set_errno(env, 0);
	*result = nix_bsd_fchflags(args->arg0, args->arg1, env);
	return (nix_env_get_errno(env));
}

int
nbsd101_kill(
    nix_us_syscall_if_t      *xus,
    nix_monitor_t            *xmon,
    nbsd101_kill_args_t const *args,
    nbsd101_kill_result_t     *result)
{
	nix_env_t *env = nbsd101_us_syscall_get_nix_env(xus);

	LCLog(g_bsd_log, LCLogDebug, 0, "invoked", 0);

	nix_env_set_errno(env, 0);
	*result = nix_kill(args->arg0, args->arg1, env);
	return (nix_env_get_errno(env));
}

int
nbsd101_dup(
    nix_us_syscall_if_t     *xus,
    nix_monitor_t           *xmon,
    nbsd101_dup_args_t const *args,
    nbsd101_dup_result_t     *result)
{
	nix_env_t *env = nbsd101_us_syscall_get_nix_env(xus);

	LCLog(g_bsd_log, LCLogDebug, 0, "invoked", 0);

	nix_env_set_errno(env, 0);
	*result = nix_dup(args->arg0, env);
	return (nix_env_get_errno(env));
}

int
nbsd101_profil(
    nix_us_syscall_if_t        *xus,
    nix_monitor_t              *xmon,
    nbsd101_profil_args_t const *args,
    nbsd101_profil_result_t     *result)
{
	nix_env_t *env = nbsd101_us_syscall_get_nix_env(xus);

	LCLog(g_bsd_log, LCLogDebug, 0, "invoked", 0);

	nix_env_set_errno(env, 0);
	*result = nix_bsd_profil(args->arg0, args->arg1, args->arg2, args->arg3, env);
	return (nix_env_get_errno(env));
}

int
nbsd101_ktrace(
    nix_us_syscall_if_t        *xus,
    nix_monitor_t              *xmon,
    nbsd101_ktrace_args_t const *args,
    nbsd101_ktrace_result_t     *result)
{
	nix_env_t *env = nbsd101_us_syscall_get_nix_env(xus);

	LCLog(g_bsd_log, LCLogDebug, 0, "invoked", 0);

	nix_env_set_errno(env, 0);
	*result = nix_bsd_ktrace(args->arg0, args->arg1, args->arg2, args->arg3, env);
	return (nix_env_get_errno(env));
}

int
nbsd101_acct(
    nix_us_syscall_if_t      *xus,
    nix_monitor_t            *xmon,
    nbsd101_acct_args_t const *args,
    nbsd101_acct_result_t     *result)
{
	nix_env_t *env = nbsd101_us_syscall_get_nix_env(xus);

	LCLog(g_bsd_log, LCLogDebug, 0, "invoked", 0);

	nix_env_set_errno(env, 0);
	*result = nix_bsd_acct((char const *)args->arg0, env);
	return (nix_env_get_errno(env));
}

int
nbsd101_ioctl(
    nix_us_syscall_if_t       *xus,
    nix_monitor_t             *xmon,
    nbsd101_ioctl_args_t const *args,
    nbsd101_ioctl_result_t     *result)
{
	nix_guest_info_t gi;
	nix_env_t       *env = nbsd101_us_syscall_get_nix_env(xus);

	LCLog(g_bsd_log, LCLogDebug, 0, "invoked", 0);

	nix_monitor_get_guest_info(xmon, &gi);

	nix_env_set_errno(env, 0);
	*result = nbsd101_ioctl_dispatch(env, gi.endian, args->arg0, args->arg1, args->arg2);
	return (nix_env_get_errno(env));
}

int
nbsd101_reboot(
    nix_us_syscall_if_t        *xus,
    nix_monitor_t              *xmon,
    nbsd101_reboot_args_t const *args,
    nbsd101_reboot_result_t     *result)
{
	nix_env_t *env = nbsd101_us_syscall_get_nix_env(xus);

	LCLog(g_bsd_log, LCLogDebug, 0, "invoked", 0);

	nix_env_set_errno(env, 0);
	*result = nix_reboot(args->arg0, env);
	return (nix_env_get_errno(env));
}

int
nbsd101_revoke(
    nix_us_syscall_if_t        *xus,
    nix_monitor_t              *xmon,
    nbsd101_revoke_args_t const *args,
    nbsd101_revoke_result_t     *result)
{
	nix_env_t *env = nbsd101_us_syscall_get_nix_env(xus);

	LCLog(g_bsd_log, LCLogDebug, 0, "invoked", 0);

	nix_env_set_errno(env, 0);
	*result = nix_bsd_revoke((char const *)args->arg0, env);
	return (nix_env_get_errno(env));
}

int
nbsd101_symlink(
    nix_us_syscall_if_t         *xus,
    nix_monitor_t               *xmon,
    nbsd101_symlink_args_t const *args,
    nbsd101_symlink_result_t     *result)
{
	nix_env_t *env = nbsd101_us_syscall_get_nix_env(xus);

	LCLog(g_bsd_log, LCLogDebug, 0, "invoked", 0);

	nix_env_set_errno(env, 0);
	*result = nix_symlink((char const *)args->arg0, (char const *)args->arg1, env);
	return (nix_env_get_errno(env));
}

int
nbsd101_readlink(
    nix_us_syscall_if_t          *xus,
    nix_monitor_t                *xmon,
    nbsd101_readlink_args_t const *args,
    nbsd101_readlink_result_t     *result)
{
	nix_env_t *env = nbsd101_us_syscall_get_nix_env(xus);

	LCLog(g_bsd_log, LCLogDebug, 0, "invoked", 0);

	nix_env_set_errno(env, 0);
	*result = nix_readlink((char *)args->arg0, args->arg1, args->arg2, env);
	return (nix_env_get_errno(env));
}

int
nbsd101_execve(
    nix_us_syscall_if_t        *xus,
    nix_monitor_t              *xmon,
    nbsd101_execve_args_t const *args,
    nbsd101_execve_result_t     *result)
{
	LCLog(g_bsd_log, LCLogDebug, 0, "invoked", 0);

	/* Insert here implementation code */

	return (ENOSYS);
}

int
nbsd101_umask(
    nix_us_syscall_if_t       *xus,
    nix_monitor_t             *xmon,
    nbsd101_umask_args_t const *args,
    nbsd101_umask_result_t     *result)
{
	nix_env_t *env = nbsd101_us_syscall_get_nix_env(xus);

	LCLog(g_bsd_log, LCLogDebug, 0, "invoked", 0);

	nix_env_set_errno(env, 0);
	*result = nix_umask(args->arg0, env);
	return (nix_env_get_errno(env));
}

int
nbsd101_chroot(
    nix_us_syscall_if_t        *xus,
    nix_monitor_t              *xmon,
    nbsd101_chroot_args_t const *args,
    nbsd101_chroot_result_t     *result)
{
	nix_env_t *env = nbsd101_us_syscall_get_nix_env(xus);

	LCLog(g_bsd_log, LCLogDebug, 0, "invoked", 0);

	nix_env_set_errno(env, 0);
	*result = nix_chroot((char const *)args->arg0, env);
	return (nix_env_get_errno(env));
}

int
nbsd101_vfork(
    nix_us_syscall_if_t       *xus,
    nix_monitor_t             *xmon,
    nbsd101_vfork_args_t const *args,
    nbsd101_vfork_result_t     *result)
{
	nix_env_t *env = nbsd101_us_syscall_get_nix_env(xus);

	LCLog(g_bsd_log, LCLogDebug, 0, "invoked", 0);

	nix_env_set_errno(env, 0);
	*result = nix_vfork(env);
	return (nix_env_get_errno(env));
}

int
nbsd101_munmap(
    nix_us_syscall_if_t        *xus,
    nix_monitor_t              *xmon,
    nbsd101_munmap_args_t const *args,
    nbsd101_munmap_result_t     *result)
{
	nix_env_t *env = nbsd101_us_syscall_get_nix_env(xus);

	LCLog(g_bsd_log, LCLogDebug, 0, "invoked", 0);

	nix_env_set_errno(env, 0);
	*result = nix_munmap((uintmax_t)(uintptr_t)args->arg0, args->arg1, env);
	return (nix_env_get_errno(env));
}

int
nbsd101_mprotect(
    nix_us_syscall_if_t          *xus,
    nix_monitor_t                *xmon,
    nbsd101_mprotect_args_t const *args,
    nbsd101_mprotect_result_t     *result)
{
	nix_env_t *env = nbsd101_us_syscall_get_nix_env(xus);

	LCLog(g_bsd_log, LCLogDebug, 0, "invoked", 0);

	nix_env_set_errno(env, 0);
	*result = nix_mprotect((uintmax_t)(uintptr_t)args->arg0, args->arg1, args->arg2, env);
	return (nix_env_get_errno(env));
}

int
nbsd101_madvise(
    nix_us_syscall_if_t         *xus,
    nix_monitor_t               *xmon,
    nbsd101_madvise_args_t const *args,
    nbsd101_madvise_result_t     *result)
{
	nix_env_t *env = nbsd101_us_syscall_get_nix_env(xus);

	LCLog(g_bsd_log, LCLogDebug, 0, "invoked", 0);

	nix_env_set_errno(env, 0);
	*result = nix_madvise((uintmax_t)(uintptr_t)args->arg0, args->arg1, args->arg2, env);
	return (nix_env_get_errno(env));
}

int
nbsd101_mincore(
    nix_us_syscall_if_t         *xus,
    nix_monitor_t               *xmon,
    nbsd101_mincore_args_t const *args,
    nbsd101_mincore_result_t     *result)
{
	nix_env_t *env = nbsd101_us_syscall_get_nix_env(xus);

	LCLog(g_bsd_log, LCLogDebug, 0, "invoked", 0);

	nix_env_set_errno(env, 0);
	*result = nix_mincore((uintmax_t)(uintptr_t)args->arg0, args->arg1, (char *)args->arg2, env);
	return (nix_env_get_errno(env));
}

int
nbsd101_getgroups(
    nix_us_syscall_if_t           *xus,
    nix_monitor_t                 *xmon,
    nbsd101_getgroups_args_t const *args,
    nbsd101_getgroups_result_t     *result)
{
	//	nix_env_t *env = nbsd101_us_syscall_get_nix_env(xus);

	LCLog(g_bsd_log, LCLogDebug, 0, "invoked", 0);

	return (ENOSYS);
}

int
nbsd101_setgroups(
    nix_us_syscall_if_t           *xus,
    nix_monitor_t                 *xmon,
    nbsd101_setgroups_args_t const *args,
    nbsd101_setgroups_result_t     *result)
{
	LCLog(g_bsd_log, LCLogDebug, 0, "invoked", 0);

	/* Insert here implementation code */

	return (ENOSYS);
}

int
nbsd101_getpgrp(
    nix_us_syscall_if_t         *xus,
    nix_monitor_t               *xmon,
    nbsd101_getpgrp_args_t const *args,
    nbsd101_getpgrp_result_t     *result)
{
	nix_env_t *env = nbsd101_us_syscall_get_nix_env(xus);

	LCLog(g_bsd_log, LCLogDebug, 0, "invoked", 0);

	nix_env_set_errno(env, 0);
	*result = nix_getpgrp(env);
	return (nix_env_get_errno(env));
}

int
nbsd101_setpgid(
    nix_us_syscall_if_t         *xus,
    nix_monitor_t               *xmon,
    nbsd101_setpgid_args_t const *args,
    nbsd101_setpgid_result_t     *result)
{
	nix_env_t *env = nbsd101_us_syscall_get_nix_env(xus);

	LCLog(g_bsd_log, LCLogDebug, 0, "invoked", 0);

	nix_env_set_errno(env, 0);
	*result = nix_setpgid(args->arg0, args->arg1, env);
	return (nix_env_get_errno(env));
}

int
nbsd101_dup2(
    nix_us_syscall_if_t      *xus,
    nix_monitor_t            *xmon,
    nbsd101_dup2_args_t const *args,
    nbsd101_dup2_result_t     *result)
{
	nix_env_t *env = nbsd101_us_syscall_get_nix_env(xus);

	LCLog(g_bsd_log, LCLogDebug, 0, "invoked", 0);

	nix_env_set_errno(env, 0);
	*result = nix_dup2(args->arg0, args->arg1, env);
	return (nix_env_get_errno(env));
}

int
nbsd101_fcntl(
    nix_us_syscall_if_t       *xus,
    nix_monitor_t             *xmon,
    nbsd101_fcntl_args_t const *args,
    nbsd101_fcntl_result_t     *result)
{
	nix_env_t *env = nbsd101_us_syscall_get_nix_env(xus);

	LCLog(g_bsd_log, LCLogDebug, 0, "invoked", 0);

	nix_env_set_errno(env, 0);
	*result = nix_fcntl(args->arg0, args->arg1, args->arg2, env);
	return (nix_env_get_errno(env));
}

int
nbsd101_fsync(
    nix_us_syscall_if_t       *xus,
    nix_monitor_t             *xmon,
    nbsd101_fsync_args_t const *args,
    nbsd101_fsync_result_t     *result)
{
	nix_env_t *env = nbsd101_us_syscall_get_nix_env(xus);

	LCLog(g_bsd_log, LCLogDebug, 0, "invoked", 0);

	nix_env_set_errno(env, 0);
	*result = nix_fsync(args->arg0, env);
	return (nix_env_get_errno(env));
}

int
nbsd101_setpriority(
    nix_us_syscall_if_t             *xus,
    nix_monitor_t                   *xmon,
    nbsd101_setpriority_args_t const *args,
    nbsd101_setpriority_result_t     *result)
{
	nix_env_t *env = nbsd101_us_syscall_get_nix_env(xus);

	LCLog(g_bsd_log, LCLogDebug, 0, "invoked", 0);

	nix_env_set_errno(env, 0);
	*result = nix_setpriority(args->arg0, args->arg1, args->arg2, env);
	return (nix_env_get_errno(env));
}

int
nbsd101_connect(
    nix_us_syscall_if_t         *xus,
    nix_monitor_t               *xmon,
    nbsd101_connect_args_t const *args,
    nbsd101_connect_result_t     *result)
{
	nix_guest_info_t    gi;
	int                 rc;
	struct nix_sockaddr sa;
	nix_socklen_t       salen = sizeof(sa);
	nix_env_t          *env = nbsd101_us_syscall_get_nix_env(xus);

	LCLog(g_bsd_log, LCLogDebug, 0, "invoked", 0);

	if (args->arg1 == NULL)
		return (EFAULT);

	nix_monitor_get_guest_info(xmon, &gi);

	rc = 0;

	__nix_try
	{
		if (!nbsd101_sockaddr_to_nix_sockaddr(gi.endian, args->arg1, args->arg2, &sa, &salen)) {
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
nbsd101_getpriority(
    nix_us_syscall_if_t             *xus,
    nix_monitor_t                   *xmon,
    nbsd101_getpriority_args_t const *args,
    nbsd101_getpriority_result_t     *result)
{

	LCLog(g_bsd_log, LCLogDebug, 0, "invoked", 0);

	/* Insert here implementation code */

	return (ENOSYS);
}

int
nbsd101_bind(
    nix_us_syscall_if_t      *xus,
    nix_monitor_t            *xmon,
    nbsd101_bind_args_t const *args,
    nbsd101_bind_result_t     *result)
{
	nix_guest_info_t    gi;
	int                 rc;
	struct nix_sockaddr sa;
	nix_socklen_t       salen = sizeof(sa);
	nix_env_t          *env = nbsd101_us_syscall_get_nix_env(xus);

	LCLog(g_bsd_log, LCLogDebug, 0, "invoked", 0);

	if (args->arg1 == NULL)
		return (EFAULT);

	nix_monitor_get_guest_info(xmon, &gi);

	rc = 0;

	__nix_try
	{
		if (!nbsd101_sockaddr_to_nix_sockaddr(gi.endian, args->arg1, args->arg2, &sa, &salen)) {
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
nbsd101_setsockopt(
    nix_us_syscall_if_t            *xus,
    nix_monitor_t                  *xmon,
    nbsd101_setsockopt_args_t const *args,
    nbsd101_setsockopt_result_t     *result)
{
	LCLog(g_bsd_log, LCLogDebug, 0, "invoked", 0);

	/* Insert here implementation code */

	*result = 0;
	return (0); // ENOSYS;
}

int
nbsd101_listen(
    nix_us_syscall_if_t        *xus,
    nix_monitor_t              *xmon,
    nbsd101_listen_args_t const *args,
    nbsd101_listen_result_t     *result)
{
	nix_env_t *env = nbsd101_us_syscall_get_nix_env(xus);

	LCLog(g_bsd_log, LCLogDebug, 0, "invoked", 0);

	nix_env_set_errno(env, 0);
	*result = nix_listen(args->arg0, args->arg1, env);
	return (nix_env_get_errno(env));
}

int
nbsd101_getsockopt(
    nix_us_syscall_if_t            *xus,
    nix_monitor_t                  *xmon,
    nbsd101_getsockopt_args_t const *args,
    nbsd101_getsockopt_result_t     *result)
{
	LCLog(g_bsd_log, LCLogDebug, 0, "invoked", 0);

	/* Insert here implementation code */
	*result = 0;
	return (0); // ENOSYS;
}

int
nbsd101_readv(
    nix_us_syscall_if_t       *xus,
    nix_monitor_t             *xmon,
    nbsd101_readv_args_t const *args,
    nbsd101_readv_result_t     *result)
{
	nix_guest_info_t  gi;
	struct nix_iovec *xiov;
	nix_env_t        *env = nbsd101_us_syscall_get_nix_env(xus);
	nix_mem_if_t     *mem = nix_monitor_get_memory(xmon);

	LCLog(g_bsd_log, LCLogDebug, 0, "invoked", 0);

#if 0	
	nix_monitor_get_guest_info (xmon, &gi);
#else
	/*XXX */
	gi.endian = NIX_ENDIAN_BIG;
#endif

	xiov = __nbsd101_iovec32_copy_from(env, mem, &gi, args->arg1, args->arg2);
	if (xiov != NULL) {
		*result = nix_readv(args->arg0, xiov, args->arg2, env);
		nix_free(xiov);
	}

	return (nix_env_get_errno(env));
}

int
nbsd101_writev(
    nix_us_syscall_if_t        *xus,
    nix_monitor_t              *xmon,
    nbsd101_writev_args_t const *args,
    nbsd101_writev_result_t     *result)
{
	nix_guest_info_t  gi;
	struct nix_iovec *xiov;
	nix_env_t        *env = nbsd101_us_syscall_get_nix_env(xus);
	nix_mem_if_t     *mem = nix_monitor_get_memory(xmon);

	LCLog(g_bsd_log, LCLogDebug, 0, "invoked", 0);

	nix_monitor_get_guest_info(xmon, &gi);

	xiov = __nbsd101_iovec32_copy_from(env, mem, &gi, args->arg1, args->arg2);
	if (xiov != NULL) {
		*result = nix_writev(args->arg0, xiov, args->arg2, env);
		nix_free(xiov);
	}

	return (nix_env_get_errno(env));
}

int
nbsd101_fchown(
    nix_us_syscall_if_t        *xus,
    nix_monitor_t              *xmon,
    nbsd101_fchown_args_t const *args,
    nbsd101_fchown_result_t     *result)
{
	nix_env_t *env = nbsd101_us_syscall_get_nix_env(xus);

	LCLog(g_bsd_log, LCLogDebug, 0, "invoked", 0);

	nix_env_set_errno(env, 0);
	*result = nix_fchown(args->arg0, args->arg1, args->arg2, env);
	return (nix_env_get_errno(env));
}

int
nbsd101_fchmod(
    nix_us_syscall_if_t        *xus,
    nix_monitor_t              *xmon,
    nbsd101_fchmod_args_t const *args,
    nbsd101_fchmod_result_t     *result)
{
	nix_env_t *env = nbsd101_us_syscall_get_nix_env(xus);

	LCLog(g_bsd_log, LCLogDebug, 0, "invoked", 0);

	nix_env_set_errno(env, 0);
	*result = nix_fchmod(args->arg0, args->arg1, env);
	return (nix_env_get_errno(env));
}

int
nbsd101_setreuid(
    nix_us_syscall_if_t          *xus,
    nix_monitor_t                *xmon,
    nbsd101_setreuid_args_t const *args,
    nbsd101_setreuid_result_t     *result)
{
	nix_env_t *env = nbsd101_us_syscall_get_nix_env(xus);

	LCLog(g_bsd_log, LCLogDebug, 0, "invoked", 0);

	nix_env_set_errno(env, 0);
	*result = nix_setreuid(args->arg0, args->arg1, env);
	return (nix_env_get_errno(env));
}

int
nbsd101_setregid(
    nix_us_syscall_if_t          *xus,
    nix_monitor_t                *xmon,
    nbsd101_setregid_args_t const *args,
    nbsd101_setregid_result_t     *result)
{
	nix_env_t *env = nbsd101_us_syscall_get_nix_env(xus);

	LCLog(g_bsd_log, LCLogDebug, 0, "invoked", 0);

	nix_env_set_errno(env, 0);
	*result = nix_setregid(args->arg0, args->arg1, env);
	return (nix_env_get_errno(env));
}

int
nbsd101_rename(
    nix_us_syscall_if_t        *xus,
    nix_monitor_t              *xmon,
    nbsd101_rename_args_t const *args,
    nbsd101_rename_result_t     *result)
{
	nix_env_t *env = nbsd101_us_syscall_get_nix_env(xus);

	LCLog(g_bsd_log, LCLogDebug, 0, "invoked", 0);

	nix_env_set_errno(env, 0);
	*result = nix_rename((char const *)args->arg0, (char const *)args->arg1, env);
	return (nix_env_get_errno(env));
}

int
nbsd101_flock(
    nix_us_syscall_if_t       *xus,
    nix_monitor_t             *xmon,
    nbsd101_flock_args_t const *args,
    nbsd101_flock_result_t     *result)
{
	nix_env_t *env = nbsd101_us_syscall_get_nix_env(xus);

	LCLog(g_bsd_log, LCLogDebug, 0, "invoked", 0);

	nix_env_set_errno(env, 0);
	*result = nix_flock(args->arg0, args->arg1, env);
	return (nix_env_get_errno(env));
}

int
nbsd101_mkfifo(
    nix_us_syscall_if_t        *xus,
    nix_monitor_t              *xmon,
    nbsd101_mkfifo_args_t const *args,
    nbsd101_mkfifo_result_t     *result)
{
	nix_env_t *env = nbsd101_us_syscall_get_nix_env(xus);

	LCLog(g_bsd_log, LCLogDebug, 0, "invoked", 0);

	nix_env_set_errno(env, 0);
	*result = nix_mkfifo((char const *)args->arg0, args->arg1, env);
	return (nix_env_get_errno(env));
}

int
nbsd101_sendto(
    nix_us_syscall_if_t        *xus,
    nix_monitor_t              *xmon,
    nbsd101_sendto_args_t const *args,
    nbsd101_sendto_result_t     *result)
{
	nix_guest_info_t     gi;
	struct nix_sockaddr  sa;
	int                  rc = -1;
	struct nix_sockaddr *psa = args->arg4 != NULL ? &sa : NULL;
	nix_socklen_t        salen = sizeof(sa);
	nix_env_t           *env = nbsd101_us_syscall_get_nix_env(xus);

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
			if (!nbsd101_sockaddr_to_nix_sockaddr(gi.endian, args->arg4, args->arg5, &sa, &salen)) {
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
nbsd101_shutdown(
    nix_us_syscall_if_t          *xus,
    nix_monitor_t                *xmon,
    nbsd101_shutdown_args_t const *args,
    nbsd101_shutdown_result_t     *result)
{
	nix_env_t *env = nbsd101_us_syscall_get_nix_env(xus);

	LCLog(g_bsd_log, LCLogDebug, 0, "invoked", 0);

	nix_env_set_errno(env, 0);
	*result = nix_shutdown(args->arg0, args->arg1, env);
	return (nix_env_get_errno(env));
}

int
nbsd101_socketpair(
    nix_us_syscall_if_t            *xus,
    nix_monitor_t                  *xmon,
    nbsd101_socketpair_args_t const *args,
    nbsd101_socketpair_result_t     *result)
{
	LCLog(g_bsd_log, LCLogDebug, 0, "invoked", 0);

	/* Insert here implementation code */

	return (ENOSYS);
}

int
nbsd101_mkdir(
    nix_us_syscall_if_t       *xus,
    nix_monitor_t             *xmon,
    nbsd101_mkdir_args_t const *args,
    nbsd101_mkdir_result_t     *result)
{
	nix_env_t *env = nbsd101_us_syscall_get_nix_env(xus);

	LCLog(g_bsd_log, LCLogDebug, 0, "invoked", 0);

	nix_env_set_errno(env, 0);
	*result = nix_mkdir((char const *)args->arg0, args->arg1, env);
	return (nix_env_get_errno(env));
}

int
nbsd101_rmdir(
    nix_us_syscall_if_t       *xus,
    nix_monitor_t             *xmon,
    nbsd101_rmdir_args_t const *args,
    nbsd101_rmdir_result_t     *result)
{
	nix_env_t *env = nbsd101_us_syscall_get_nix_env(xus);

	LCLog(g_bsd_log, LCLogDebug, 0, "invoked", 0);

	nix_env_set_errno(env, 0);
	*result = nix_rmdir((char const *)args->arg0, env);
	return (nix_env_get_errno(env));
}

int
nbsd101_setsid(
    nix_us_syscall_if_t        *xus,
    nix_monitor_t              *xmon,
    nbsd101_setsid_args_t const *args,
    nbsd101_setsid_result_t     *result)
{
	nix_env_t *env = nbsd101_us_syscall_get_nix_env(xus);

	LCLog(g_bsd_log, LCLogDebug, 0, "invoked", 0);

	nix_env_set_errno(env, 0);
	*result = nix_setsid(env);
	return (nix_env_get_errno(env));
}

int
nbsd101_nfssvc(
    nix_us_syscall_if_t        *xus,
    nix_monitor_t              *xmon,
    nbsd101_nfssvc_args_t const *args,
    nbsd101_nfssvc_result_t     *result)
{
	LCLog(g_bsd_log, LCLogDebug, 0, "invoked", 0);

	/* Insert here implementation code */

	return (ENOSYS);
}

int
nbsd101_sysarch(
    nix_us_syscall_if_t         *xus,
    nix_monitor_t               *xmon,
    nbsd101_sysarch_args_t const *args,
    nbsd101_sysarch_result_t     *result)
{
	LCLog(g_bsd_log, LCLogDebug, 0, "invoked", 0);

	/* Insert here implementation code */

	return (ENOSYS);
}

int
nbsd101_pread(
    nix_us_syscall_if_t       *xus,
    nix_monitor_t             *xmon,
    nbsd101_pread_args_t const *args,
    nbsd101_pread_result_t     *result)
{
	nix_env_t *env = nbsd101_us_syscall_get_nix_env(xus);

	LCLog(g_bsd_log, LCLogDebug, 0, "invoked", 0);

	nix_env_set_errno(env, 0);
	*result = nix_pread(args->arg0, args->arg1, args->arg2, args->arg4, env);
	return (nix_env_get_errno(env));
}

int
nbsd101_pwrite(
    nix_us_syscall_if_t        *xus,
    nix_monitor_t              *xmon,
    nbsd101_pwrite_args_t const *args,
    nbsd101_pwrite_result_t     *result)
{
	nix_env_t *env = nbsd101_us_syscall_get_nix_env(xus);

	LCLog(g_bsd_log, LCLogDebug, 0, "invoked", 0);

	nix_env_set_errno(env, 0);
	*result = nix_pwrite(args->arg0, args->arg1, args->arg2, args->arg4, env);
	return (nix_env_get_errno(env));
}

int
nbsd101_setgid(
    nix_us_syscall_if_t        *xus,
    nix_monitor_t              *xmon,
    nbsd101_setgid_args_t const *args,
    nbsd101_setgid_result_t     *result)
{
	nix_env_t *env = nbsd101_us_syscall_get_nix_env(xus);

	LCLog(g_bsd_log, LCLogDebug, 0, "invoked", 0);

	nix_env_set_errno(env, 0);
	*result = nix_setgid(args->arg0, env);
	return (nix_env_get_errno(env));
}

int
nbsd101_setegid(
    nix_us_syscall_if_t         *xus,
    nix_monitor_t               *xmon,
    nbsd101_setegid_args_t const *args,
    nbsd101_setegid_result_t     *result)
{
	nix_env_t *env = nbsd101_us_syscall_get_nix_env(xus);

	LCLog(g_bsd_log, LCLogDebug, 0, "invoked", 0);

	nix_env_set_errno(env, 0);
	*result = nix_setegid(args->arg0, env);
	return (nix_env_get_errno(env));
}

int
nbsd101_seteuid(
    nix_us_syscall_if_t         *xus,
    nix_monitor_t               *xmon,
    nbsd101_seteuid_args_t const *args,
    nbsd101_seteuid_result_t     *result)
{
	nix_env_t *env = nbsd101_us_syscall_get_nix_env(xus);

	LCLog(g_bsd_log, LCLogDebug, 0, "invoked", 0);

	nix_env_set_errno(env, 0);
	*result = nix_seteuid(args->arg0, env);
	return (nix_env_get_errno(env));
}

int
nbsd101_lfs_bmapv(
    nix_us_syscall_if_t           *xus,
    nix_monitor_t                 *xmon,
    nbsd101_lfs_bmapv_args_t const *args,
    nbsd101_lfs_bmapv_result_t     *result)
{
	LCLog(g_bsd_log, LCLogDebug, 0, "invoked", 0);

	/* Insert here implementation code */

	return (ENOSYS);
}

int
nbsd101_lfs_markv(
    nix_us_syscall_if_t           *xus,
    nix_monitor_t                 *xmon,
    nbsd101_lfs_markv_args_t const *args,
    nbsd101_lfs_markv_result_t     *result)
{
	LCLog(g_bsd_log, LCLogDebug, 0, "invoked", 0);

	/* Insert here implementation code */

	return (ENOSYS);
}

int
nbsd101_lfs_segclean(
    nix_us_syscall_if_t              *xus,
    nix_monitor_t                    *xmon,
    nbsd101_lfs_segclean_args_t const *args,
    nbsd101_lfs_segclean_result_t     *result)
{
	LCLog(g_bsd_log, LCLogDebug, 0, "invoked", 0);

	/* Insert here implementation code */

	return (ENOSYS);
}

int
nbsd101_pathconf(
    nix_us_syscall_if_t          *xus,
    nix_monitor_t                *xmon,
    nbsd101_pathconf_args_t const *args,
    nbsd101_pathconf_result_t     *result)
{
	nix_env_t *env = nbsd101_us_syscall_get_nix_env(xus);

	LCLog(g_bsd_log, LCLogDebug, 0, "invoked", 0);

	nix_env_set_errno(env, 0);
	*result = nix_pathconf((char const *)args->arg0, args->arg1, env);
	return (nix_env_get_errno(env));
}

int
nbsd101_fpathconf(
    nix_us_syscall_if_t           *xus,
    nix_monitor_t                 *xmon,
    nbsd101_fpathconf_args_t const *args,
    nbsd101_fpathconf_result_t     *result)
{
	nix_env_t *env = nbsd101_us_syscall_get_nix_env(xus);

	LCLog(g_bsd_log, LCLogDebug, 0, "invoked", 0);

	nix_env_set_errno(env, 0);
	*result = nix_fpathconf(args->arg0, args->arg1, env);
	return (nix_env_get_errno(env));
}

int
nbsd101_swapctl(
    nix_us_syscall_if_t         *xus,
    nix_monitor_t               *xmon,
    nbsd101_swapctl_args_t const *args,
    nbsd101_swapctl_result_t     *result)
{
	LCLog(g_bsd_log, LCLogDebug, 0, "invoked", 0);

	/* Insert here implementation code */

	return (ENOSYS);
}

int
nbsd101_getrlimit(
    nix_us_syscall_if_t           *xus,
    nix_monitor_t                 *xmon,
    nbsd101_getrlimit_args_t const *args,
    nbsd101_getrlimit_result_t     *result)
{
	nix_guest_info_t  gi;
	int               resource;
	struct nix_rlimit rl;
	nix_env_t        *env = nbsd101_us_syscall_get_nix_env(xus);

	LCLog(g_bsd_log, LCLogDebug, 0, "invoked", 0);

	if (args->arg1 == NULL)
		return (EFAULT);

	nix_monitor_get_guest_info(xmon, &gi);

	nix_env_set_errno(env, 0);
	switch (args->arg0) {
	case NBSD101_RLIMIT_CPU:
		resource = NIX_RLIMIT_CPU;
		break;
	case NBSD101_RLIMIT_CORE:
		resource = NIX_RLIMIT_CORE;
		break;
	case NBSD101_RLIMIT_DATA:
		resource = NIX_RLIMIT_DATA;
		break;
	case NBSD101_RLIMIT_FSIZE:
		resource = NIX_RLIMIT_FSIZE;
		break;
	case NBSD101_RLIMIT_MEMLOCK:
		resource = NIX_RLIMIT_MEMLOCK;
		break;
	case NBSD101_RLIMIT_NOFILE:
		resource = NIX_RLIMIT_NOFILE;
		break;
	case NBSD101_RLIMIT_NPROC:
		resource = NIX_RLIMIT_NPROC;
		break;
	case NBSD101_RLIMIT_RSS:
		resource = NIX_RLIMIT_RSS;
		break;
	case NBSD101_RLIMIT_STACK:
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
		nix_rlimit_to_nbsd101_rlimit(gi.endian, &rl, args->arg1);
	}
	__nix_catch_any
	{
		nix_env_set_errno(env, EFAULT);
	}
	__nix_end_try

	    return (nix_env_get_errno(env));
}

int
nbsd101_setrlimit(
    nix_us_syscall_if_t           *xus,
    nix_monitor_t                 *xmon,
    nbsd101_setrlimit_args_t const *args,
    nbsd101_setrlimit_result_t     *result)
{
	LCLog(g_bsd_log, LCLogDebug, 0, "invoked", 0);

	/* Insert here implementation code */

	*result = 0;
	return (0); // XXX ENOSYS;
}

int
nbsd101_mmap(
    nix_us_syscall_if_t      *xus,
    nix_monitor_t            *xmon,
    nbsd101_mmap_args_t const *args,
    nbsd101_mmap_result_t     *result)
{
	int        prot;
	int        flags;
	nix_env_t *env = nbsd101_us_syscall_get_nix_env(xus);

	LCLog(g_bsd_log, LCLogDebug, 0, "invoked", 0);

	/*
	 * NIX PROT and FLAGS values are based on BSD.
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
nbsd101_lseek(
    nix_us_syscall_if_t       *xus,
    nix_monitor_t             *xmon,
    nbsd101_lseek_args_t const *args,
    nbsd101_lseek_result_t     *result)
{
	nix_env_t *env = nbsd101_us_syscall_get_nix_env(xus);

	LCLog(g_bsd_log, LCLogDebug, 0, "invoked", 0);

	nix_env_set_errno(env, 0);
	*result = nix_lseek(args->arg0, args->arg2, args->arg3, env);
	return (nix_env_get_errno(env));
}

int
nbsd101_truncate(
    nix_us_syscall_if_t          *xus,
    nix_monitor_t                *xmon,
    nbsd101_truncate_args_t const *args,
    nbsd101_truncate_result_t     *result)
{
	nix_env_t *env = nbsd101_us_syscall_get_nix_env(xus);

	LCLog(g_bsd_log, LCLogDebug, 0, "invoked", 0);

	nix_env_set_errno(env, 0);
	*result = nix_truncate((char const *)args->arg0, args->arg1, env);
	return (nix_env_get_errno(env));
}

int
nbsd101_ftruncate(
    nix_us_syscall_if_t           *xus,
    nix_monitor_t                 *xmon,
    nbsd101_ftruncate_args_t const *args,
    nbsd101_ftruncate_result_t     *result)
{
	nix_env_t *env = nbsd101_us_syscall_get_nix_env(xus);

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
nbsd101___sysctl(
    nix_us_syscall_if_t          *xus,
    nix_monitor_t                *xmon,
    nbsd101___sysctl_args_t const *args,
    nbsd101___sysctl_result_t     *result)
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
	case NBSD101_CTL_KERN:
		switch (GE32(&gi, mib[1])) {
		case NBSD101_KERN_OSTYPE:
			LCAssert(g_bsd_log, miblen == 2);
			__nix_try
			{
				__str_copy(&gi, (char *)oldp, oldlenp, "NetBSD");
			}
			__nix_catch_any
			{
				*result = -1;
				return (EFAULT);
			}
			__nix_end_try return (0);

		case NBSD101_KERN_OSRELEASE:
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

		case NBSD101_KERN_ARGMAX:
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

		case NBSD101_KERN_OSVERSION:
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

		case NBSD101_KERN_HOSTNAME: {
			char       hostname[512];
			nix_env_t *env = nbsd101_us_syscall_get_nix_env(xus);

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

		case NBSD101_KERN_DOMAINNAME: {
			char       domainname[512];
			nix_env_t *env = nbsd101_us_syscall_get_nix_env(xus);

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

		case NBSD101_KERN_ARND: {
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

	case NBSD101_CTL_HW:
		switch (GE32(&gi, mib[1])) {
		case NBSD101_HW_MACHINE:
			LCAssert(g_bsd_log, miblen == 2);
			__nix_try
			{
				__str_copy(&gi, (char *)oldp, oldlenp, NBSD101_MACHINE_NAME);
			}
			__nix_catch_any
			{
				*result = -1;
				return (EFAULT);
			}
			__nix_end_try return (0);

		case NBSD101_HW_NCPUS:
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

		case NBSD101_HW_PAGESIZE:
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
nbsd101_mlock(
    nix_us_syscall_if_t       *xus,
    nix_monitor_t             *xmon,
    nbsd101_mlock_args_t const *args,
    nbsd101_mlock_result_t     *result)
{
	nix_env_t *env = nbsd101_us_syscall_get_nix_env(xus);

	LCLog(g_bsd_log, LCLogDebug, 0, "invoked", 0);

	nix_env_set_errno(env, 0);
	*result = nix_mlock((uintmax_t)(uintptr_t)args->arg0, args->arg1, env);
	return (nix_env_get_errno(env));
}

int
nbsd101_munlock(
    nix_us_syscall_if_t         *xus,
    nix_monitor_t               *xmon,
    nbsd101_munlock_args_t const *args,
    nbsd101_munlock_result_t     *result)
{
	nix_env_t *env = nbsd101_us_syscall_get_nix_env(xus);

	LCLog(g_bsd_log, LCLogDebug, 0, "invoked", 0);

	nix_env_set_errno(env, 0);
	*result = nix_munlock((uintmax_t)(uintptr_t)args->arg0, args->arg1, env);
	return (nix_env_get_errno(env));
}

int
nbsd101_getpgid(
    nix_us_syscall_if_t         *xus,
    nix_monitor_t               *xmon,
    nbsd101_getpgid_args_t const *args,
    nbsd101_getpgid_result_t     *result)
{
	nix_env_t *env = nbsd101_us_syscall_get_nix_env(xus);

	LCLog(g_bsd_log, LCLogDebug, 0, "invoked", 0);

	nix_env_set_errno(env, 0);
	*result = nix_getpgid(args->arg0, env);
	return (nix_env_get_errno(env));
}

int
nbsd101_semget(
    nix_us_syscall_if_t        *xus,
    nix_monitor_t              *xmon,
    nbsd101_semget_args_t const *args,
    nbsd101_semget_result_t     *result)
{
	LCLog(g_bsd_log, LCLogDebug, 0, "invoked", 0);

	/* Insert here implementation code */

	return (ENOSYS);
}

int
nbsd101_msgget(
    nix_us_syscall_if_t        *xus,
    nix_monitor_t              *xmon,
    nbsd101_msgget_args_t const *args,
    nbsd101_msgget_result_t     *result)
{
	LCLog(g_bsd_log, LCLogDebug, 0, "invoked", 0);

	/* Insert here implementation code */

	return (ENOSYS);
}

int
nbsd101_msgsnd(
    nix_us_syscall_if_t        *xus,
    nix_monitor_t              *xmon,
    nbsd101_msgsnd_args_t const *args,
    nbsd101_msgsnd_result_t     *result)
{
	LCLog(g_bsd_log, LCLogDebug, 0, "invoked", 0);

	/* Insert here implementation code */

	return (ENOSYS);
}

int
nbsd101_msgrcv(
    nix_us_syscall_if_t        *xus,
    nix_monitor_t              *xmon,
    nbsd101_msgrcv_args_t const *args,
    nbsd101_msgrcv_result_t     *result)
{
	LCLog(g_bsd_log, LCLogDebug, 0, "invoked", 0);

	/* Insert here implementation code */

	return (ENOSYS);
}

int
nbsd101_shmat(
    nix_us_syscall_if_t       *xus,
    nix_monitor_t             *xmon,
    nbsd101_shmat_args_t const *args,
    nbsd101_shmat_result_t     *result)
{
	LCLog(g_bsd_log, LCLogDebug, 0, "invoked", 0);

	/* Insert here implementation code */

	return (ENOSYS);
}

int
nbsd101_shmdt(
    nix_us_syscall_if_t       *xus,
    nix_monitor_t             *xmon,
    nbsd101_shmdt_args_t const *args,
    nbsd101_shmdt_result_t     *result)
{
	LCLog(g_bsd_log, LCLogDebug, 0, "invoked", 0);

	/* Insert here implementation code */

	return (ENOSYS);
}

int
nbsd101_minherit(
    nix_us_syscall_if_t          *xus,
    nix_monitor_t                *xmon,
    nbsd101_minherit_args_t const *args,
    nbsd101_minherit_result_t     *result)
{
	nix_env_t *env = nbsd101_us_syscall_get_nix_env(xus);

	LCLog(g_bsd_log, LCLogDebug, 0, "invoked", 0);

	nix_env_set_errno(env, 0);
	*result = nix_minherit((uintmax_t)(uintptr_t)args->arg0, args->arg1, args->arg2, env);
	return (nix_env_get_errno(env));
}

int
nbsd101_poll(
    nix_us_syscall_if_t      *xus,
    nix_monitor_t            *xmon,
    nbsd101_poll_args_t const *args,
    nbsd101_poll_result_t     *result)
{
	nix_guest_info_t      gi;
	struct nbsd101_pollfd *ofds = args->arg0;
	struct nix_pollfd    *fds = NULL;
	nix_env_t            *env = nbsd101_us_syscall_get_nix_env(xus);

	LCLog(g_bsd_log, LCLogDebug, 0, "invoked", 0);

	nix_monitor_get_guest_info(xmon, &gi);

	nix_env_set_errno(env, 0);
	fds = __nbsd101_pollfd_copy_from(env, &gi, ofds, args->arg1);
	if (fds != NULL) {
		*result = nix_poll(fds, args->arg1, args->arg2, env);

		if (nix_env_get_errno(env) == 0) {
			/* Copy back the results */
			__nix_try
			{
				size_t n;

				for (n = 0; n < args->arg1; n++)
					nix_pollfd_to_nbsd101_pollfd(gi.endian, fds + n, ofds + n);
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
nbsd101_lchown(
    nix_us_syscall_if_t        *xus,
    nix_monitor_t              *xmon,
    nbsd101_lchown_args_t const *args,
    nbsd101_lchown_result_t     *result)
{
	nix_env_t *env = nbsd101_us_syscall_get_nix_env(xus);

	LCLog(g_bsd_log, LCLogDebug, 0, "invoked", 0);

	nix_env_set_errno(env, 0);
	*result = nix_lchown((char const *)args->arg0, args->arg1, args->arg2, env);
	return (nix_env_get_errno(env));
}

int
nbsd101_getsid(
    nix_us_syscall_if_t        *xus,
    nix_monitor_t              *xmon,
    nbsd101_getsid_args_t const *args,
    nbsd101_getsid_result_t     *result)
{
	nix_env_t *env = nbsd101_us_syscall_get_nix_env(xus);

	LCLog(g_bsd_log, LCLogDebug, 0, "invoked", 0);

	nix_env_set_errno(env, 0);
	*result = nix_getsid(args->arg0, env);
	return (nix_env_get_errno(env));
}

int
nbsd101_pipe(
    nix_us_syscall_if_t      *xus,
    nix_monitor_t            *xmon,
    nbsd101_pipe_args_t const *args,
    nbsd101_pipe_result_t     *result)
{
	int        fds[2];
	int        rc;
	nix_env_t *env = nbsd101_us_syscall_get_nix_env(xus);

	(void)args;

	LCLog(g_bsd_log, LCLogDebug, 0, "invoked", 0);

	nix_env_set_errno(env, 0);
	rc = nix_pipe(fds, env);
	if (rc != 0)
		return (nix_env_get_errno(env));

	/*
	 * Unlike OpenBSD's pipe(2) (which takes a user buffer), NetBSD
	 * returns the read descriptor in r2 and the write descriptor in
	 * r3.  The m88k dispatcher splits a dword result high->r2, low->r3,
	 * so pack the pair accordingly (see nbsd101-guest.c set_result).
	 */
	*result = ((uint64_t)(uint32_t)fds[0] << 32) | (uint32_t)fds[1];
	return (0);
}

int
nbsd101_preadv(
    nix_us_syscall_if_t        *xus,
    nix_monitor_t              *xmon,
    nbsd101_preadv_args_t const *args,
    nbsd101_preadv_result_t     *result)
{
	LCLog(g_bsd_log, LCLogDebug, 0, "invoked", 0);

	/* Insert here implementation code */

	return (ENOSYS);
}

int
nbsd101_pwritev(
    nix_us_syscall_if_t         *xus,
    nix_monitor_t               *xmon,
    nbsd101_pwritev_args_t const *args,
    nbsd101_pwritev_result_t     *result)
{
	LCLog(g_bsd_log, LCLogDebug, 0, "invoked", 0);

	/* Insert here implementation code */

	return (ENOSYS);
}

int
nbsd101_kqueue(
    nix_us_syscall_if_t        *xus,
    nix_monitor_t              *xmon,
    nbsd101_kqueue_args_t const *args,
    nbsd101_kqueue_result_t     *result)
{
	nix_env_t *env = nbsd101_us_syscall_get_nix_env(xus);

	LCLog(g_bsd_log, LCLogDebug, 0, "invoked", 0);

	nix_env_set_errno(env, 0);
	*result = nix_bsd_kqueue(env);
	return (nix_env_get_errno(env));
}

int
nbsd101_mlockall(
    nix_us_syscall_if_t          *xus,
    nix_monitor_t                *xmon,
    nbsd101_mlockall_args_t const *args,
    nbsd101_mlockall_result_t     *result)
{
	nix_env_t *env = nbsd101_us_syscall_get_nix_env(xus);

	LCLog(g_bsd_log, LCLogDebug, 0, "invoked", 0);

	nix_env_set_errno(env, 0);
	*result = nix_mlockall(args->arg0, env);
	return (nix_env_get_errno(env));
}

int
nbsd101_munlockall(
    nix_us_syscall_if_t            *xus,
    nix_monitor_t                  *xmon,
    nbsd101_munlockall_args_t const *args,
    nbsd101_munlockall_result_t     *result)
{
	nix_env_t *env = nbsd101_us_syscall_get_nix_env(xus);

	LCLog(g_bsd_log, LCLogDebug, 0, "invoked", 0);

	nix_env_set_errno(env, 0);
	*result = nix_munlockall(env);
	return (nix_env_get_errno(env));
}

int
nbsd101_shmget(
    nix_us_syscall_if_t        *xus,
    nix_monitor_t              *xmon,
    nbsd101_shmget_args_t const *args,
    nbsd101_shmget_result_t     *result)
{
	LCLog(g_bsd_log, LCLogDebug, 0, "invoked", 0);

	/* Insert here implementation code */

	return (ENOSYS);
}

int
nbsd101_semop(
    nix_us_syscall_if_t       *xus,
    nix_monitor_t             *xmon,
    nbsd101_semop_args_t const *args,
    nbsd101_semop_result_t     *result)
{
	LCLog(g_bsd_log, LCLogDebug, 0, "invoked", 0);

	/* Insert here implementation code */

	return (ENOSYS);
}

int
nbsd101_sched_yield(
    nix_us_syscall_if_t             *xus,
    nix_monitor_t                   *xmon,
    nbsd101_sched_yield_args_t const *args,
    nbsd101_sched_yield_result_t     *result)
{
	nix_env_t *env = nbsd101_us_syscall_get_nix_env(xus);

	LCLog(g_bsd_log, LCLogDebug, 0, "invoked", 0);

	nix_env_set_errno(env, 0);
	*result = nix_rt_sched_yield(env);
	return (nix_env_get_errno(env));
}

int
nbsd101___getcwd(
    nix_us_syscall_if_t          *xus,
    nix_monitor_t                *xmon,
    nbsd101___getcwd_args_t const *args,
    nbsd101___getcwd_result_t     *result)
{
	nix_env_t *env = nbsd101_us_syscall_get_nix_env(xus);

	LCLog(g_bsd_log, LCLogDebug, 0, "invoked", 0);

	nix_env_set_errno(env, 0);
	*result = nix_getcwd((char *)args->arg0, args->arg1, env);
	return (nix_env_get_errno(env));
}

int
nbsd101_obreak(
    nix_us_syscall_if_t        *xus,
    nix_monitor_t              *xmon,
    nbsd101_obreak_args_t const *args,
    nbsd101_obreak_result_t     *result)
{
	nix_env_t *env = nbsd101_us_syscall_get_nix_env(xus);

	LCLog(g_bsd_log, LCLogDebug, 0, "invoked", 0);

	nix_env_set_errno(env, 0);
	nix_brk((uintmax_t)(uintptr_t)args->arg0, env);
	*result = 0;
	return (nix_env_get_errno(env));
}
int
nbsd101_unmount(
    nix_us_syscall_if_t         *xus,
    nix_monitor_t               *xmon,
    nbsd101_unmount_args_t const *args,
    nbsd101_unmount_result_t     *result)
{
	nix_env_t *env = nbsd101_us_syscall_get_nix_env(xus);

	LCLog(g_bsd_log, LCLogDebug, 0, "invoked", 0);

	nix_env_set_errno(env, 0);
	*result = nix_bsd_unmount((char const *)args->arg0, args->arg1, env);
	return (nix_env_get_errno(env));
}
int
nbsd101___getlogin(
    nix_us_syscall_if_t            *xus,
    nix_monitor_t                  *xmon,
    nbsd101___getlogin_args_t const *args,
    nbsd101___getlogin_result_t     *result)
{
	nix_env_t *env = nbsd101_us_syscall_get_nix_env(xus);

	LCLog(g_bsd_log, LCLogDebug, 0, "invoked", 0);

	nix_env_set_errno(env, 0);
	*result = nix_bsd_getlogin((char *)args->arg0, args->arg1, env);
	return (nix_env_get_errno(env));
}
int
nbsd101___setlogin(
    nix_us_syscall_if_t            *xus,
    nix_monitor_t                  *xmon,
    nbsd101___setlogin_args_t const *args,
    nbsd101___setlogin_result_t     *result)
{
	nix_env_t *env = nbsd101_us_syscall_get_nix_env(xus);

	LCLog(g_bsd_log, LCLogDebug, 0, "invoked", 0);

	nix_env_set_errno(env, 0);
	*result = nix_bsd_setlogin((char const *)args->arg0, env);
	return (nix_env_get_errno(env));
}
int
nbsd101___posix_rename(
    nix_us_syscall_if_t                *xus,
    nix_monitor_t                      *xmon,
    nbsd101___posix_rename_args_t const *args,
    nbsd101___posix_rename_result_t     *result)
{
	nix_env_t *env = nbsd101_us_syscall_get_nix_env(xus);

	LCLog(g_bsd_log, LCLogDebug, 0, "invoked", 0);

	nix_env_set_errno(env, 0);
	*result = nix_rename((char const *)args->arg0, (char const *)args->arg1, env);
	return (nix_env_get_errno(env));
}
int
nbsd101___posix_chown(
    nix_us_syscall_if_t               *xus,
    nix_monitor_t                     *xmon,
    nbsd101___posix_chown_args_t const *args,
    nbsd101___posix_chown_result_t     *result)
{
	nix_env_t *env = nbsd101_us_syscall_get_nix_env(xus);

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
nbsd101___posix_fchown(
    nix_us_syscall_if_t                *xus,
    nix_monitor_t                      *xmon,
    nbsd101___posix_fchown_args_t const *args,
    nbsd101___posix_fchown_result_t     *result)
{
	nix_env_t *env = nbsd101_us_syscall_get_nix_env(xus);

	LCLog(g_bsd_log, LCLogDebug, 0, "invoked", 0);

	nix_env_set_errno(env, 0);
	*result = nix_fchown(args->arg0, args->arg1, args->arg2, env);
	return (nix_env_get_errno(env));
}
int
nbsd101___posix_lchown(
    nix_us_syscall_if_t                *xus,
    nix_monitor_t                      *xmon,
    nbsd101___posix_lchown_args_t const *args,
    nbsd101___posix_lchown_result_t     *result)
{
	nix_env_t *env = nbsd101_us_syscall_get_nix_env(xus);

	LCLog(g_bsd_log, LCLogDebug, 0, "invoked", 0);

	nix_env_set_errno(env, 0);
	*result = nix_lchown((char const *)args->arg0, args->arg1, args->arg2, env);
	return (nix_env_get_errno(env));
}

int
nbsd101_ovadvise(
    nix_us_syscall_if_t          *xus,
    nix_monitor_t                *xmon,
    nbsd101_ovadvise_args_t const *args,
    nbsd101_ovadvise_result_t     *result)
{
	(void)xus;
	(void)xmon;
	(void)args;
	(void)result;

	LCLog(g_bsd_log, LCLogWarning, 0,
	      "unimplemented NetBSD 10.1 syscall `ovadvise'", 0);

	return (ENOSYS);
}

int
nbsd101_getrandom(
    nix_us_syscall_if_t           *xus,
    nix_monitor_t                 *xmon,
    nbsd101_getrandom_args_t const *args,
    nbsd101_getrandom_result_t     *result)
{
	(void)xus;
	(void)xmon;
	(void)args;
	(void)result;

	LCLog(g_bsd_log, LCLogWarning, 0,
	      "unimplemented NetBSD 10.1 syscall `getrandom'", 0);

	return (ENOSYS);
}

int
nbsd101___futex(
    nix_us_syscall_if_t         *xus,
    nix_monitor_t               *xmon,
    nbsd101___futex_args_t const *args,
    nbsd101___futex_result_t     *result)
{
	(void)xus;
	(void)xmon;
	(void)args;
	(void)result;

	LCLog(g_bsd_log, LCLogWarning, 0,
	      "unimplemented NetBSD 10.1 syscall `__futex'", 0);

	return (ENOSYS);
}

int
nbsd101___futex_set_robust_list(
    nix_us_syscall_if_t                         *xus,
    nix_monitor_t                               *xmon,
    nbsd101___futex_set_robust_list_args_t const *args,
    nbsd101___futex_set_robust_list_result_t     *result)
{
	(void)xus;
	(void)xmon;
	(void)args;
	(void)result;

	LCLog(g_bsd_log, LCLogWarning, 0,
	      "unimplemented NetBSD 10.1 syscall `__futex_set_robust_list'", 0);

	return (ENOSYS);
}

int
nbsd101___futex_get_robust_list(
    nix_us_syscall_if_t                         *xus,
    nix_monitor_t                               *xmon,
    nbsd101___futex_get_robust_list_args_t const *args,
    nbsd101___futex_get_robust_list_result_t     *result)
{
	(void)xus;
	(void)xmon;
	(void)args;
	(void)result;

	LCLog(g_bsd_log, LCLogWarning, 0,
	      "unimplemented NetBSD 10.1 syscall `__futex_get_robust_list'", 0);

	return (ENOSYS);
}

int
nbsd101_ntp_adjtime(
    nix_us_syscall_if_t             *xus,
    nix_monitor_t                   *xmon,
    nbsd101_ntp_adjtime_args_t const *args,
    nbsd101_ntp_adjtime_result_t     *result)
{
	(void)xus;
	(void)xmon;
	(void)args;
	(void)result;

	LCLog(g_bsd_log, LCLogWarning, 0,
	      "unimplemented NetBSD 10.1 syscall `ntp_adjtime'", 0);

	return (ENOSYS);
}

int
nbsd101_timerfd_create(
    nix_us_syscall_if_t                *xus,
    nix_monitor_t                      *xmon,
    nbsd101_timerfd_create_args_t const *args,
    nbsd101_timerfd_create_result_t     *result)
{
	(void)xus;
	(void)xmon;
	(void)args;
	(void)result;

	LCLog(g_bsd_log, LCLogWarning, 0,
	      "unimplemented NetBSD 10.1 syscall `timerfd_create'", 0);

	return (ENOSYS);
}

int
nbsd101_timerfd_settime(
    nix_us_syscall_if_t                 *xus,
    nix_monitor_t                       *xmon,
    nbsd101_timerfd_settime_args_t const *args,
    nbsd101_timerfd_settime_result_t     *result)
{
	(void)xus;
	(void)xmon;
	(void)args;
	(void)result;

	LCLog(g_bsd_log, LCLogWarning, 0,
	      "unimplemented NetBSD 10.1 syscall `timerfd_settime'", 0);

	return (ENOSYS);
}

int
nbsd101_timerfd_gettime(
    nix_us_syscall_if_t                 *xus,
    nix_monitor_t                       *xmon,
    nbsd101_timerfd_gettime_args_t const *args,
    nbsd101_timerfd_gettime_result_t     *result)
{
	(void)xus;
	(void)xmon;
	(void)args;
	(void)result;

	LCLog(g_bsd_log, LCLogWarning, 0,
	      "unimplemented NetBSD 10.1 syscall `timerfd_gettime'", 0);

	return (ENOSYS);
}

int
nbsd101_getsockopt2(
    nix_us_syscall_if_t             *xus,
    nix_monitor_t                   *xmon,
    nbsd101_getsockopt2_args_t const *args,
    nbsd101_getsockopt2_result_t     *result)
{
	(void)xus;
	(void)xmon;
	(void)args;
	(void)result;

	LCLog(g_bsd_log, LCLogWarning, 0,
	      "unimplemented NetBSD 10.1 syscall `getsockopt2'", 0);

	return (ENOSYS);
}

int
nbsd101_undelete(
    nix_us_syscall_if_t          *xus,
    nix_monitor_t                *xmon,
    nbsd101_undelete_args_t const *args,
    nbsd101_undelete_result_t     *result)
{
	(void)xus;
	(void)xmon;
	(void)args;
	(void)result;

	LCLog(g_bsd_log, LCLogWarning, 0,
	      "unimplemented NetBSD 10.1 syscall `undelete'", 0);

	return (ENOSYS);
}

int
nbsd101_semconfig(
    nix_us_syscall_if_t           *xus,
    nix_monitor_t                 *xmon,
    nbsd101_semconfig_args_t const *args,
    nbsd101_semconfig_result_t     *result)
{
	(void)xus;
	(void)xmon;
	(void)args;
	(void)result;

	LCLog(g_bsd_log, LCLogWarning, 0,
	      "unimplemented NetBSD 10.1 syscall `semconfig'", 0);

	return (ENOSYS);
}

int
nbsd101_timer_create(
    nix_us_syscall_if_t              *xus,
    nix_monitor_t                    *xmon,
    nbsd101_timer_create_args_t const *args,
    nbsd101_timer_create_result_t     *result)
{
	(void)xus;
	(void)xmon;
	(void)args;
	(void)result;

	LCLog(g_bsd_log, LCLogWarning, 0,
	      "unimplemented NetBSD 10.1 syscall `timer_create'", 0);

	return (ENOSYS);
}

int
nbsd101_timer_delete(
    nix_us_syscall_if_t              *xus,
    nix_monitor_t                    *xmon,
    nbsd101_timer_delete_args_t const *args,
    nbsd101_timer_delete_result_t     *result)
{
	(void)xus;
	(void)xmon;
	(void)args;
	(void)result;

	LCLog(g_bsd_log, LCLogWarning, 0,
	      "unimplemented NetBSD 10.1 syscall `timer_delete'", 0);

	return (ENOSYS);
}

int
nbsd101_timer_getoverrun(
    nix_us_syscall_if_t                  *xus,
    nix_monitor_t                        *xmon,
    nbsd101_timer_getoverrun_args_t const *args,
    nbsd101_timer_getoverrun_result_t     *result)
{
	(void)xus;
	(void)xmon;
	(void)args;
	(void)result;

	LCLog(g_bsd_log, LCLogWarning, 0,
	      "unimplemented NetBSD 10.1 syscall `timer_getoverrun'", 0);

	return (ENOSYS);
}

int
nbsd101_fdatasync(
    nix_us_syscall_if_t           *xus,
    nix_monitor_t                 *xmon,
    nbsd101_fdatasync_args_t const *args,
    nbsd101_fdatasync_result_t     *result)
{
	(void)xus;
	(void)xmon;
	(void)args;
	(void)result;

	LCLog(g_bsd_log, LCLogWarning, 0,
	      "unimplemented NetBSD 10.1 syscall `fdatasync'", 0);

	return (ENOSYS);
}

int
nbsd101_sigqueueinfo(
    nix_us_syscall_if_t              *xus,
    nix_monitor_t                    *xmon,
    nbsd101_sigqueueinfo_args_t const *args,
    nbsd101_sigqueueinfo_result_t     *result)
{
	(void)xus;
	(void)xmon;
	(void)args;
	(void)result;

	LCLog(g_bsd_log, LCLogWarning, 0,
	      "unimplemented NetBSD 10.1 syscall `sigqueueinfo'", 0);

	return (ENOSYS);
}

int
nbsd101_modctl(
    nix_us_syscall_if_t        *xus,
    nix_monitor_t              *xmon,
    nbsd101_modctl_args_t const *args,
    nbsd101_modctl_result_t     *result)
{
	(void)xus;
	(void)xmon;
	(void)args;
	(void)result;

	LCLog(g_bsd_log, LCLogWarning, 0,
	      "unimplemented NetBSD 10.1 syscall `modctl'", 0);

	return (ENOSYS);
}

int
nbsd101__ksem_init(
    nix_us_syscall_if_t            *xus,
    nix_monitor_t                  *xmon,
    nbsd101__ksem_init_args_t const *args,
    nbsd101__ksem_init_result_t     *result)
{
	(void)xus;
	(void)xmon;
	(void)args;
	(void)result;

	LCLog(g_bsd_log, LCLogWarning, 0,
	      "unimplemented NetBSD 10.1 syscall `_ksem_init'", 0);

	return (ENOSYS);
}

int
nbsd101__ksem_open(
    nix_us_syscall_if_t            *xus,
    nix_monitor_t                  *xmon,
    nbsd101__ksem_open_args_t const *args,
    nbsd101__ksem_open_result_t     *result)
{
	(void)xus;
	(void)xmon;
	(void)args;
	(void)result;

	LCLog(g_bsd_log, LCLogWarning, 0,
	      "unimplemented NetBSD 10.1 syscall `_ksem_open'", 0);

	return (ENOSYS);
}

int
nbsd101__ksem_unlink(
    nix_us_syscall_if_t              *xus,
    nix_monitor_t                    *xmon,
    nbsd101__ksem_unlink_args_t const *args,
    nbsd101__ksem_unlink_result_t     *result)
{
	(void)xus;
	(void)xmon;
	(void)args;
	(void)result;

	LCLog(g_bsd_log, LCLogWarning, 0,
	      "unimplemented NetBSD 10.1 syscall `_ksem_unlink'", 0);

	return (ENOSYS);
}

int
nbsd101__ksem_close(
    nix_us_syscall_if_t             *xus,
    nix_monitor_t                   *xmon,
    nbsd101__ksem_close_args_t const *args,
    nbsd101__ksem_close_result_t     *result)
{
	(void)xus;
	(void)xmon;
	(void)args;
	(void)result;

	LCLog(g_bsd_log, LCLogWarning, 0,
	      "unimplemented NetBSD 10.1 syscall `_ksem_close'", 0);

	return (ENOSYS);
}

int
nbsd101__ksem_post(
    nix_us_syscall_if_t            *xus,
    nix_monitor_t                  *xmon,
    nbsd101__ksem_post_args_t const *args,
    nbsd101__ksem_post_result_t     *result)
{
	(void)xus;
	(void)xmon;
	(void)args;
	(void)result;

	LCLog(g_bsd_log, LCLogWarning, 0,
	      "unimplemented NetBSD 10.1 syscall `_ksem_post'", 0);

	return (ENOSYS);
}

int
nbsd101__ksem_wait(
    nix_us_syscall_if_t            *xus,
    nix_monitor_t                  *xmon,
    nbsd101__ksem_wait_args_t const *args,
    nbsd101__ksem_wait_result_t     *result)
{
	(void)xus;
	(void)xmon;
	(void)args;
	(void)result;

	LCLog(g_bsd_log, LCLogWarning, 0,
	      "unimplemented NetBSD 10.1 syscall `_ksem_wait'", 0);

	return (ENOSYS);
}

int
nbsd101__ksem_trywait(
    nix_us_syscall_if_t               *xus,
    nix_monitor_t                     *xmon,
    nbsd101__ksem_trywait_args_t const *args,
    nbsd101__ksem_trywait_result_t     *result)
{
	(void)xus;
	(void)xmon;
	(void)args;
	(void)result;

	LCLog(g_bsd_log, LCLogWarning, 0,
	      "unimplemented NetBSD 10.1 syscall `_ksem_trywait'", 0);

	return (ENOSYS);
}

int
nbsd101__ksem_getvalue(
    nix_us_syscall_if_t                *xus,
    nix_monitor_t                      *xmon,
    nbsd101__ksem_getvalue_args_t const *args,
    nbsd101__ksem_getvalue_result_t     *result)
{
	(void)xus;
	(void)xmon;
	(void)args;
	(void)result;

	LCLog(g_bsd_log, LCLogWarning, 0,
	      "unimplemented NetBSD 10.1 syscall `_ksem_getvalue'", 0);

	return (ENOSYS);
}

int
nbsd101__ksem_destroy(
    nix_us_syscall_if_t               *xus,
    nix_monitor_t                     *xmon,
    nbsd101__ksem_destroy_args_t const *args,
    nbsd101__ksem_destroy_result_t     *result)
{
	(void)xus;
	(void)xmon;
	(void)args;
	(void)result;

	LCLog(g_bsd_log, LCLogWarning, 0,
	      "unimplemented NetBSD 10.1 syscall `_ksem_destroy'", 0);

	return (ENOSYS);
}

int
nbsd101__ksem_timedwait(
    nix_us_syscall_if_t                 *xus,
    nix_monitor_t                       *xmon,
    nbsd101__ksem_timedwait_args_t const *args,
    nbsd101__ksem_timedwait_result_t     *result)
{
	(void)xus;
	(void)xmon;
	(void)args;
	(void)result;

	LCLog(g_bsd_log, LCLogWarning, 0,
	      "unimplemented NetBSD 10.1 syscall `_ksem_timedwait'", 0);

	return (ENOSYS);
}

int
nbsd101_mq_open(
    nix_us_syscall_if_t         *xus,
    nix_monitor_t               *xmon,
    nbsd101_mq_open_args_t const *args,
    nbsd101_mq_open_result_t     *result)
{
	(void)xus;
	(void)xmon;
	(void)args;
	(void)result;

	LCLog(g_bsd_log, LCLogWarning, 0,
	      "unimplemented NetBSD 10.1 syscall `mq_open'", 0);

	return (ENOSYS);
}

int
nbsd101_mq_close(
    nix_us_syscall_if_t          *xus,
    nix_monitor_t                *xmon,
    nbsd101_mq_close_args_t const *args,
    nbsd101_mq_close_result_t     *result)
{
	(void)xus;
	(void)xmon;
	(void)args;
	(void)result;

	LCLog(g_bsd_log, LCLogWarning, 0,
	      "unimplemented NetBSD 10.1 syscall `mq_close'", 0);

	return (ENOSYS);
}

int
nbsd101_mq_unlink(
    nix_us_syscall_if_t           *xus,
    nix_monitor_t                 *xmon,
    nbsd101_mq_unlink_args_t const *args,
    nbsd101_mq_unlink_result_t     *result)
{
	(void)xus;
	(void)xmon;
	(void)args;
	(void)result;

	LCLog(g_bsd_log, LCLogWarning, 0,
	      "unimplemented NetBSD 10.1 syscall `mq_unlink'", 0);

	return (ENOSYS);
}

int
nbsd101_mq_getattr(
    nix_us_syscall_if_t            *xus,
    nix_monitor_t                  *xmon,
    nbsd101_mq_getattr_args_t const *args,
    nbsd101_mq_getattr_result_t     *result)
{
	(void)xus;
	(void)xmon;
	(void)args;
	(void)result;

	LCLog(g_bsd_log, LCLogWarning, 0,
	      "unimplemented NetBSD 10.1 syscall `mq_getattr'", 0);

	return (ENOSYS);
}

int
nbsd101_mq_setattr(
    nix_us_syscall_if_t            *xus,
    nix_monitor_t                  *xmon,
    nbsd101_mq_setattr_args_t const *args,
    nbsd101_mq_setattr_result_t     *result)
{
	(void)xus;
	(void)xmon;
	(void)args;
	(void)result;

	LCLog(g_bsd_log, LCLogWarning, 0,
	      "unimplemented NetBSD 10.1 syscall `mq_setattr'", 0);

	return (ENOSYS);
}

int
nbsd101_mq_notify(
    nix_us_syscall_if_t           *xus,
    nix_monitor_t                 *xmon,
    nbsd101_mq_notify_args_t const *args,
    nbsd101_mq_notify_result_t     *result)
{
	(void)xus;
	(void)xmon;
	(void)args;
	(void)result;

	LCLog(g_bsd_log, LCLogWarning, 0,
	      "unimplemented NetBSD 10.1 syscall `mq_notify'", 0);

	return (ENOSYS);
}

int
nbsd101_mq_send(
    nix_us_syscall_if_t         *xus,
    nix_monitor_t               *xmon,
    nbsd101_mq_send_args_t const *args,
    nbsd101_mq_send_result_t     *result)
{
	(void)xus;
	(void)xmon;
	(void)args;
	(void)result;

	LCLog(g_bsd_log, LCLogWarning, 0,
	      "unimplemented NetBSD 10.1 syscall `mq_send'", 0);

	return (ENOSYS);
}

int
nbsd101_mq_receive(
    nix_us_syscall_if_t            *xus,
    nix_monitor_t                  *xmon,
    nbsd101_mq_receive_args_t const *args,
    nbsd101_mq_receive_result_t     *result)
{
	(void)xus;
	(void)xmon;
	(void)args;
	(void)result;

	LCLog(g_bsd_log, LCLogWarning, 0,
	      "unimplemented NetBSD 10.1 syscall `mq_receive'", 0);

	return (ENOSYS);
}

int
nbsd101_eventfd(
    nix_us_syscall_if_t         *xus,
    nix_monitor_t               *xmon,
    nbsd101_eventfd_args_t const *args,
    nbsd101_eventfd_result_t     *result)
{
	(void)xus;
	(void)xmon;
	(void)args;
	(void)result;

	LCLog(g_bsd_log, LCLogWarning, 0,
	      "unimplemented NetBSD 10.1 syscall `eventfd'", 0);

	return (ENOSYS);
}

int
nbsd101_lchmod(
    nix_us_syscall_if_t        *xus,
    nix_monitor_t              *xmon,
    nbsd101_lchmod_args_t const *args,
    nbsd101_lchmod_result_t     *result)
{
	(void)xus;
	(void)xmon;
	(void)args;
	(void)result;

	LCLog(g_bsd_log, LCLogWarning, 0,
	      "unimplemented NetBSD 10.1 syscall `lchmod'", 0);

	return (ENOSYS);
}

int
nbsd101___clone(
    nix_us_syscall_if_t         *xus,
    nix_monitor_t               *xmon,
    nbsd101___clone_args_t const *args,
    nbsd101___clone_result_t     *result)
{
	(void)xus;
	(void)xmon;
	(void)args;
	(void)result;

	LCLog(g_bsd_log, LCLogWarning, 0,
	      "unimplemented NetBSD 10.1 syscall `__clone'", 0);

	return (ENOSYS);
}

int
nbsd101_fktrace(
    nix_us_syscall_if_t         *xus,
    nix_monitor_t               *xmon,
    nbsd101_fktrace_args_t const *args,
    nbsd101_fktrace_result_t     *result)
{
	(void)xus;
	(void)xmon;
	(void)args;
	(void)result;

	LCLog(g_bsd_log, LCLogWarning, 0,
	      "unimplemented NetBSD 10.1 syscall `fktrace'", 0);

	return (ENOSYS);
}

int
nbsd101_fchroot(
    nix_us_syscall_if_t         *xus,
    nix_monitor_t               *xmon,
    nbsd101_fchroot_args_t const *args,
    nbsd101_fchroot_result_t     *result)
{
	(void)xus;
	(void)xmon;
	(void)args;
	(void)result;

	LCLog(g_bsd_log, LCLogWarning, 0,
	      "unimplemented NetBSD 10.1 syscall `fchroot'", 0);

	return (ENOSYS);
}

int
nbsd101_lchflags(
    nix_us_syscall_if_t          *xus,
    nix_monitor_t                *xmon,
    nbsd101_lchflags_args_t const *args,
    nbsd101_lchflags_result_t     *result)
{
	(void)xus;
	(void)xmon;
	(void)args;
	(void)result;

	LCLog(g_bsd_log, LCLogWarning, 0,
	      "unimplemented NetBSD 10.1 syscall `lchflags'", 0);

	return (ENOSYS);
}

int
nbsd101_utrace(
    nix_us_syscall_if_t        *xus,
    nix_monitor_t              *xmon,
    nbsd101_utrace_args_t const *args,
    nbsd101_utrace_result_t     *result)
{
	(void)xus;
	(void)xmon;
	(void)args;
	(void)result;

	LCLog(g_bsd_log, LCLogWarning, 0,
	      "unimplemented NetBSD 10.1 syscall `utrace'", 0);

	return (ENOSYS);
}

int
nbsd101_getcontext(
    nix_us_syscall_if_t            *xus,
    nix_monitor_t                  *xmon,
    nbsd101_getcontext_args_t const *args,
    nbsd101_getcontext_result_t     *result)
{
	(void)xus;
	(void)xmon;
	(void)args;
	(void)result;

	LCLog(g_bsd_log, LCLogWarning, 0,
	      "unimplemented NetBSD 10.1 syscall `getcontext'", 0);

	return (ENOSYS);
}

int
nbsd101_setcontext(
    nix_us_syscall_if_t            *xus,
    nix_monitor_t                  *xmon,
    nbsd101_setcontext_args_t const *args,
    nbsd101_setcontext_result_t     *result)
{
	(void)xus;
	(void)xmon;
	(void)args;
	(void)result;

	LCLog(g_bsd_log, LCLogWarning, 0,
	      "unimplemented NetBSD 10.1 syscall `setcontext'", 0);

	return (ENOSYS);
}

int
nbsd101__lwp_create(
    nix_us_syscall_if_t             *xus,
    nix_monitor_t                   *xmon,
    nbsd101__lwp_create_args_t const *args,
    nbsd101__lwp_create_result_t     *result)
{
	(void)xus;
	(void)xmon;
	(void)args;
	(void)result;

	LCLog(g_bsd_log, LCLogWarning, 0,
	      "unimplemented NetBSD 10.1 syscall `_lwp_create'", 0);

	return (ENOSYS);
}

int
nbsd101__lwp_exit(
    nix_us_syscall_if_t           *xus,
    nix_monitor_t                 *xmon,
    nbsd101__lwp_exit_args_t const *args,
    nbsd101__lwp_exit_result_t     *result)
{
	(void)xus;
	(void)xmon;
	(void)args;
	(void)result;

	LCLog(g_bsd_log, LCLogWarning, 0,
	      "unimplemented NetBSD 10.1 syscall `_lwp_exit'", 0);

	return (ENOSYS);
}

int
nbsd101__lwp_self(
    nix_us_syscall_if_t           *xus,
    nix_monitor_t                 *xmon,
    nbsd101__lwp_self_args_t const *args,
    nbsd101__lwp_self_result_t     *result)
{
	(void)xus;
	(void)xmon;
	(void)args;
	(void)result;

	LCLog(g_bsd_log, LCLogWarning, 0,
	      "unimplemented NetBSD 10.1 syscall `_lwp_self'", 0);

	return (ENOSYS);
}

int
nbsd101__lwp_wait(
    nix_us_syscall_if_t           *xus,
    nix_monitor_t                 *xmon,
    nbsd101__lwp_wait_args_t const *args,
    nbsd101__lwp_wait_result_t     *result)
{
	(void)xus;
	(void)xmon;
	(void)args;
	(void)result;

	LCLog(g_bsd_log, LCLogWarning, 0,
	      "unimplemented NetBSD 10.1 syscall `_lwp_wait'", 0);

	return (ENOSYS);
}

int
nbsd101__lwp_suspend(
    nix_us_syscall_if_t              *xus,
    nix_monitor_t                    *xmon,
    nbsd101__lwp_suspend_args_t const *args,
    nbsd101__lwp_suspend_result_t     *result)
{
	(void)xus;
	(void)xmon;
	(void)args;
	(void)result;

	LCLog(g_bsd_log, LCLogWarning, 0,
	      "unimplemented NetBSD 10.1 syscall `_lwp_suspend'", 0);

	return (ENOSYS);
}

int
nbsd101__lwp_continue(
    nix_us_syscall_if_t               *xus,
    nix_monitor_t                     *xmon,
    nbsd101__lwp_continue_args_t const *args,
    nbsd101__lwp_continue_result_t     *result)
{
	(void)xus;
	(void)xmon;
	(void)args;
	(void)result;

	LCLog(g_bsd_log, LCLogWarning, 0,
	      "unimplemented NetBSD 10.1 syscall `_lwp_continue'", 0);

	return (ENOSYS);
}

int
nbsd101__lwp_wakeup(
    nix_us_syscall_if_t             *xus,
    nix_monitor_t                   *xmon,
    nbsd101__lwp_wakeup_args_t const *args,
    nbsd101__lwp_wakeup_result_t     *result)
{
	(void)xus;
	(void)xmon;
	(void)args;
	(void)result;

	LCLog(g_bsd_log, LCLogWarning, 0,
	      "unimplemented NetBSD 10.1 syscall `_lwp_wakeup'", 0);

	return (ENOSYS);
}

int
nbsd101__lwp_getprivate(
    nix_us_syscall_if_t                 *xus,
    nix_monitor_t                       *xmon,
    nbsd101__lwp_getprivate_args_t const *args,
    nbsd101__lwp_getprivate_result_t     *result)
{
	(void)xus;
	(void)xmon;
	(void)args;
	(void)result;

	LCLog(g_bsd_log, LCLogWarning, 0,
	      "unimplemented NetBSD 10.1 syscall `_lwp_getprivate'", 0);

	return (ENOSYS);
}

int
nbsd101__lwp_setprivate(
    nix_us_syscall_if_t                 *xus,
    nix_monitor_t                       *xmon,
    nbsd101__lwp_setprivate_args_t const *args,
    nbsd101__lwp_setprivate_result_t     *result)
{
	(void)xus;
	(void)xmon;
	(void)args;
	(void)result;

	LCLog(g_bsd_log, LCLogWarning, 0,
	      "unimplemented NetBSD 10.1 syscall `_lwp_setprivate'", 0);

	return (ENOSYS);
}

int
nbsd101__lwp_kill(
    nix_us_syscall_if_t           *xus,
    nix_monitor_t                 *xmon,
    nbsd101__lwp_kill_args_t const *args,
    nbsd101__lwp_kill_result_t     *result)
{
	(void)xus;
	(void)xmon;
	(void)args;
	(void)result;

	LCLog(g_bsd_log, LCLogWarning, 0,
	      "unimplemented NetBSD 10.1 syscall `_lwp_kill'", 0);

	return (ENOSYS);
}

int
nbsd101__lwp_detach(
    nix_us_syscall_if_t             *xus,
    nix_monitor_t                   *xmon,
    nbsd101__lwp_detach_args_t const *args,
    nbsd101__lwp_detach_result_t     *result)
{
	(void)xus;
	(void)xmon;
	(void)args;
	(void)result;

	LCLog(g_bsd_log, LCLogWarning, 0,
	      "unimplemented NetBSD 10.1 syscall `_lwp_detach'", 0);

	return (ENOSYS);
}

int
nbsd101__lwp_unpark(
    nix_us_syscall_if_t             *xus,
    nix_monitor_t                   *xmon,
    nbsd101__lwp_unpark_args_t const *args,
    nbsd101__lwp_unpark_result_t     *result)
{
	(void)xus;
	(void)xmon;
	(void)args;
	(void)result;

	LCLog(g_bsd_log, LCLogWarning, 0,
	      "unimplemented NetBSD 10.1 syscall `_lwp_unpark'", 0);

	return (ENOSYS);
}

int
nbsd101__lwp_unpark_all(
    nix_us_syscall_if_t                 *xus,
    nix_monitor_t                       *xmon,
    nbsd101__lwp_unpark_all_args_t const *args,
    nbsd101__lwp_unpark_all_result_t     *result)
{
	(void)xus;
	(void)xmon;
	(void)args;
	(void)result;

	LCLog(g_bsd_log, LCLogWarning, 0,
	      "unimplemented NetBSD 10.1 syscall `_lwp_unpark_all'", 0);

	return (ENOSYS);
}

int
nbsd101__lwp_setname(
    nix_us_syscall_if_t              *xus,
    nix_monitor_t                    *xmon,
    nbsd101__lwp_setname_args_t const *args,
    nbsd101__lwp_setname_result_t     *result)
{
	(void)xus;
	(void)xmon;
	(void)args;
	(void)result;

	LCLog(g_bsd_log, LCLogWarning, 0,
	      "unimplemented NetBSD 10.1 syscall `_lwp_setname'", 0);

	return (ENOSYS);
}

int
nbsd101__lwp_getname(
    nix_us_syscall_if_t              *xus,
    nix_monitor_t                    *xmon,
    nbsd101__lwp_getname_args_t const *args,
    nbsd101__lwp_getname_result_t     *result)
{
	(void)xus;
	(void)xmon;
	(void)args;
	(void)result;

	LCLog(g_bsd_log, LCLogWarning, 0,
	      "unimplemented NetBSD 10.1 syscall `_lwp_getname'", 0);

	return (ENOSYS);
}

int
nbsd101__lwp_ctl(
    nix_us_syscall_if_t          *xus,
    nix_monitor_t                *xmon,
    nbsd101__lwp_ctl_args_t const *args,
    nbsd101__lwp_ctl_result_t     *result)
{
	(void)xus;
	(void)xmon;
	(void)args;
	(void)result;

	LCLog(g_bsd_log, LCLogWarning, 0,
	      "unimplemented NetBSD 10.1 syscall `_lwp_ctl'", 0);

	return (ENOSYS);
}

int
nbsd101___sigaction_sigtramp(
    nix_us_syscall_if_t                      *xus,
    nix_monitor_t                            *xmon,
    nbsd101___sigaction_sigtramp_args_t const *args,
    nbsd101___sigaction_sigtramp_result_t     *result)
{
	(void)xus;
	(void)xmon;
	(void)args;
	(void)result;

	LCLog(g_bsd_log, LCLogWarning, 0,
	      "unimplemented NetBSD 10.1 syscall `__sigaction_sigtramp'", 0);

	return (ENOSYS);
}

int
nbsd101_rasctl(
    nix_us_syscall_if_t        *xus,
    nix_monitor_t              *xmon,
    nbsd101_rasctl_args_t const *args,
    nbsd101_rasctl_result_t     *result)
{
	(void)xus;
	(void)xmon;
	(void)args;
	(void)result;

	LCLog(g_bsd_log, LCLogWarning, 0,
	      "unimplemented NetBSD 10.1 syscall `rasctl'", 0);

	return (ENOSYS);
}

int
nbsd101__sched_setparam(
    nix_us_syscall_if_t                 *xus,
    nix_monitor_t                       *xmon,
    nbsd101__sched_setparam_args_t const *args,
    nbsd101__sched_setparam_result_t     *result)
{
	(void)xus;
	(void)xmon;
	(void)args;
	(void)result;

	LCLog(g_bsd_log, LCLogWarning, 0,
	      "unimplemented NetBSD 10.1 syscall `_sched_setparam'", 0);

	return (ENOSYS);
}

int
nbsd101__sched_getparam(
    nix_us_syscall_if_t                 *xus,
    nix_monitor_t                       *xmon,
    nbsd101__sched_getparam_args_t const *args,
    nbsd101__sched_getparam_result_t     *result)
{
	(void)xus;
	(void)xmon;
	(void)args;
	(void)result;

	LCLog(g_bsd_log, LCLogWarning, 0,
	      "unimplemented NetBSD 10.1 syscall `_sched_getparam'", 0);

	return (ENOSYS);
}

int
nbsd101__sched_setaffinity(
    nix_us_syscall_if_t                    *xus,
    nix_monitor_t                          *xmon,
    nbsd101__sched_setaffinity_args_t const *args,
    nbsd101__sched_setaffinity_result_t     *result)
{
	(void)xus;
	(void)xmon;
	(void)args;
	(void)result;

	LCLog(g_bsd_log, LCLogWarning, 0,
	      "unimplemented NetBSD 10.1 syscall `_sched_setaffinity'", 0);

	return (ENOSYS);
}

int
nbsd101__sched_getaffinity(
    nix_us_syscall_if_t                    *xus,
    nix_monitor_t                          *xmon,
    nbsd101__sched_getaffinity_args_t const *args,
    nbsd101__sched_getaffinity_result_t     *result)
{
	(void)xus;
	(void)xmon;
	(void)args;
	(void)result;

	LCLog(g_bsd_log, LCLogWarning, 0,
	      "unimplemented NetBSD 10.1 syscall `_sched_getaffinity'", 0);

	return (ENOSYS);
}

int
nbsd101__sched_protect(
    nix_us_syscall_if_t                *xus,
    nix_monitor_t                      *xmon,
    nbsd101__sched_protect_args_t const *args,
    nbsd101__sched_protect_result_t     *result)
{
	(void)xus;
	(void)xmon;
	(void)args;
	(void)result;

	LCLog(g_bsd_log, LCLogWarning, 0,
	      "unimplemented NetBSD 10.1 syscall `_sched_protect'", 0);

	return (ENOSYS);
}

int
nbsd101_fsync_range(
    nix_us_syscall_if_t             *xus,
    nix_monitor_t                   *xmon,
    nbsd101_fsync_range_args_t const *args,
    nbsd101_fsync_range_result_t     *result)
{
	(void)xus;
	(void)xmon;
	(void)args;
	(void)result;

	LCLog(g_bsd_log, LCLogWarning, 0,
	      "unimplemented NetBSD 10.1 syscall `fsync_range'", 0);

	return (ENOSYS);
}

int
nbsd101_uuidgen(
    nix_us_syscall_if_t         *xus,
    nix_monitor_t               *xmon,
    nbsd101_uuidgen_args_t const *args,
    nbsd101_uuidgen_result_t     *result)
{
	(void)xus;
	(void)xmon;
	(void)args;
	(void)result;

	LCLog(g_bsd_log, LCLogWarning, 0,
	      "unimplemented NetBSD 10.1 syscall `uuidgen'", 0);

	return (ENOSYS);
}

int
nbsd101_extattrctl(
    nix_us_syscall_if_t            *xus,
    nix_monitor_t                  *xmon,
    nbsd101_extattrctl_args_t const *args,
    nbsd101_extattrctl_result_t     *result)
{
	(void)xus;
	(void)xmon;
	(void)args;
	(void)result;

	LCLog(g_bsd_log, LCLogWarning, 0,
	      "unimplemented NetBSD 10.1 syscall `extattrctl'", 0);

	return (ENOSYS);
}

int
nbsd101_extattr_set_file(
    nix_us_syscall_if_t                  *xus,
    nix_monitor_t                        *xmon,
    nbsd101_extattr_set_file_args_t const *args,
    nbsd101_extattr_set_file_result_t     *result)
{
	(void)xus;
	(void)xmon;
	(void)args;
	(void)result;

	LCLog(g_bsd_log, LCLogWarning, 0,
	      "unimplemented NetBSD 10.1 syscall `extattr_set_file'", 0);

	return (ENOSYS);
}

int
nbsd101_extattr_get_file(
    nix_us_syscall_if_t                  *xus,
    nix_monitor_t                        *xmon,
    nbsd101_extattr_get_file_args_t const *args,
    nbsd101_extattr_get_file_result_t     *result)
{
	(void)xus;
	(void)xmon;
	(void)args;
	(void)result;

	LCLog(g_bsd_log, LCLogWarning, 0,
	      "unimplemented NetBSD 10.1 syscall `extattr_get_file'", 0);

	return (ENOSYS);
}

int
nbsd101_extattr_delete_file(
    nix_us_syscall_if_t                     *xus,
    nix_monitor_t                           *xmon,
    nbsd101_extattr_delete_file_args_t const *args,
    nbsd101_extattr_delete_file_result_t     *result)
{
	(void)xus;
	(void)xmon;
	(void)args;
	(void)result;

	LCLog(g_bsd_log, LCLogWarning, 0,
	      "unimplemented NetBSD 10.1 syscall `extattr_delete_file'", 0);

	return (ENOSYS);
}

int
nbsd101_extattr_set_fd(
    nix_us_syscall_if_t                *xus,
    nix_monitor_t                      *xmon,
    nbsd101_extattr_set_fd_args_t const *args,
    nbsd101_extattr_set_fd_result_t     *result)
{
	(void)xus;
	(void)xmon;
	(void)args;
	(void)result;

	LCLog(g_bsd_log, LCLogWarning, 0,
	      "unimplemented NetBSD 10.1 syscall `extattr_set_fd'", 0);

	return (ENOSYS);
}

int
nbsd101_extattr_get_fd(
    nix_us_syscall_if_t                *xus,
    nix_monitor_t                      *xmon,
    nbsd101_extattr_get_fd_args_t const *args,
    nbsd101_extattr_get_fd_result_t     *result)
{
	(void)xus;
	(void)xmon;
	(void)args;
	(void)result;

	LCLog(g_bsd_log, LCLogWarning, 0,
	      "unimplemented NetBSD 10.1 syscall `extattr_get_fd'", 0);

	return (ENOSYS);
}

int
nbsd101_extattr_delete_fd(
    nix_us_syscall_if_t                   *xus,
    nix_monitor_t                         *xmon,
    nbsd101_extattr_delete_fd_args_t const *args,
    nbsd101_extattr_delete_fd_result_t     *result)
{
	(void)xus;
	(void)xmon;
	(void)args;
	(void)result;

	LCLog(g_bsd_log, LCLogWarning, 0,
	      "unimplemented NetBSD 10.1 syscall `extattr_delete_fd'", 0);

	return (ENOSYS);
}

int
nbsd101_extattr_set_link(
    nix_us_syscall_if_t                  *xus,
    nix_monitor_t                        *xmon,
    nbsd101_extattr_set_link_args_t const *args,
    nbsd101_extattr_set_link_result_t     *result)
{
	(void)xus;
	(void)xmon;
	(void)args;
	(void)result;

	LCLog(g_bsd_log, LCLogWarning, 0,
	      "unimplemented NetBSD 10.1 syscall `extattr_set_link'", 0);

	return (ENOSYS);
}

int
nbsd101_extattr_get_link(
    nix_us_syscall_if_t                  *xus,
    nix_monitor_t                        *xmon,
    nbsd101_extattr_get_link_args_t const *args,
    nbsd101_extattr_get_link_result_t     *result)
{
	(void)xus;
	(void)xmon;
	(void)args;
	(void)result;

	LCLog(g_bsd_log, LCLogWarning, 0,
	      "unimplemented NetBSD 10.1 syscall `extattr_get_link'", 0);

	return (ENOSYS);
}

int
nbsd101_extattr_delete_link(
    nix_us_syscall_if_t                     *xus,
    nix_monitor_t                           *xmon,
    nbsd101_extattr_delete_link_args_t const *args,
    nbsd101_extattr_delete_link_result_t     *result)
{
	(void)xus;
	(void)xmon;
	(void)args;
	(void)result;

	LCLog(g_bsd_log, LCLogWarning, 0,
	      "unimplemented NetBSD 10.1 syscall `extattr_delete_link'", 0);

	return (ENOSYS);
}

int
nbsd101_extattr_list_fd(
    nix_us_syscall_if_t                 *xus,
    nix_monitor_t                       *xmon,
    nbsd101_extattr_list_fd_args_t const *args,
    nbsd101_extattr_list_fd_result_t     *result)
{
	(void)xus;
	(void)xmon;
	(void)args;
	(void)result;

	LCLog(g_bsd_log, LCLogWarning, 0,
	      "unimplemented NetBSD 10.1 syscall `extattr_list_fd'", 0);

	return (ENOSYS);
}

int
nbsd101_extattr_list_file(
    nix_us_syscall_if_t                   *xus,
    nix_monitor_t                         *xmon,
    nbsd101_extattr_list_file_args_t const *args,
    nbsd101_extattr_list_file_result_t     *result)
{
	(void)xus;
	(void)xmon;
	(void)args;
	(void)result;

	LCLog(g_bsd_log, LCLogWarning, 0,
	      "unimplemented NetBSD 10.1 syscall `extattr_list_file'", 0);

	return (ENOSYS);
}

int
nbsd101_extattr_list_link(
    nix_us_syscall_if_t                   *xus,
    nix_monitor_t                         *xmon,
    nbsd101_extattr_list_link_args_t const *args,
    nbsd101_extattr_list_link_result_t     *result)
{
	(void)xus;
	(void)xmon;
	(void)args;
	(void)result;

	LCLog(g_bsd_log, LCLogWarning, 0,
	      "unimplemented NetBSD 10.1 syscall `extattr_list_link'", 0);

	return (ENOSYS);
}

int
nbsd101_setxattr(
    nix_us_syscall_if_t          *xus,
    nix_monitor_t                *xmon,
    nbsd101_setxattr_args_t const *args,
    nbsd101_setxattr_result_t     *result)
{
	(void)xus;
	(void)xmon;
	(void)args;
	(void)result;

	LCLog(g_bsd_log, LCLogWarning, 0,
	      "unimplemented NetBSD 10.1 syscall `setxattr'", 0);

	return (ENOSYS);
}

int
nbsd101_lsetxattr(
    nix_us_syscall_if_t           *xus,
    nix_monitor_t                 *xmon,
    nbsd101_lsetxattr_args_t const *args,
    nbsd101_lsetxattr_result_t     *result)
{
	(void)xus;
	(void)xmon;
	(void)args;
	(void)result;

	LCLog(g_bsd_log, LCLogWarning, 0,
	      "unimplemented NetBSD 10.1 syscall `lsetxattr'", 0);

	return (ENOSYS);
}

int
nbsd101_fsetxattr(
    nix_us_syscall_if_t           *xus,
    nix_monitor_t                 *xmon,
    nbsd101_fsetxattr_args_t const *args,
    nbsd101_fsetxattr_result_t     *result)
{
	(void)xus;
	(void)xmon;
	(void)args;
	(void)result;

	LCLog(g_bsd_log, LCLogWarning, 0,
	      "unimplemented NetBSD 10.1 syscall `fsetxattr'", 0);

	return (ENOSYS);
}

int
nbsd101_getxattr(
    nix_us_syscall_if_t          *xus,
    nix_monitor_t                *xmon,
    nbsd101_getxattr_args_t const *args,
    nbsd101_getxattr_result_t     *result)
{
	(void)xus;
	(void)xmon;
	(void)args;
	(void)result;

	LCLog(g_bsd_log, LCLogWarning, 0,
	      "unimplemented NetBSD 10.1 syscall `getxattr'", 0);

	return (ENOSYS);
}

int
nbsd101_lgetxattr(
    nix_us_syscall_if_t           *xus,
    nix_monitor_t                 *xmon,
    nbsd101_lgetxattr_args_t const *args,
    nbsd101_lgetxattr_result_t     *result)
{
	(void)xus;
	(void)xmon;
	(void)args;
	(void)result;

	LCLog(g_bsd_log, LCLogWarning, 0,
	      "unimplemented NetBSD 10.1 syscall `lgetxattr'", 0);

	return (ENOSYS);
}

int
nbsd101_fgetxattr(
    nix_us_syscall_if_t           *xus,
    nix_monitor_t                 *xmon,
    nbsd101_fgetxattr_args_t const *args,
    nbsd101_fgetxattr_result_t     *result)
{
	(void)xus;
	(void)xmon;
	(void)args;
	(void)result;

	LCLog(g_bsd_log, LCLogWarning, 0,
	      "unimplemented NetBSD 10.1 syscall `fgetxattr'", 0);

	return (ENOSYS);
}

int
nbsd101_listxattr(
    nix_us_syscall_if_t           *xus,
    nix_monitor_t                 *xmon,
    nbsd101_listxattr_args_t const *args,
    nbsd101_listxattr_result_t     *result)
{
	(void)xus;
	(void)xmon;
	(void)args;
	(void)result;

	LCLog(g_bsd_log, LCLogWarning, 0,
	      "unimplemented NetBSD 10.1 syscall `listxattr'", 0);

	return (ENOSYS);
}

int
nbsd101_llistxattr(
    nix_us_syscall_if_t            *xus,
    nix_monitor_t                  *xmon,
    nbsd101_llistxattr_args_t const *args,
    nbsd101_llistxattr_result_t     *result)
{
	(void)xus;
	(void)xmon;
	(void)args;
	(void)result;

	LCLog(g_bsd_log, LCLogWarning, 0,
	      "unimplemented NetBSD 10.1 syscall `llistxattr'", 0);

	return (ENOSYS);
}

int
nbsd101_flistxattr(
    nix_us_syscall_if_t            *xus,
    nix_monitor_t                  *xmon,
    nbsd101_flistxattr_args_t const *args,
    nbsd101_flistxattr_result_t     *result)
{
	(void)xus;
	(void)xmon;
	(void)args;
	(void)result;

	LCLog(g_bsd_log, LCLogWarning, 0,
	      "unimplemented NetBSD 10.1 syscall `flistxattr'", 0);

	return (ENOSYS);
}

int
nbsd101_removexattr(
    nix_us_syscall_if_t             *xus,
    nix_monitor_t                   *xmon,
    nbsd101_removexattr_args_t const *args,
    nbsd101_removexattr_result_t     *result)
{
	(void)xus;
	(void)xmon;
	(void)args;
	(void)result;

	LCLog(g_bsd_log, LCLogWarning, 0,
	      "unimplemented NetBSD 10.1 syscall `removexattr'", 0);

	return (ENOSYS);
}

int
nbsd101_lremovexattr(
    nix_us_syscall_if_t              *xus,
    nix_monitor_t                    *xmon,
    nbsd101_lremovexattr_args_t const *args,
    nbsd101_lremovexattr_result_t     *result)
{
	(void)xus;
	(void)xmon;
	(void)args;
	(void)result;

	LCLog(g_bsd_log, LCLogWarning, 0,
	      "unimplemented NetBSD 10.1 syscall `lremovexattr'", 0);

	return (ENOSYS);
}

int
nbsd101_fremovexattr(
    nix_us_syscall_if_t              *xus,
    nix_monitor_t                    *xmon,
    nbsd101_fremovexattr_args_t const *args,
    nbsd101_fremovexattr_result_t     *result)
{
	(void)xus;
	(void)xmon;
	(void)args;
	(void)result;

	LCLog(g_bsd_log, LCLogWarning, 0,
	      "unimplemented NetBSD 10.1 syscall `fremovexattr'", 0);

	return (ENOSYS);
}

int
nbsd101_aio_cancel(
    nix_us_syscall_if_t            *xus,
    nix_monitor_t                  *xmon,
    nbsd101_aio_cancel_args_t const *args,
    nbsd101_aio_cancel_result_t     *result)
{
	(void)xus;
	(void)xmon;
	(void)args;
	(void)result;

	LCLog(g_bsd_log, LCLogWarning, 0,
	      "unimplemented NetBSD 10.1 syscall `aio_cancel'", 0);

	return (ENOSYS);
}

int
nbsd101_aio_error(
    nix_us_syscall_if_t           *xus,
    nix_monitor_t                 *xmon,
    nbsd101_aio_error_args_t const *args,
    nbsd101_aio_error_result_t     *result)
{
	(void)xus;
	(void)xmon;
	(void)args;
	(void)result;

	LCLog(g_bsd_log, LCLogWarning, 0,
	      "unimplemented NetBSD 10.1 syscall `aio_error'", 0);

	return (ENOSYS);
}

int
nbsd101_aio_fsync(
    nix_us_syscall_if_t           *xus,
    nix_monitor_t                 *xmon,
    nbsd101_aio_fsync_args_t const *args,
    nbsd101_aio_fsync_result_t     *result)
{
	(void)xus;
	(void)xmon;
	(void)args;
	(void)result;

	LCLog(g_bsd_log, LCLogWarning, 0,
	      "unimplemented NetBSD 10.1 syscall `aio_fsync'", 0);

	return (ENOSYS);
}

int
nbsd101_aio_read(
    nix_us_syscall_if_t          *xus,
    nix_monitor_t                *xmon,
    nbsd101_aio_read_args_t const *args,
    nbsd101_aio_read_result_t     *result)
{
	(void)xus;
	(void)xmon;
	(void)args;
	(void)result;

	LCLog(g_bsd_log, LCLogWarning, 0,
	      "unimplemented NetBSD 10.1 syscall `aio_read'", 0);

	return (ENOSYS);
}

int
nbsd101_aio_return(
    nix_us_syscall_if_t            *xus,
    nix_monitor_t                  *xmon,
    nbsd101_aio_return_args_t const *args,
    nbsd101_aio_return_result_t     *result)
{
	(void)xus;
	(void)xmon;
	(void)args;
	(void)result;

	LCLog(g_bsd_log, LCLogWarning, 0,
	      "unimplemented NetBSD 10.1 syscall `aio_return'", 0);

	return (ENOSYS);
}

int
nbsd101_aio_write(
    nix_us_syscall_if_t           *xus,
    nix_monitor_t                 *xmon,
    nbsd101_aio_write_args_t const *args,
    nbsd101_aio_write_result_t     *result)
{
	(void)xus;
	(void)xmon;
	(void)args;
	(void)result;

	LCLog(g_bsd_log, LCLogWarning, 0,
	      "unimplemented NetBSD 10.1 syscall `aio_write'", 0);

	return (ENOSYS);
}

int
nbsd101_lio_listio(
    nix_us_syscall_if_t            *xus,
    nix_monitor_t                  *xmon,
    nbsd101_lio_listio_args_t const *args,
    nbsd101_lio_listio_result_t     *result)
{
	(void)xus;
	(void)xmon;
	(void)args;
	(void)result;

	LCLog(g_bsd_log, LCLogWarning, 0,
	      "unimplemented NetBSD 10.1 syscall `lio_listio'", 0);

	return (ENOSYS);
}

int
nbsd101_mremap(
    nix_us_syscall_if_t        *xus,
    nix_monitor_t              *xmon,
    nbsd101_mremap_args_t const *args,
    nbsd101_mremap_result_t     *result)
{
	(void)xus;
	(void)xmon;
	(void)args;
	(void)result;

	LCLog(g_bsd_log, LCLogWarning, 0,
	      "unimplemented NetBSD 10.1 syscall `mremap'", 0);

	return (ENOSYS);
}

int
nbsd101_pset_create(
    nix_us_syscall_if_t             *xus,
    nix_monitor_t                   *xmon,
    nbsd101_pset_create_args_t const *args,
    nbsd101_pset_create_result_t     *result)
{
	(void)xus;
	(void)xmon;
	(void)args;
	(void)result;

	LCLog(g_bsd_log, LCLogWarning, 0,
	      "unimplemented NetBSD 10.1 syscall `pset_create'", 0);

	return (ENOSYS);
}

int
nbsd101_pset_destroy(
    nix_us_syscall_if_t              *xus,
    nix_monitor_t                    *xmon,
    nbsd101_pset_destroy_args_t const *args,
    nbsd101_pset_destroy_result_t     *result)
{
	(void)xus;
	(void)xmon;
	(void)args;
	(void)result;

	LCLog(g_bsd_log, LCLogWarning, 0,
	      "unimplemented NetBSD 10.1 syscall `pset_destroy'", 0);

	return (ENOSYS);
}

int
nbsd101_pset_assign(
    nix_us_syscall_if_t             *xus,
    nix_monitor_t                   *xmon,
    nbsd101_pset_assign_args_t const *args,
    nbsd101_pset_assign_result_t     *result)
{
	(void)xus;
	(void)xmon;
	(void)args;
	(void)result;

	LCLog(g_bsd_log, LCLogWarning, 0,
	      "unimplemented NetBSD 10.1 syscall `pset_assign'", 0);

	return (ENOSYS);
}

int
nbsd101__pset_bind(
    nix_us_syscall_if_t            *xus,
    nix_monitor_t                  *xmon,
    nbsd101__pset_bind_args_t const *args,
    nbsd101__pset_bind_result_t     *result)
{
	(void)xus;
	(void)xmon;
	(void)args;
	(void)result;

	LCLog(g_bsd_log, LCLogWarning, 0,
	      "unimplemented NetBSD 10.1 syscall `_pset_bind'", 0);

	return (ENOSYS);
}

int
nbsd101_pipe2(
    nix_us_syscall_if_t       *xus,
    nix_monitor_t             *xmon,
    nbsd101_pipe2_args_t const *args,
    nbsd101_pipe2_result_t     *result)
{
	(void)xus;
	(void)xmon;
	(void)args;
	(void)result;

	LCLog(g_bsd_log, LCLogWarning, 0,
	      "unimplemented NetBSD 10.1 syscall `pipe2'", 0);

	return (ENOSYS);
}

int
nbsd101_dup3(
    nix_us_syscall_if_t      *xus,
    nix_monitor_t            *xmon,
    nbsd101_dup3_args_t const *args,
    nbsd101_dup3_result_t     *result)
{
	(void)xus;
	(void)xmon;
	(void)args;
	(void)result;

	LCLog(g_bsd_log, LCLogWarning, 0,
	      "unimplemented NetBSD 10.1 syscall `dup3'", 0);

	return (ENOSYS);
}

int
nbsd101_kqueue1(
    nix_us_syscall_if_t         *xus,
    nix_monitor_t               *xmon,
    nbsd101_kqueue1_args_t const *args,
    nbsd101_kqueue1_result_t     *result)
{
	(void)xus;
	(void)xmon;
	(void)args;
	(void)result;

	LCLog(g_bsd_log, LCLogWarning, 0,
	      "unimplemented NetBSD 10.1 syscall `kqueue1'", 0);

	return (ENOSYS);
}

int
nbsd101_paccept(
    nix_us_syscall_if_t         *xus,
    nix_monitor_t               *xmon,
    nbsd101_paccept_args_t const *args,
    nbsd101_paccept_result_t     *result)
{
	(void)xus;
	(void)xmon;
	(void)args;
	(void)result;

	LCLog(g_bsd_log, LCLogWarning, 0,
	      "unimplemented NetBSD 10.1 syscall `paccept'", 0);

	return (ENOSYS);
}

int
nbsd101_linkat(
    nix_us_syscall_if_t        *xus,
    nix_monitor_t              *xmon,
    nbsd101_linkat_args_t const *args,
    nbsd101_linkat_result_t     *result)
{
	(void)xus;
	(void)xmon;
	(void)args;
	(void)result;

	LCLog(g_bsd_log, LCLogWarning, 0,
	      "unimplemented NetBSD 10.1 syscall `linkat'", 0);

	return (ENOSYS);
}

int
nbsd101_renameat(
    nix_us_syscall_if_t          *xus,
    nix_monitor_t                *xmon,
    nbsd101_renameat_args_t const *args,
    nbsd101_renameat_result_t     *result)
{
	(void)xus;
	(void)xmon;
	(void)args;
	(void)result;

	LCLog(g_bsd_log, LCLogWarning, 0,
	      "unimplemented NetBSD 10.1 syscall `renameat'", 0);

	return (ENOSYS);
}

int
nbsd101_mkfifoat(
    nix_us_syscall_if_t          *xus,
    nix_monitor_t                *xmon,
    nbsd101_mkfifoat_args_t const *args,
    nbsd101_mkfifoat_result_t     *result)
{
	(void)xus;
	(void)xmon;
	(void)args;
	(void)result;

	LCLog(g_bsd_log, LCLogWarning, 0,
	      "unimplemented NetBSD 10.1 syscall `mkfifoat'", 0);

	return (ENOSYS);
}

int
nbsd101_mknodat(
    nix_us_syscall_if_t         *xus,
    nix_monitor_t               *xmon,
    nbsd101_mknodat_args_t const *args,
    nbsd101_mknodat_result_t     *result)
{
	(void)xus;
	(void)xmon;
	(void)args;
	(void)result;

	LCLog(g_bsd_log, LCLogWarning, 0,
	      "unimplemented NetBSD 10.1 syscall `mknodat'", 0);

	return (ENOSYS);
}

int
nbsd101_mkdirat(
    nix_us_syscall_if_t         *xus,
    nix_monitor_t               *xmon,
    nbsd101_mkdirat_args_t const *args,
    nbsd101_mkdirat_result_t     *result)
{
	(void)xus;
	(void)xmon;
	(void)args;
	(void)result;

	LCLog(g_bsd_log, LCLogWarning, 0,
	      "unimplemented NetBSD 10.1 syscall `mkdirat'", 0);

	return (ENOSYS);
}

int
nbsd101_faccessat(
    nix_us_syscall_if_t           *xus,
    nix_monitor_t                 *xmon,
    nbsd101_faccessat_args_t const *args,
    nbsd101_faccessat_result_t     *result)
{
	(void)xus;
	(void)xmon;
	(void)args;
	(void)result;

	LCLog(g_bsd_log, LCLogWarning, 0,
	      "unimplemented NetBSD 10.1 syscall `faccessat'", 0);

	return (ENOSYS);
}

int
nbsd101_fchmodat(
    nix_us_syscall_if_t          *xus,
    nix_monitor_t                *xmon,
    nbsd101_fchmodat_args_t const *args,
    nbsd101_fchmodat_result_t     *result)
{
	(void)xus;
	(void)xmon;
	(void)args;
	(void)result;

	LCLog(g_bsd_log, LCLogWarning, 0,
	      "unimplemented NetBSD 10.1 syscall `fchmodat'", 0);

	return (ENOSYS);
}

int
nbsd101_fchownat(
    nix_us_syscall_if_t          *xus,
    nix_monitor_t                *xmon,
    nbsd101_fchownat_args_t const *args,
    nbsd101_fchownat_result_t     *result)
{
	(void)xus;
	(void)xmon;
	(void)args;
	(void)result;

	LCLog(g_bsd_log, LCLogWarning, 0,
	      "unimplemented NetBSD 10.1 syscall `fchownat'", 0);

	return (ENOSYS);
}

int
nbsd101_fexecve(
    nix_us_syscall_if_t         *xus,
    nix_monitor_t               *xmon,
    nbsd101_fexecve_args_t const *args,
    nbsd101_fexecve_result_t     *result)
{
	(void)xus;
	(void)xmon;
	(void)args;
	(void)result;

	LCLog(g_bsd_log, LCLogWarning, 0,
	      "unimplemented NetBSD 10.1 syscall `fexecve'", 0);

	return (ENOSYS);
}

int
nbsd101_fstatat(
    nix_us_syscall_if_t         *xus,
    nix_monitor_t               *xmon,
    nbsd101_fstatat_args_t const *args,
    nbsd101_fstatat_result_t     *result)
{
	(void)xus;
	(void)xmon;
	(void)args;
	(void)result;

	LCLog(g_bsd_log, LCLogWarning, 0,
	      "unimplemented NetBSD 10.1 syscall `fstatat'", 0);

	return (ENOSYS);
}

int
nbsd101_utimensat(
    nix_us_syscall_if_t           *xus,
    nix_monitor_t                 *xmon,
    nbsd101_utimensat_args_t const *args,
    nbsd101_utimensat_result_t     *result)
{
	(void)xus;
	(void)xmon;
	(void)args;
	(void)result;

	LCLog(g_bsd_log, LCLogWarning, 0,
	      "unimplemented NetBSD 10.1 syscall `utimensat'", 0);

	return (ENOSYS);
}

int
nbsd101_openat(
    nix_us_syscall_if_t        *xus,
    nix_monitor_t              *xmon,
    nbsd101_openat_args_t const *args,
    nbsd101_openat_result_t     *result)
{
	(void)xus;
	(void)xmon;
	(void)args;
	(void)result;

	LCLog(g_bsd_log, LCLogWarning, 0,
	      "unimplemented NetBSD 10.1 syscall `openat'", 0);

	return (ENOSYS);
}

int
nbsd101_readlinkat(
    nix_us_syscall_if_t            *xus,
    nix_monitor_t                  *xmon,
    nbsd101_readlinkat_args_t const *args,
    nbsd101_readlinkat_result_t     *result)
{
	(void)xus;
	(void)xmon;
	(void)args;
	(void)result;

	LCLog(g_bsd_log, LCLogWarning, 0,
	      "unimplemented NetBSD 10.1 syscall `readlinkat'", 0);

	return (ENOSYS);
}

int
nbsd101_symlinkat(
    nix_us_syscall_if_t           *xus,
    nix_monitor_t                 *xmon,
    nbsd101_symlinkat_args_t const *args,
    nbsd101_symlinkat_result_t     *result)
{
	(void)xus;
	(void)xmon;
	(void)args;
	(void)result;

	LCLog(g_bsd_log, LCLogWarning, 0,
	      "unimplemented NetBSD 10.1 syscall `symlinkat'", 0);

	return (ENOSYS);
}

int
nbsd101_unlinkat(
    nix_us_syscall_if_t          *xus,
    nix_monitor_t                *xmon,
    nbsd101_unlinkat_args_t const *args,
    nbsd101_unlinkat_result_t     *result)
{
	(void)xus;
	(void)xmon;
	(void)args;
	(void)result;

	LCLog(g_bsd_log, LCLogWarning, 0,
	      "unimplemented NetBSD 10.1 syscall `unlinkat'", 0);

	return (ENOSYS);
}

int
nbsd101_futimens(
    nix_us_syscall_if_t          *xus,
    nix_monitor_t                *xmon,
    nbsd101_futimens_args_t const *args,
    nbsd101_futimens_result_t     *result)
{
	(void)xus;
	(void)xmon;
	(void)args;
	(void)result;

	LCLog(g_bsd_log, LCLogWarning, 0,
	      "unimplemented NetBSD 10.1 syscall `futimens'", 0);

	return (ENOSYS);
}

int
nbsd101___quotactl(
    nix_us_syscall_if_t            *xus,
    nix_monitor_t                  *xmon,
    nbsd101___quotactl_args_t const *args,
    nbsd101___quotactl_result_t     *result)
{
	(void)xus;
	(void)xmon;
	(void)args;
	(void)result;

	LCLog(g_bsd_log, LCLogWarning, 0,
	      "unimplemented NetBSD 10.1 syscall `__quotactl'", 0);

	return (ENOSYS);
}

int
nbsd101_recvmmsg(
    nix_us_syscall_if_t          *xus,
    nix_monitor_t                *xmon,
    nbsd101_recvmmsg_args_t const *args,
    nbsd101_recvmmsg_result_t     *result)
{
	(void)xus;
	(void)xmon;
	(void)args;
	(void)result;

	LCLog(g_bsd_log, LCLogWarning, 0,
	      "unimplemented NetBSD 10.1 syscall `recvmmsg'", 0);

	return (ENOSYS);
}

int
nbsd101_sendmmsg(
    nix_us_syscall_if_t          *xus,
    nix_monitor_t                *xmon,
    nbsd101_sendmmsg_args_t const *args,
    nbsd101_sendmmsg_result_t     *result)
{
	(void)xus;
	(void)xmon;
	(void)args;
	(void)result;

	LCLog(g_bsd_log, LCLogWarning, 0,
	      "unimplemented NetBSD 10.1 syscall `sendmmsg'", 0);

	return (ENOSYS);
}

int
nbsd101_fdiscard(
    nix_us_syscall_if_t          *xus,
    nix_monitor_t                *xmon,
    nbsd101_fdiscard_args_t const *args,
    nbsd101_fdiscard_result_t     *result)
{
	(void)xus;
	(void)xmon;
	(void)args;
	(void)result;

	LCLog(g_bsd_log, LCLogWarning, 0,
	      "unimplemented NetBSD 10.1 syscall `fdiscard'", 0);

	return (ENOSYS);
}

int
nbsd101_wait6(
    nix_us_syscall_if_t       *xus,
    nix_monitor_t             *xmon,
    nbsd101_wait6_args_t const *args,
    nbsd101_wait6_result_t     *result)
{
	(void)xus;
	(void)xmon;
	(void)args;
	(void)result;

	LCLog(g_bsd_log, LCLogWarning, 0,
	      "unimplemented NetBSD 10.1 syscall `wait6'", 0);

	return (ENOSYS);
}

int
nbsd101_clock_getcpuclockid2(
    nix_us_syscall_if_t                      *xus,
    nix_monitor_t                            *xmon,
    nbsd101_clock_getcpuclockid2_args_t const *args,
    nbsd101_clock_getcpuclockid2_result_t     *result)
{
	(void)xus;
	(void)xmon;
	(void)args;
	(void)result;

	LCLog(g_bsd_log, LCLogWarning, 0,
	      "unimplemented NetBSD 10.1 syscall `clock_getcpuclockid2'", 0);

	return (ENOSYS);
}

int
nbsd101___acl_get_link(
    nix_us_syscall_if_t                *xus,
    nix_monitor_t                      *xmon,
    nbsd101___acl_get_link_args_t const *args,
    nbsd101___acl_get_link_result_t     *result)
{
	(void)xus;
	(void)xmon;
	(void)args;
	(void)result;

	LCLog(g_bsd_log, LCLogWarning, 0,
	      "unimplemented NetBSD 10.1 syscall `__acl_get_link'", 0);

	return (ENOSYS);
}

int
nbsd101___acl_set_link(
    nix_us_syscall_if_t                *xus,
    nix_monitor_t                      *xmon,
    nbsd101___acl_set_link_args_t const *args,
    nbsd101___acl_set_link_result_t     *result)
{
	(void)xus;
	(void)xmon;
	(void)args;
	(void)result;

	LCLog(g_bsd_log, LCLogWarning, 0,
	      "unimplemented NetBSD 10.1 syscall `__acl_set_link'", 0);

	return (ENOSYS);
}

int
nbsd101___acl_delete_link(
    nix_us_syscall_if_t                   *xus,
    nix_monitor_t                         *xmon,
    nbsd101___acl_delete_link_args_t const *args,
    nbsd101___acl_delete_link_result_t     *result)
{
	(void)xus;
	(void)xmon;
	(void)args;
	(void)result;

	LCLog(g_bsd_log, LCLogWarning, 0,
	      "unimplemented NetBSD 10.1 syscall `__acl_delete_link'", 0);

	return (ENOSYS);
}

int
nbsd101___acl_aclcheck_link(
    nix_us_syscall_if_t                     *xus,
    nix_monitor_t                           *xmon,
    nbsd101___acl_aclcheck_link_args_t const *args,
    nbsd101___acl_aclcheck_link_result_t     *result)
{
	(void)xus;
	(void)xmon;
	(void)args;
	(void)result;

	LCLog(g_bsd_log, LCLogWarning, 0,
	      "unimplemented NetBSD 10.1 syscall `__acl_aclcheck_link'", 0);

	return (ENOSYS);
}

int
nbsd101___acl_get_file(
    nix_us_syscall_if_t                *xus,
    nix_monitor_t                      *xmon,
    nbsd101___acl_get_file_args_t const *args,
    nbsd101___acl_get_file_result_t     *result)
{
	(void)xus;
	(void)xmon;
	(void)args;
	(void)result;

	LCLog(g_bsd_log, LCLogWarning, 0,
	      "unimplemented NetBSD 10.1 syscall `__acl_get_file'", 0);

	return (ENOSYS);
}

int
nbsd101___acl_set_file(
    nix_us_syscall_if_t                *xus,
    nix_monitor_t                      *xmon,
    nbsd101___acl_set_file_args_t const *args,
    nbsd101___acl_set_file_result_t     *result)
{
	(void)xus;
	(void)xmon;
	(void)args;
	(void)result;

	LCLog(g_bsd_log, LCLogWarning, 0,
	      "unimplemented NetBSD 10.1 syscall `__acl_set_file'", 0);

	return (ENOSYS);
}

int
nbsd101___acl_get_fd(
    nix_us_syscall_if_t              *xus,
    nix_monitor_t                    *xmon,
    nbsd101___acl_get_fd_args_t const *args,
    nbsd101___acl_get_fd_result_t     *result)
{
	(void)xus;
	(void)xmon;
	(void)args;
	(void)result;

	LCLog(g_bsd_log, LCLogWarning, 0,
	      "unimplemented NetBSD 10.1 syscall `__acl_get_fd'", 0);

	return (ENOSYS);
}

int
nbsd101___acl_set_fd(
    nix_us_syscall_if_t              *xus,
    nix_monitor_t                    *xmon,
    nbsd101___acl_set_fd_args_t const *args,
    nbsd101___acl_set_fd_result_t     *result)
{
	(void)xus;
	(void)xmon;
	(void)args;
	(void)result;

	LCLog(g_bsd_log, LCLogWarning, 0,
	      "unimplemented NetBSD 10.1 syscall `__acl_set_fd'", 0);

	return (ENOSYS);
}

int
nbsd101___acl_delete_file(
    nix_us_syscall_if_t                   *xus,
    nix_monitor_t                         *xmon,
    nbsd101___acl_delete_file_args_t const *args,
    nbsd101___acl_delete_file_result_t     *result)
{
	(void)xus;
	(void)xmon;
	(void)args;
	(void)result;

	LCLog(g_bsd_log, LCLogWarning, 0,
	      "unimplemented NetBSD 10.1 syscall `__acl_delete_file'", 0);

	return (ENOSYS);
}

int
nbsd101___acl_delete_fd(
    nix_us_syscall_if_t                 *xus,
    nix_monitor_t                       *xmon,
    nbsd101___acl_delete_fd_args_t const *args,
    nbsd101___acl_delete_fd_result_t     *result)
{
	(void)xus;
	(void)xmon;
	(void)args;
	(void)result;

	LCLog(g_bsd_log, LCLogWarning, 0,
	      "unimplemented NetBSD 10.1 syscall `__acl_delete_fd'", 0);

	return (ENOSYS);
}

int
nbsd101___acl_aclcheck_file(
    nix_us_syscall_if_t                     *xus,
    nix_monitor_t                           *xmon,
    nbsd101___acl_aclcheck_file_args_t const *args,
    nbsd101___acl_aclcheck_file_result_t     *result)
{
	(void)xus;
	(void)xmon;
	(void)args;
	(void)result;

	LCLog(g_bsd_log, LCLogWarning, 0,
	      "unimplemented NetBSD 10.1 syscall `__acl_aclcheck_file'", 0);

	return (ENOSYS);
}

int
nbsd101___acl_aclcheck_fd(
    nix_us_syscall_if_t                   *xus,
    nix_monitor_t                         *xmon,
    nbsd101___acl_aclcheck_fd_args_t const *args,
    nbsd101___acl_aclcheck_fd_result_t     *result)
{
	(void)xus;
	(void)xmon;
	(void)args;
	(void)result;

	LCLog(g_bsd_log, LCLogWarning, 0,
	      "unimplemented NetBSD 10.1 syscall `__acl_aclcheck_fd'", 0);

	return (ENOSYS);
}

int
nbsd101_lpathconf(
    nix_us_syscall_if_t           *xus,
    nix_monitor_t                 *xmon,
    nbsd101_lpathconf_args_t const *args,
    nbsd101_lpathconf_result_t     *result)
{
	(void)xus;
	(void)xmon;
	(void)args;
	(void)result;

	LCLog(g_bsd_log, LCLogWarning, 0,
	      "unimplemented NetBSD 10.1 syscall `lpathconf'", 0);

	return (ENOSYS);
}
