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
#include "netbsd-syscalls.h"
#include "netbsd-us-syscall-priv.h"

#include "nix.h"
#include "nix-fd.h"
#include "netbsd.h"
#include "arc4random.h"

#include "netbsd-args.h"
#include "netbsd-sysctl.h"
#include "netbsd-mman.h"

#include "nix-host.h" /* XXX */

#define GE32(gi, x) \
	(((gi)->endian != NIX_ENDIAN_NATIVE) ? nix_byte_swap_int32(x) : (x))

#define GE64(gi, x) \
	(((gi)->endian != NIX_ENDIAN_NATIVE) ? nix_byte_swap_int64(x) : (x))

void *g_bsd_log = NULL;

static __inline struct nix_iovec *
__netbsd_iovec32_copy_from(nix_env_t              *env,
                           nix_mem_if_t           *mem,
                           nix_guest_info_t const *gi,
                           struct netbsd_iovec32  *iov,
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
__netbsd_pollfd_copy_from(nix_env_t              *env,
                          nix_guest_info_t const *gi,
                          struct netbsd_pollfd   *fds,
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
				netbsd_pollfd_to_nix_pollfd(gi->endian, fds + n, xfds + n);
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
netbsd_exit(
    nix_us_syscall_if_t      *xus,
    nix_monitor_t            *xmon,
    netbsd_exit_args_t const *args,
    netbsd_exit_result_t     *result)
{
	LCLog(g_bsd_log, LCLogDebug, 0, "invoked", 0);

	nix_exit(args->arg0);
	return (0); /* Not reached (should)! */
}

int
netbsd_fork(
    nix_us_syscall_if_t      *xus,
    nix_monitor_t            *xmon,
    netbsd_fork_args_t const *args,
    netbsd_fork_result_t     *result)
{
	nix_env_t *env = netbsd_us_syscall_get_nix_env(xus);

	LCLog(g_bsd_log, LCLogDebug, 0, "invoked", 0);

	nix_env_set_errno(env, 0);
	*result = nix_fork(env);
	return (nix_env_get_errno(env));
}

int
netbsd_read(
    nix_us_syscall_if_t      *xus,
    nix_monitor_t            *xmon,
    netbsd_read_args_t const *args,
    netbsd_read_result_t     *result)
{
	nix_env_t *env = netbsd_us_syscall_get_nix_env(xus);

	LCLog(g_bsd_log, LCLogDebug, 0, "invoked", 0);

	nix_env_set_errno(env, 0);
	*result = nix_read(args->arg0, args->arg1, args->arg2, env);
	return (nix_env_get_errno(env));
}

int
netbsd_write(
    nix_us_syscall_if_t       *xus,
    nix_monitor_t             *xmon,
    netbsd_write_args_t const *args,
    netbsd_write_result_t     *result)
{
	nix_env_t *env = netbsd_us_syscall_get_nix_env(xus);

	LCLog(g_bsd_log, LCLogDebug, 0, "invoked", 0);

	nix_env_set_errno(env, 0);
	*result = nix_write(args->arg0, args->arg1, args->arg2, env);
	return (nix_env_get_errno(env));
}

int
netbsd_open(
    nix_us_syscall_if_t      *xus,
    nix_monitor_t            *xmon,
    netbsd_open_args_t const *args,
    netbsd_open_result_t     *result)
{
	nix_env_t *env = netbsd_us_syscall_get_nix_env(xus);

	LCLog(g_bsd_log, LCLogDebug, 0, "invoked", 0);

	nix_env_set_errno(env, 0);
	*result = nix_open((char const *)args->arg0, args->arg1, args->arg2, env);
	return (nix_env_get_errno(env));
}

int
netbsd_close(
    nix_us_syscall_if_t       *xus,
    nix_monitor_t             *xmon,
    netbsd_close_args_t const *args,
    netbsd_close_result_t     *result)
{
	nix_env_t *env = netbsd_us_syscall_get_nix_env(xus);

	LCLog(g_bsd_log, LCLogDebug, 0, "invoked", 0);

	nix_env_set_errno(env, 0);
	*result = nix_close(args->arg0, env);
	return (nix_env_get_errno(env));
}

int
netbsd_link(
    nix_us_syscall_if_t      *xus,
    nix_monitor_t            *xmon,
    netbsd_link_args_t const *args,
    netbsd_link_result_t     *result)
{
	nix_env_t *env = netbsd_us_syscall_get_nix_env(xus);

	LCLog(g_bsd_log, LCLogDebug, 0, "invoked", 0);

	nix_env_set_errno(env, 0);
	*result = nix_link((char const *)args->arg0, (char const *)args->arg1, env);
	return (nix_env_get_errno(env));
}

int
netbsd_unlink(
    nix_us_syscall_if_t        *xus,
    nix_monitor_t              *xmon,
    netbsd_unlink_args_t const *args,
    netbsd_unlink_result_t     *result)
{
	nix_env_t *env = netbsd_us_syscall_get_nix_env(xus);

	LCLog(g_bsd_log, LCLogDebug, 0, "invoked", 0);

	nix_env_set_errno(env, 0);
	*result = nix_unlink((char const *)args->arg0, env);
	return (nix_env_get_errno(env));
}

int
netbsd_chdir(
    nix_us_syscall_if_t       *xus,
    nix_monitor_t             *xmon,
    netbsd_chdir_args_t const *args,
    netbsd_chdir_result_t     *result)
{
	nix_env_t *env = netbsd_us_syscall_get_nix_env(xus);

	LCLog(g_bsd_log, LCLogDebug, 0, "invoked", 0);

	nix_env_set_errno(env, 0);
	*result = nix_chdir((char const *)args->arg0, env);
	return (nix_env_get_errno(env));
}

int
netbsd_fchdir(
    nix_us_syscall_if_t        *xus,
    nix_monitor_t              *xmon,
    netbsd_fchdir_args_t const *args,
    netbsd_fchdir_result_t     *result)
{
	nix_env_t *env = netbsd_us_syscall_get_nix_env(xus);

	LCLog(g_bsd_log, LCLogDebug, 0, "invoked", 0);

	nix_env_set_errno(env, 0);
	*result = nix_fchdir(args->arg0, env);
	return (nix_env_get_errno(env));
}

int
netbsd_chmod(
    nix_us_syscall_if_t       *xus,
    nix_monitor_t             *xmon,
    netbsd_chmod_args_t const *args,
    netbsd_chmod_result_t     *result)
{
	nix_env_t *env = netbsd_us_syscall_get_nix_env(xus);

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
netbsd_chown(
    nix_us_syscall_if_t       *xus,
    nix_monitor_t             *xmon,
    netbsd_chown_args_t const *args,
    netbsd_chown_result_t     *result)
{
	nix_env_t *env = netbsd_us_syscall_get_nix_env(xus);

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
netbsd_setuid(
    nix_us_syscall_if_t        *xus,
    nix_monitor_t              *xmon,
    netbsd_setuid_args_t const *args,
    netbsd_setuid_result_t     *result)
{
	nix_env_t *env = netbsd_us_syscall_get_nix_env(xus);

	LCLog(g_bsd_log, LCLogDebug, 0, "invoked", 0);

	nix_env_set_errno(env, 0);
	*result = nix_setuid(args->arg0, env);
	return (nix_env_get_errno(env));
}

int
netbsd_ptrace(
    nix_us_syscall_if_t        *xus,
    nix_monitor_t              *xmon,
    netbsd_ptrace_args_t const *args,
    netbsd_ptrace_result_t     *result)
{
	nix_env_t *env = netbsd_us_syscall_get_nix_env(xus);

	LCLog(g_bsd_log, LCLogDebug, 0, "invoked", 0);

	nix_env_set_errno(env, 0);
	*result = nix_ptrace(args->arg0, args->arg1, args->arg2, args->arg3, env);
	return (nix_env_get_errno(env));
}

int
netbsd_recvmsg(
    nix_us_syscall_if_t         *xus,
    nix_monitor_t               *xmon,
    netbsd_recvmsg_args_t const *args,
    netbsd_recvmsg_result_t     *result)
{
	LCLog(g_bsd_log, LCLogDebug, 0, "invoked", 0);

	/* Insert here implementation code */

	return (ENOSYS);
}

int
netbsd_sendmsg(
    nix_us_syscall_if_t         *xus,
    nix_monitor_t               *xmon,
    netbsd_sendmsg_args_t const *args,
    netbsd_sendmsg_result_t     *result)
{
	LCLog(g_bsd_log, LCLogDebug, 0, "invoked", 0);

	/* Insert here implementation code */

	return (ENOSYS);
}

int
netbsd_recvfrom(
    nix_us_syscall_if_t          *xus,
    nix_monitor_t                *xmon,
    netbsd_recvfrom_args_t const *args,
    netbsd_recvfrom_result_t     *result)
{
	nix_guest_info_t     gi;
	struct nix_sockaddr  sa;
	int                  rc = -1;
	nix_socklen_t        salen = sizeof(sa);
	struct nix_sockaddr *psa = args->arg4 != NULL ? &sa : NULL;
	nix_socklen_t       *psalen = args->arg4 != NULL ? &salen : NULL;
	nix_env_t           *env = netbsd_us_syscall_get_nix_env(xus);

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
			if (!nix_sockaddr_to_netbsd_sockaddr(gi.endian, psa, *psalen,
			                                     args->arg4, (netbsd_socklen_t *)args->arg5)) {
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
netbsd_accept(
    nix_us_syscall_if_t        *xus,
    nix_monitor_t              *xmon,
    netbsd_accept_args_t const *args,
    netbsd_accept_result_t     *result)
{
	LCLog(g_bsd_log, LCLogDebug, 0, "invoked", 0);

	/* Insert here implementation code */

	return (ENOSYS);
}

int
netbsd_getpeername(
    nix_us_syscall_if_t             *xus,
    nix_monitor_t                   *xmon,
    netbsd_getpeername_args_t const *args,
    netbsd_getpeername_result_t     *result)
{
	nix_guest_info_t    gi;
	struct nix_sockaddr sa;
	nix_socklen_t       salen = sizeof(sa);
	nix_env_t          *env = netbsd_us_syscall_get_nix_env(xus);

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
		if (!nix_sockaddr_to_netbsd_sockaddr(gi.endian, &sa, salen,
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
netbsd_getsockname(
    nix_us_syscall_if_t             *xus,
    nix_monitor_t                   *xmon,
    netbsd_getsockname_args_t const *args,
    netbsd_getsockname_result_t     *result)
{
	nix_guest_info_t    gi;
	struct nix_sockaddr sa;
	nix_socklen_t       salen = sizeof(sa);
	nix_env_t          *env = netbsd_us_syscall_get_nix_env(xus);

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
		if (!nix_sockaddr_to_netbsd_sockaddr(gi.endian, &sa, salen, args->arg1, args->arg2)) {
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
netbsd_access(
    nix_us_syscall_if_t        *xus,
    nix_monitor_t              *xmon,
    netbsd_access_args_t const *args,
    netbsd_access_result_t     *result)
{
	nix_env_t *env = netbsd_us_syscall_get_nix_env(xus);

	LCLog(g_bsd_log, LCLogDebug, 0, "invoked", 0);

	nix_env_set_errno(env, 0);
	*result = nix_access((char const *)args->arg0, args->arg1, env);
	return (nix_env_get_errno(env));
}

int
netbsd_chflags(
    nix_us_syscall_if_t         *xus,
    nix_monitor_t               *xmon,
    netbsd_chflags_args_t const *args,
    netbsd_chflags_result_t     *result)
{
	nix_env_t *env = netbsd_us_syscall_get_nix_env(xus);

	LCLog(g_bsd_log, LCLogDebug, 0, "invoked", 0);

	nix_env_set_errno(env, 0);
	*result = nix_bsd_chflags((char const *)args->arg0, args->arg1, env);
	return (nix_env_get_errno(env));
}

int
netbsd_fchflags(
    nix_us_syscall_if_t          *xus,
    nix_monitor_t                *xmon,
    netbsd_fchflags_args_t const *args,
    netbsd_fchflags_result_t     *result)
{
	nix_env_t *env = netbsd_us_syscall_get_nix_env(xus);

	LCLog(g_bsd_log, LCLogDebug, 0, "invoked", 0);

	nix_env_set_errno(env, 0);
	*result = nix_bsd_fchflags(args->arg0, args->arg1, env);
	return (nix_env_get_errno(env));
}

int
netbsd_kill(
    nix_us_syscall_if_t      *xus,
    nix_monitor_t            *xmon,
    netbsd_kill_args_t const *args,
    netbsd_kill_result_t     *result)
{
	nix_env_t *env = netbsd_us_syscall_get_nix_env(xus);

	LCLog(g_bsd_log, LCLogDebug, 0, "invoked", 0);

	nix_env_set_errno(env, 0);
	*result = nix_kill(args->arg0, args->arg1, env);
	return (nix_env_get_errno(env));
}

int
netbsd_dup(
    nix_us_syscall_if_t     *xus,
    nix_monitor_t           *xmon,
    netbsd_dup_args_t const *args,
    netbsd_dup_result_t     *result)
{
	nix_env_t *env = netbsd_us_syscall_get_nix_env(xus);

	LCLog(g_bsd_log, LCLogDebug, 0, "invoked", 0);

	nix_env_set_errno(env, 0);
	*result = nix_dup(args->arg0, env);
	return (nix_env_get_errno(env));
}

int
netbsd_profil(
    nix_us_syscall_if_t        *xus,
    nix_monitor_t              *xmon,
    netbsd_profil_args_t const *args,
    netbsd_profil_result_t     *result)
{
	nix_env_t *env = netbsd_us_syscall_get_nix_env(xus);

	LCLog(g_bsd_log, LCLogDebug, 0, "invoked", 0);

	nix_env_set_errno(env, 0);
	*result = nix_bsd_profil(args->arg0, args->arg1, args->arg2, args->arg3, env);
	return (nix_env_get_errno(env));
}

int
netbsd_ktrace(
    nix_us_syscall_if_t        *xus,
    nix_monitor_t              *xmon,
    netbsd_ktrace_args_t const *args,
    netbsd_ktrace_result_t     *result)
{
	nix_env_t *env = netbsd_us_syscall_get_nix_env(xus);

	LCLog(g_bsd_log, LCLogDebug, 0, "invoked", 0);

	nix_env_set_errno(env, 0);
	*result = nix_bsd_ktrace(args->arg0, args->arg1, args->arg2, args->arg3, env);
	return (nix_env_get_errno(env));
}

int
netbsd_acct(
    nix_us_syscall_if_t      *xus,
    nix_monitor_t            *xmon,
    netbsd_acct_args_t const *args,
    netbsd_acct_result_t     *result)
{
	nix_env_t *env = netbsd_us_syscall_get_nix_env(xus);

	LCLog(g_bsd_log, LCLogDebug, 0, "invoked", 0);

	nix_env_set_errno(env, 0);
	*result = nix_bsd_acct((char const *)args->arg0, env);
	return (nix_env_get_errno(env));
}

int
netbsd_ioctl(
    nix_us_syscall_if_t       *xus,
    nix_monitor_t             *xmon,
    netbsd_ioctl_args_t const *args,
    netbsd_ioctl_result_t     *result)
{
	nix_guest_info_t gi;
	nix_env_t       *env = netbsd_us_syscall_get_nix_env(xus);

	LCLog(g_bsd_log, LCLogDebug, 0, "invoked", 0);

	nix_monitor_get_guest_info(xmon, &gi);

	nix_env_set_errno(env, 0);
	*result = netbsd_ioctl_dispatch(env, gi.endian, args->arg0, args->arg1, args->arg2);
	return (nix_env_get_errno(env));
}

int
netbsd_reboot(
    nix_us_syscall_if_t        *xus,
    nix_monitor_t              *xmon,
    netbsd_reboot_args_t const *args,
    netbsd_reboot_result_t     *result)
{
	nix_env_t *env = netbsd_us_syscall_get_nix_env(xus);

	LCLog(g_bsd_log, LCLogDebug, 0, "invoked", 0);

	nix_env_set_errno(env, 0);
	*result = nix_reboot(args->arg0, env);
	return (nix_env_get_errno(env));
}

int
netbsd_revoke(
    nix_us_syscall_if_t        *xus,
    nix_monitor_t              *xmon,
    netbsd_revoke_args_t const *args,
    netbsd_revoke_result_t     *result)
{
	nix_env_t *env = netbsd_us_syscall_get_nix_env(xus);

	LCLog(g_bsd_log, LCLogDebug, 0, "invoked", 0);

	nix_env_set_errno(env, 0);
	*result = nix_bsd_revoke((char const *)args->arg0, env);
	return (nix_env_get_errno(env));
}

int
netbsd_symlink(
    nix_us_syscall_if_t         *xus,
    nix_monitor_t               *xmon,
    netbsd_symlink_args_t const *args,
    netbsd_symlink_result_t     *result)
{
	nix_env_t *env = netbsd_us_syscall_get_nix_env(xus);

	LCLog(g_bsd_log, LCLogDebug, 0, "invoked", 0);

	nix_env_set_errno(env, 0);
	*result = nix_symlink((char const *)args->arg0, (char const *)args->arg1, env);
	return (nix_env_get_errno(env));
}

int
netbsd_readlink(
    nix_us_syscall_if_t          *xus,
    nix_monitor_t                *xmon,
    netbsd_readlink_args_t const *args,
    netbsd_readlink_result_t     *result)
{
	nix_env_t *env = netbsd_us_syscall_get_nix_env(xus);

	LCLog(g_bsd_log, LCLogDebug, 0, "invoked", 0);

	nix_env_set_errno(env, 0);
	*result = nix_readlink((char *)args->arg0, args->arg1, args->arg2, env);
	return (nix_env_get_errno(env));
}

int
netbsd_execve(
    nix_us_syscall_if_t        *xus,
    nix_monitor_t              *xmon,
    netbsd_execve_args_t const *args,
    netbsd_execve_result_t     *result)
{
	LCLog(g_bsd_log, LCLogDebug, 0, "invoked", 0);

	/* Insert here implementation code */

	return (ENOSYS);
}

int
netbsd_umask(
    nix_us_syscall_if_t       *xus,
    nix_monitor_t             *xmon,
    netbsd_umask_args_t const *args,
    netbsd_umask_result_t     *result)
{
	nix_env_t *env = netbsd_us_syscall_get_nix_env(xus);

	LCLog(g_bsd_log, LCLogDebug, 0, "invoked", 0);

	nix_env_set_errno(env, 0);
	*result = nix_umask(args->arg0, env);
	return (nix_env_get_errno(env));
}

int
netbsd_chroot(
    nix_us_syscall_if_t        *xus,
    nix_monitor_t              *xmon,
    netbsd_chroot_args_t const *args,
    netbsd_chroot_result_t     *result)
{
	nix_env_t *env = netbsd_us_syscall_get_nix_env(xus);

	LCLog(g_bsd_log, LCLogDebug, 0, "invoked", 0);

	nix_env_set_errno(env, 0);
	*result = nix_chroot((char const *)args->arg0, env);
	return (nix_env_get_errno(env));
}

int
netbsd_vfork(
    nix_us_syscall_if_t       *xus,
    nix_monitor_t             *xmon,
    netbsd_vfork_args_t const *args,
    netbsd_vfork_result_t     *result)
{
	nix_env_t *env = netbsd_us_syscall_get_nix_env(xus);

	LCLog(g_bsd_log, LCLogDebug, 0, "invoked", 0);

	nix_env_set_errno(env, 0);
	*result = nix_vfork(env);
	return (nix_env_get_errno(env));
}

int
netbsd_munmap(
    nix_us_syscall_if_t        *xus,
    nix_monitor_t              *xmon,
    netbsd_munmap_args_t const *args,
    netbsd_munmap_result_t     *result)
{
	nix_env_t *env = netbsd_us_syscall_get_nix_env(xus);

	LCLog(g_bsd_log, LCLogDebug, 0, "invoked", 0);

	nix_env_set_errno(env, 0);
	*result = nix_munmap((uintmax_t)(uintptr_t)args->arg0, args->arg1, env);
	return (nix_env_get_errno(env));
}

int
netbsd_mprotect(
    nix_us_syscall_if_t          *xus,
    nix_monitor_t                *xmon,
    netbsd_mprotect_args_t const *args,
    netbsd_mprotect_result_t     *result)
{
	nix_env_t *env = netbsd_us_syscall_get_nix_env(xus);

	LCLog(g_bsd_log, LCLogDebug, 0, "invoked", 0);

	nix_env_set_errno(env, 0);
	*result = nix_mprotect((uintmax_t)(uintptr_t)args->arg0, args->arg1, args->arg2, env);
	return (nix_env_get_errno(env));
}

int
netbsd_madvise(
    nix_us_syscall_if_t         *xus,
    nix_monitor_t               *xmon,
    netbsd_madvise_args_t const *args,
    netbsd_madvise_result_t     *result)
{
	nix_env_t *env = netbsd_us_syscall_get_nix_env(xus);

	LCLog(g_bsd_log, LCLogDebug, 0, "invoked", 0);

	nix_env_set_errno(env, 0);
	*result = nix_madvise((uintmax_t)(uintptr_t)args->arg0, args->arg1, args->arg2, env);
	return (nix_env_get_errno(env));
}

int
netbsd_mincore(
    nix_us_syscall_if_t         *xus,
    nix_monitor_t               *xmon,
    netbsd_mincore_args_t const *args,
    netbsd_mincore_result_t     *result)
{
	nix_env_t *env = netbsd_us_syscall_get_nix_env(xus);

	LCLog(g_bsd_log, LCLogDebug, 0, "invoked", 0);

	nix_env_set_errno(env, 0);
	*result = nix_mincore((uintmax_t)(uintptr_t)args->arg0, args->arg1, (char *)args->arg2, env);
	return (nix_env_get_errno(env));
}

int
netbsd_getgroups(
    nix_us_syscall_if_t           *xus,
    nix_monitor_t                 *xmon,
    netbsd_getgroups_args_t const *args,
    netbsd_getgroups_result_t     *result)
{
	//	nix_env_t *env = netbsd_us_syscall_get_nix_env(xus);

	LCLog(g_bsd_log, LCLogDebug, 0, "invoked", 0);

	return (ENOSYS);
}

int
netbsd_setgroups(
    nix_us_syscall_if_t           *xus,
    nix_monitor_t                 *xmon,
    netbsd_setgroups_args_t const *args,
    netbsd_setgroups_result_t     *result)
{
	LCLog(g_bsd_log, LCLogDebug, 0, "invoked", 0);

	/* Insert here implementation code */

	return (ENOSYS);
}

int
netbsd_getpgrp(
    nix_us_syscall_if_t         *xus,
    nix_monitor_t               *xmon,
    netbsd_getpgrp_args_t const *args,
    netbsd_getpgrp_result_t     *result)
{
	nix_env_t *env = netbsd_us_syscall_get_nix_env(xus);

	LCLog(g_bsd_log, LCLogDebug, 0, "invoked", 0);

	nix_env_set_errno(env, 0);
	*result = nix_getpgrp(env);
	return (nix_env_get_errno(env));
}

int
netbsd_setpgid(
    nix_us_syscall_if_t         *xus,
    nix_monitor_t               *xmon,
    netbsd_setpgid_args_t const *args,
    netbsd_setpgid_result_t     *result)
{
	nix_env_t *env = netbsd_us_syscall_get_nix_env(xus);

	LCLog(g_bsd_log, LCLogDebug, 0, "invoked", 0);

	nix_env_set_errno(env, 0);
	*result = nix_setpgid(args->arg0, args->arg1, env);
	return (nix_env_get_errno(env));
}

int
netbsd_dup2(
    nix_us_syscall_if_t      *xus,
    nix_monitor_t            *xmon,
    netbsd_dup2_args_t const *args,
    netbsd_dup2_result_t     *result)
{
	nix_env_t *env = netbsd_us_syscall_get_nix_env(xus);

	LCLog(g_bsd_log, LCLogDebug, 0, "invoked", 0);

	nix_env_set_errno(env, 0);
	*result = nix_dup2(args->arg0, args->arg1, env);
	return (nix_env_get_errno(env));
}

int
netbsd_fcntl(
    nix_us_syscall_if_t       *xus,
    nix_monitor_t             *xmon,
    netbsd_fcntl_args_t const *args,
    netbsd_fcntl_result_t     *result)
{
	nix_env_t *env = netbsd_us_syscall_get_nix_env(xus);

	LCLog(g_bsd_log, LCLogDebug, 0, "invoked", 0);

	nix_env_set_errno(env, 0);
	*result = nix_fcntl(args->arg0, args->arg1, args->arg2, env);
	return (nix_env_get_errno(env));
}

int
netbsd_fsync(
    nix_us_syscall_if_t       *xus,
    nix_monitor_t             *xmon,
    netbsd_fsync_args_t const *args,
    netbsd_fsync_result_t     *result)
{
	nix_env_t *env = netbsd_us_syscall_get_nix_env(xus);

	LCLog(g_bsd_log, LCLogDebug, 0, "invoked", 0);

	nix_env_set_errno(env, 0);
	*result = nix_fsync(args->arg0, env);
	return (nix_env_get_errno(env));
}

int
netbsd_setpriority(
    nix_us_syscall_if_t             *xus,
    nix_monitor_t                   *xmon,
    netbsd_setpriority_args_t const *args,
    netbsd_setpriority_result_t     *result)
{
	nix_env_t *env = netbsd_us_syscall_get_nix_env(xus);

	LCLog(g_bsd_log, LCLogDebug, 0, "invoked", 0);

	nix_env_set_errno(env, 0);
	*result = nix_setpriority(args->arg0, args->arg1, args->arg2, env);
	return (nix_env_get_errno(env));
}

int
netbsd_connect(
    nix_us_syscall_if_t         *xus,
    nix_monitor_t               *xmon,
    netbsd_connect_args_t const *args,
    netbsd_connect_result_t     *result)
{
	nix_guest_info_t    gi;
	int                 rc;
	struct nix_sockaddr sa;
	nix_socklen_t       salen = sizeof(sa);
	nix_env_t          *env = netbsd_us_syscall_get_nix_env(xus);

	LCLog(g_bsd_log, LCLogDebug, 0, "invoked", 0);

	if (args->arg1 == NULL)
		return (EFAULT);

	nix_monitor_get_guest_info(xmon, &gi);

	rc = 0;

	__nix_try
	{
		if (!netbsd_sockaddr_to_nix_sockaddr(gi.endian, args->arg1, args->arg2, &sa, &salen)) {
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
netbsd_getpriority(
    nix_us_syscall_if_t             *xus,
    nix_monitor_t                   *xmon,
    netbsd_getpriority_args_t const *args,
    netbsd_getpriority_result_t     *result)
{

	LCLog(g_bsd_log, LCLogDebug, 0, "invoked", 0);

	/* Insert here implementation code */

	return (ENOSYS);
}

int
netbsd_bind(
    nix_us_syscall_if_t      *xus,
    nix_monitor_t            *xmon,
    netbsd_bind_args_t const *args,
    netbsd_bind_result_t     *result)
{
	nix_guest_info_t    gi;
	int                 rc;
	struct nix_sockaddr sa;
	nix_socklen_t       salen = sizeof(sa);
	nix_env_t          *env = netbsd_us_syscall_get_nix_env(xus);

	LCLog(g_bsd_log, LCLogDebug, 0, "invoked", 0);

	if (args->arg1 == NULL)
		return (EFAULT);

	nix_monitor_get_guest_info(xmon, &gi);

	rc = 0;

	__nix_try
	{
		if (!netbsd_sockaddr_to_nix_sockaddr(gi.endian, args->arg1, args->arg2, &sa, &salen)) {
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
netbsd_setsockopt(
    nix_us_syscall_if_t            *xus,
    nix_monitor_t                  *xmon,
    netbsd_setsockopt_args_t const *args,
    netbsd_setsockopt_result_t     *result)
{
	LCLog(g_bsd_log, LCLogDebug, 0, "invoked", 0);

	/* Insert here implementation code */

	*result = 0;
	return (0); // ENOSYS;
}

int
netbsd_listen(
    nix_us_syscall_if_t        *xus,
    nix_monitor_t              *xmon,
    netbsd_listen_args_t const *args,
    netbsd_listen_result_t     *result)
{
	nix_env_t *env = netbsd_us_syscall_get_nix_env(xus);

	LCLog(g_bsd_log, LCLogDebug, 0, "invoked", 0);

	nix_env_set_errno(env, 0);
	*result = nix_listen(args->arg0, args->arg1, env);
	return (nix_env_get_errno(env));
}

int
netbsd_getsockopt(
    nix_us_syscall_if_t            *xus,
    nix_monitor_t                  *xmon,
    netbsd_getsockopt_args_t const *args,
    netbsd_getsockopt_result_t     *result)
{
	LCLog(g_bsd_log, LCLogDebug, 0, "invoked", 0);

	/* Insert here implementation code */
	*result = 0;
	return (0); // ENOSYS;
}

int
netbsd_readv(
    nix_us_syscall_if_t       *xus,
    nix_monitor_t             *xmon,
    netbsd_readv_args_t const *args,
    netbsd_readv_result_t     *result)
{
	nix_guest_info_t  gi;
	struct nix_iovec *xiov;
	nix_env_t        *env = netbsd_us_syscall_get_nix_env(xus);
	nix_mem_if_t     *mem = nix_monitor_get_memory(xmon);

	LCLog(g_bsd_log, LCLogDebug, 0, "invoked", 0);

#if 0	
	nix_monitor_get_guest_info (xmon, &gi);
#else
	/*XXX */
	gi.endian = NIX_ENDIAN_BIG;
#endif

	xiov = __netbsd_iovec32_copy_from(env, mem, &gi, args->arg1, args->arg2);
	if (xiov != NULL) {
		*result = nix_readv(args->arg0, xiov, args->arg2, env);
		nix_free(xiov);
	}

	return (nix_env_get_errno(env));
}

int
netbsd_writev(
    nix_us_syscall_if_t        *xus,
    nix_monitor_t              *xmon,
    netbsd_writev_args_t const *args,
    netbsd_writev_result_t     *result)
{
	nix_guest_info_t  gi;
	struct nix_iovec *xiov;
	nix_env_t        *env = netbsd_us_syscall_get_nix_env(xus);
	nix_mem_if_t     *mem = nix_monitor_get_memory(xmon);

	LCLog(g_bsd_log, LCLogDebug, 0, "invoked", 0);

	nix_monitor_get_guest_info(xmon, &gi);

	xiov = __netbsd_iovec32_copy_from(env, mem, &gi, args->arg1, args->arg2);
	if (xiov != NULL) {
		*result = nix_writev(args->arg0, xiov, args->arg2, env);
		nix_free(xiov);
	}

	return (nix_env_get_errno(env));
}

int
netbsd_fchown(
    nix_us_syscall_if_t        *xus,
    nix_monitor_t              *xmon,
    netbsd_fchown_args_t const *args,
    netbsd_fchown_result_t     *result)
{
	nix_env_t *env = netbsd_us_syscall_get_nix_env(xus);

	LCLog(g_bsd_log, LCLogDebug, 0, "invoked", 0);

	nix_env_set_errno(env, 0);
	*result = nix_fchown(args->arg0, args->arg1, args->arg2, env);
	return (nix_env_get_errno(env));
}

int
netbsd_fchmod(
    nix_us_syscall_if_t        *xus,
    nix_monitor_t              *xmon,
    netbsd_fchmod_args_t const *args,
    netbsd_fchmod_result_t     *result)
{
	nix_env_t *env = netbsd_us_syscall_get_nix_env(xus);

	LCLog(g_bsd_log, LCLogDebug, 0, "invoked", 0);

	nix_env_set_errno(env, 0);
	*result = nix_fchmod(args->arg0, args->arg1, env);
	return (nix_env_get_errno(env));
}

int
netbsd_setreuid(
    nix_us_syscall_if_t          *xus,
    nix_monitor_t                *xmon,
    netbsd_setreuid_args_t const *args,
    netbsd_setreuid_result_t     *result)
{
	nix_env_t *env = netbsd_us_syscall_get_nix_env(xus);

	LCLog(g_bsd_log, LCLogDebug, 0, "invoked", 0);

	nix_env_set_errno(env, 0);
	*result = nix_setreuid(args->arg0, args->arg1, env);
	return (nix_env_get_errno(env));
}

int
netbsd_setregid(
    nix_us_syscall_if_t          *xus,
    nix_monitor_t                *xmon,
    netbsd_setregid_args_t const *args,
    netbsd_setregid_result_t     *result)
{
	nix_env_t *env = netbsd_us_syscall_get_nix_env(xus);

	LCLog(g_bsd_log, LCLogDebug, 0, "invoked", 0);

	nix_env_set_errno(env, 0);
	*result = nix_setregid(args->arg0, args->arg1, env);
	return (nix_env_get_errno(env));
}

int
netbsd_rename(
    nix_us_syscall_if_t        *xus,
    nix_monitor_t              *xmon,
    netbsd_rename_args_t const *args,
    netbsd_rename_result_t     *result)
{
	nix_env_t *env = netbsd_us_syscall_get_nix_env(xus);

	LCLog(g_bsd_log, LCLogDebug, 0, "invoked", 0);

	nix_env_set_errno(env, 0);
	*result = nix_rename((char const *)args->arg0, (char const *)args->arg1, env);
	return (nix_env_get_errno(env));
}

int
netbsd_flock(
    nix_us_syscall_if_t       *xus,
    nix_monitor_t             *xmon,
    netbsd_flock_args_t const *args,
    netbsd_flock_result_t     *result)
{
	nix_env_t *env = netbsd_us_syscall_get_nix_env(xus);

	LCLog(g_bsd_log, LCLogDebug, 0, "invoked", 0);

	nix_env_set_errno(env, 0);
	*result = nix_flock(args->arg0, args->arg1, env);
	return (nix_env_get_errno(env));
}

int
netbsd_mkfifo(
    nix_us_syscall_if_t        *xus,
    nix_monitor_t              *xmon,
    netbsd_mkfifo_args_t const *args,
    netbsd_mkfifo_result_t     *result)
{
	nix_env_t *env = netbsd_us_syscall_get_nix_env(xus);

	LCLog(g_bsd_log, LCLogDebug, 0, "invoked", 0);

	nix_env_set_errno(env, 0);
	*result = nix_mkfifo((char const *)args->arg0, args->arg1, env);
	return (nix_env_get_errno(env));
}

int
netbsd_sendto(
    nix_us_syscall_if_t        *xus,
    nix_monitor_t              *xmon,
    netbsd_sendto_args_t const *args,
    netbsd_sendto_result_t     *result)
{
	nix_guest_info_t     gi;
	struct nix_sockaddr  sa;
	int                  rc = -1;
	struct nix_sockaddr *psa = args->arg4 != NULL ? &sa : NULL;
	nix_socklen_t        salen = sizeof(sa);
	nix_env_t           *env = netbsd_us_syscall_get_nix_env(xus);

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
			if (!netbsd_sockaddr_to_nix_sockaddr(gi.endian, args->arg4, args->arg5, &sa, &salen)) {
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
netbsd_shutdown(
    nix_us_syscall_if_t          *xus,
    nix_monitor_t                *xmon,
    netbsd_shutdown_args_t const *args,
    netbsd_shutdown_result_t     *result)
{
	nix_env_t *env = netbsd_us_syscall_get_nix_env(xus);

	LCLog(g_bsd_log, LCLogDebug, 0, "invoked", 0);

	nix_env_set_errno(env, 0);
	*result = nix_shutdown(args->arg0, args->arg1, env);
	return (nix_env_get_errno(env));
}

int
netbsd_socketpair(
    nix_us_syscall_if_t            *xus,
    nix_monitor_t                  *xmon,
    netbsd_socketpair_args_t const *args,
    netbsd_socketpair_result_t     *result)
{
	LCLog(g_bsd_log, LCLogDebug, 0, "invoked", 0);

	/* Insert here implementation code */

	return (ENOSYS);
}

int
netbsd_mkdir(
    nix_us_syscall_if_t       *xus,
    nix_monitor_t             *xmon,
    netbsd_mkdir_args_t const *args,
    netbsd_mkdir_result_t     *result)
{
	nix_env_t *env = netbsd_us_syscall_get_nix_env(xus);

	LCLog(g_bsd_log, LCLogDebug, 0, "invoked", 0);

	nix_env_set_errno(env, 0);
	*result = nix_mkdir((char const *)args->arg0, args->arg1, env);
	return (nix_env_get_errno(env));
}

int
netbsd_rmdir(
    nix_us_syscall_if_t       *xus,
    nix_monitor_t             *xmon,
    netbsd_rmdir_args_t const *args,
    netbsd_rmdir_result_t     *result)
{
	nix_env_t *env = netbsd_us_syscall_get_nix_env(xus);

	LCLog(g_bsd_log, LCLogDebug, 0, "invoked", 0);

	nix_env_set_errno(env, 0);
	*result = nix_rmdir((char const *)args->arg0, env);
	return (nix_env_get_errno(env));
}

int
netbsd_setsid(
    nix_us_syscall_if_t        *xus,
    nix_monitor_t              *xmon,
    netbsd_setsid_args_t const *args,
    netbsd_setsid_result_t     *result)
{
	nix_env_t *env = netbsd_us_syscall_get_nix_env(xus);

	LCLog(g_bsd_log, LCLogDebug, 0, "invoked", 0);

	nix_env_set_errno(env, 0);
	*result = nix_setsid(env);
	return (nix_env_get_errno(env));
}

int
netbsd_nfssvc(
    nix_us_syscall_if_t        *xus,
    nix_monitor_t              *xmon,
    netbsd_nfssvc_args_t const *args,
    netbsd_nfssvc_result_t     *result)
{
	LCLog(g_bsd_log, LCLogDebug, 0, "invoked", 0);

	/* Insert here implementation code */

	return (ENOSYS);
}

int
netbsd_sysarch(
    nix_us_syscall_if_t         *xus,
    nix_monitor_t               *xmon,
    netbsd_sysarch_args_t const *args,
    netbsd_sysarch_result_t     *result)
{
	LCLog(g_bsd_log, LCLogDebug, 0, "invoked", 0);

	/* Insert here implementation code */

	return (ENOSYS);
}

int
netbsd_pread(
    nix_us_syscall_if_t       *xus,
    nix_monitor_t             *xmon,
    netbsd_pread_args_t const *args,
    netbsd_pread_result_t     *result)
{
	nix_env_t *env = netbsd_us_syscall_get_nix_env(xus);

	LCLog(g_bsd_log, LCLogDebug, 0, "invoked", 0);

	nix_env_set_errno(env, 0);
	*result = nix_pread(args->arg0, args->arg1, args->arg2, args->arg4, env);
	return (nix_env_get_errno(env));
}

int
netbsd_pwrite(
    nix_us_syscall_if_t        *xus,
    nix_monitor_t              *xmon,
    netbsd_pwrite_args_t const *args,
    netbsd_pwrite_result_t     *result)
{
	nix_env_t *env = netbsd_us_syscall_get_nix_env(xus);

	LCLog(g_bsd_log, LCLogDebug, 0, "invoked", 0);

	nix_env_set_errno(env, 0);
	*result = nix_pwrite(args->arg0, args->arg1, args->arg2, args->arg4, env);
	return (nix_env_get_errno(env));
}

int
netbsd_setgid(
    nix_us_syscall_if_t        *xus,
    nix_monitor_t              *xmon,
    netbsd_setgid_args_t const *args,
    netbsd_setgid_result_t     *result)
{
	nix_env_t *env = netbsd_us_syscall_get_nix_env(xus);

	LCLog(g_bsd_log, LCLogDebug, 0, "invoked", 0);

	nix_env_set_errno(env, 0);
	*result = nix_setgid(args->arg0, env);
	return (nix_env_get_errno(env));
}

int
netbsd_setegid(
    nix_us_syscall_if_t         *xus,
    nix_monitor_t               *xmon,
    netbsd_setegid_args_t const *args,
    netbsd_setegid_result_t     *result)
{
	nix_env_t *env = netbsd_us_syscall_get_nix_env(xus);

	LCLog(g_bsd_log, LCLogDebug, 0, "invoked", 0);

	nix_env_set_errno(env, 0);
	*result = nix_setegid(args->arg0, env);
	return (nix_env_get_errno(env));
}

int
netbsd_seteuid(
    nix_us_syscall_if_t         *xus,
    nix_monitor_t               *xmon,
    netbsd_seteuid_args_t const *args,
    netbsd_seteuid_result_t     *result)
{
	nix_env_t *env = netbsd_us_syscall_get_nix_env(xus);

	LCLog(g_bsd_log, LCLogDebug, 0, "invoked", 0);

	nix_env_set_errno(env, 0);
	*result = nix_seteuid(args->arg0, env);
	return (nix_env_get_errno(env));
}

int
netbsd_lfs_bmapv(
    nix_us_syscall_if_t           *xus,
    nix_monitor_t                 *xmon,
    netbsd_lfs_bmapv_args_t const *args,
    netbsd_lfs_bmapv_result_t     *result)
{
	LCLog(g_bsd_log, LCLogDebug, 0, "invoked", 0);

	/* Insert here implementation code */

	return (ENOSYS);
}

int
netbsd_lfs_markv(
    nix_us_syscall_if_t           *xus,
    nix_monitor_t                 *xmon,
    netbsd_lfs_markv_args_t const *args,
    netbsd_lfs_markv_result_t     *result)
{
	LCLog(g_bsd_log, LCLogDebug, 0, "invoked", 0);

	/* Insert here implementation code */

	return (ENOSYS);
}

int
netbsd_lfs_segclean(
    nix_us_syscall_if_t              *xus,
    nix_monitor_t                    *xmon,
    netbsd_lfs_segclean_args_t const *args,
    netbsd_lfs_segclean_result_t     *result)
{
	LCLog(g_bsd_log, LCLogDebug, 0, "invoked", 0);

	/* Insert here implementation code */

	return (ENOSYS);
}

int
netbsd_pathconf(
    nix_us_syscall_if_t          *xus,
    nix_monitor_t                *xmon,
    netbsd_pathconf_args_t const *args,
    netbsd_pathconf_result_t     *result)
{
	nix_env_t *env = netbsd_us_syscall_get_nix_env(xus);

	LCLog(g_bsd_log, LCLogDebug, 0, "invoked", 0);

	nix_env_set_errno(env, 0);
	*result = nix_pathconf((char const *)args->arg0, args->arg1, env);
	return (nix_env_get_errno(env));
}

int
netbsd_fpathconf(
    nix_us_syscall_if_t           *xus,
    nix_monitor_t                 *xmon,
    netbsd_fpathconf_args_t const *args,
    netbsd_fpathconf_result_t     *result)
{
	nix_env_t *env = netbsd_us_syscall_get_nix_env(xus);

	LCLog(g_bsd_log, LCLogDebug, 0, "invoked", 0);

	nix_env_set_errno(env, 0);
	*result = nix_fpathconf(args->arg0, args->arg1, env);
	return (nix_env_get_errno(env));
}

int
netbsd_swapctl(
    nix_us_syscall_if_t         *xus,
    nix_monitor_t               *xmon,
    netbsd_swapctl_args_t const *args,
    netbsd_swapctl_result_t     *result)
{
	LCLog(g_bsd_log, LCLogDebug, 0, "invoked", 0);

	/* Insert here implementation code */

	return (ENOSYS);
}

int
netbsd_getrlimit(
    nix_us_syscall_if_t           *xus,
    nix_monitor_t                 *xmon,
    netbsd_getrlimit_args_t const *args,
    netbsd_getrlimit_result_t     *result)
{
	nix_guest_info_t  gi;
	int               resource;
	struct nix_rlimit rl;
	nix_env_t        *env = netbsd_us_syscall_get_nix_env(xus);

	LCLog(g_bsd_log, LCLogDebug, 0, "invoked", 0);

	if (args->arg1 == NULL)
		return (EFAULT);

	nix_monitor_get_guest_info(xmon, &gi);

	nix_env_set_errno(env, 0);
	switch (args->arg0) {
	case NETBSD_RLIMIT_CPU:
		resource = NIX_RLIMIT_CPU;
		break;
	case NETBSD_RLIMIT_CORE:
		resource = NIX_RLIMIT_CORE;
		break;
	case NETBSD_RLIMIT_DATA:
		resource = NIX_RLIMIT_DATA;
		break;
	case NETBSD_RLIMIT_FSIZE:
		resource = NIX_RLIMIT_FSIZE;
		break;
	case NETBSD_RLIMIT_MEMLOCK:
		resource = NIX_RLIMIT_MEMLOCK;
		break;
	case NETBSD_RLIMIT_NOFILE:
		resource = NIX_RLIMIT_NOFILE;
		break;
	case NETBSD_RLIMIT_NPROC:
		resource = NIX_RLIMIT_NPROC;
		break;
	case NETBSD_RLIMIT_RSS:
		resource = NIX_RLIMIT_RSS;
		break;
	case NETBSD_RLIMIT_STACK:
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
		nix_rlimit_to_netbsd_rlimit(gi.endian, &rl, args->arg1);
	}
	__nix_catch_any
	{
		nix_env_set_errno(env, EFAULT);
	}
	__nix_end_try

	    return (nix_env_get_errno(env));
}

int
netbsd_setrlimit(
    nix_us_syscall_if_t           *xus,
    nix_monitor_t                 *xmon,
    netbsd_setrlimit_args_t const *args,
    netbsd_setrlimit_result_t     *result)
{
	LCLog(g_bsd_log, LCLogDebug, 0, "invoked", 0);

	/* Insert here implementation code */

	*result = 0;
	return (0); // XXX ENOSYS;
}

int
netbsd_mmap(
    nix_us_syscall_if_t      *xus,
    nix_monitor_t            *xmon,
    netbsd_mmap_args_t const *args,
    netbsd_mmap_result_t     *result)
{
	int        prot;
	int        flags;
	nix_env_t *env = netbsd_us_syscall_get_nix_env(xus);

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
netbsd_lseek(
    nix_us_syscall_if_t       *xus,
    nix_monitor_t             *xmon,
    netbsd_lseek_args_t const *args,
    netbsd_lseek_result_t     *result)
{
	nix_env_t *env = netbsd_us_syscall_get_nix_env(xus);

	LCLog(g_bsd_log, LCLogDebug, 0, "invoked", 0);

	nix_env_set_errno(env, 0);
	*result = nix_lseek(args->arg0, args->arg2, args->arg3, env);
	return (nix_env_get_errno(env));
}

int
netbsd_truncate(
    nix_us_syscall_if_t          *xus,
    nix_monitor_t                *xmon,
    netbsd_truncate_args_t const *args,
    netbsd_truncate_result_t     *result)
{
	nix_env_t *env = netbsd_us_syscall_get_nix_env(xus);

	LCLog(g_bsd_log, LCLogDebug, 0, "invoked", 0);

	nix_env_set_errno(env, 0);
	*result = nix_truncate((char const *)args->arg0, args->arg1, env);
	return (nix_env_get_errno(env));
}

int
netbsd_ftruncate(
    nix_us_syscall_if_t           *xus,
    nix_monitor_t                 *xmon,
    netbsd_ftruncate_args_t const *args,
    netbsd_ftruncate_result_t     *result)
{
	nix_env_t *env = netbsd_us_syscall_get_nix_env(xus);

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
netbsd___sysctl(
    nix_us_syscall_if_t          *xus,
    nix_monitor_t                *xmon,
    netbsd___sysctl_args_t const *args,
    netbsd___sysctl_result_t     *result)
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
	case NETBSD_CTL_KERN:
		switch (GE32(&gi, mib[1])) {
		case NETBSD_KERN_OSTYPE:
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

		case NETBSD_KERN_OSRELEASE:
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

		case NETBSD_KERN_ARGMAX:
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

		case NETBSD_KERN_OSVERSION:
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

		case NETBSD_KERN_HOSTNAME: {
			char       hostname[512];
			nix_env_t *env = netbsd_us_syscall_get_nix_env(xus);

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

		case NETBSD_KERN_DOMAINNAME: {
			char       domainname[512];
			nix_env_t *env = netbsd_us_syscall_get_nix_env(xus);

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

		case NETBSD_KERN_ARND: {
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

	case NETBSD_CTL_HW:
		switch (GE32(&gi, mib[1])) {
		case NETBSD_HW_MACHINE:
			LCAssert(g_bsd_log, miblen == 2);
			__nix_try
			{
				__str_copy(&gi, (char *)oldp, oldlenp, NETBSD_MACHINE_NAME);
			}
			__nix_catch_any
			{
				*result = -1;
				return (EFAULT);
			}
			__nix_end_try return (0);

		case NETBSD_HW_NCPUS:
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

		case NETBSD_HW_PAGESIZE:
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
netbsd_mlock(
    nix_us_syscall_if_t       *xus,
    nix_monitor_t             *xmon,
    netbsd_mlock_args_t const *args,
    netbsd_mlock_result_t     *result)
{
	nix_env_t *env = netbsd_us_syscall_get_nix_env(xus);

	LCLog(g_bsd_log, LCLogDebug, 0, "invoked", 0);

	nix_env_set_errno(env, 0);
	*result = nix_mlock((uintmax_t)(uintptr_t)args->arg0, args->arg1, env);
	return (nix_env_get_errno(env));
}

int
netbsd_munlock(
    nix_us_syscall_if_t         *xus,
    nix_monitor_t               *xmon,
    netbsd_munlock_args_t const *args,
    netbsd_munlock_result_t     *result)
{
	nix_env_t *env = netbsd_us_syscall_get_nix_env(xus);

	LCLog(g_bsd_log, LCLogDebug, 0, "invoked", 0);

	nix_env_set_errno(env, 0);
	*result = nix_munlock((uintmax_t)(uintptr_t)args->arg0, args->arg1, env);
	return (nix_env_get_errno(env));
}

int
netbsd_getpgid(
    nix_us_syscall_if_t         *xus,
    nix_monitor_t               *xmon,
    netbsd_getpgid_args_t const *args,
    netbsd_getpgid_result_t     *result)
{
	nix_env_t *env = netbsd_us_syscall_get_nix_env(xus);

	LCLog(g_bsd_log, LCLogDebug, 0, "invoked", 0);

	nix_env_set_errno(env, 0);
	*result = nix_getpgid(args->arg0, env);
	return (nix_env_get_errno(env));
}

int
netbsd_semget(
    nix_us_syscall_if_t        *xus,
    nix_monitor_t              *xmon,
    netbsd_semget_args_t const *args,
    netbsd_semget_result_t     *result)
{
	LCLog(g_bsd_log, LCLogDebug, 0, "invoked", 0);

	/* Insert here implementation code */

	return (ENOSYS);
}

int
netbsd_msgget(
    nix_us_syscall_if_t        *xus,
    nix_monitor_t              *xmon,
    netbsd_msgget_args_t const *args,
    netbsd_msgget_result_t     *result)
{
	LCLog(g_bsd_log, LCLogDebug, 0, "invoked", 0);

	/* Insert here implementation code */

	return (ENOSYS);
}

int
netbsd_msgsnd(
    nix_us_syscall_if_t        *xus,
    nix_monitor_t              *xmon,
    netbsd_msgsnd_args_t const *args,
    netbsd_msgsnd_result_t     *result)
{
	LCLog(g_bsd_log, LCLogDebug, 0, "invoked", 0);

	/* Insert here implementation code */

	return (ENOSYS);
}

int
netbsd_msgrcv(
    nix_us_syscall_if_t        *xus,
    nix_monitor_t              *xmon,
    netbsd_msgrcv_args_t const *args,
    netbsd_msgrcv_result_t     *result)
{
	LCLog(g_bsd_log, LCLogDebug, 0, "invoked", 0);

	/* Insert here implementation code */

	return (ENOSYS);
}

int
netbsd_shmat(
    nix_us_syscall_if_t       *xus,
    nix_monitor_t             *xmon,
    netbsd_shmat_args_t const *args,
    netbsd_shmat_result_t     *result)
{
	LCLog(g_bsd_log, LCLogDebug, 0, "invoked", 0);

	/* Insert here implementation code */

	return (ENOSYS);
}

int
netbsd_shmdt(
    nix_us_syscall_if_t       *xus,
    nix_monitor_t             *xmon,
    netbsd_shmdt_args_t const *args,
    netbsd_shmdt_result_t     *result)
{
	LCLog(g_bsd_log, LCLogDebug, 0, "invoked", 0);

	/* Insert here implementation code */

	return (ENOSYS);
}

int
netbsd_minherit(
    nix_us_syscall_if_t          *xus,
    nix_monitor_t                *xmon,
    netbsd_minherit_args_t const *args,
    netbsd_minherit_result_t     *result)
{
	nix_env_t *env = netbsd_us_syscall_get_nix_env(xus);

	LCLog(g_bsd_log, LCLogDebug, 0, "invoked", 0);

	nix_env_set_errno(env, 0);
	*result = nix_minherit((uintmax_t)(uintptr_t)args->arg0, args->arg1, args->arg2, env);
	return (nix_env_get_errno(env));
}

int
netbsd_poll(
    nix_us_syscall_if_t      *xus,
    nix_monitor_t            *xmon,
    netbsd_poll_args_t const *args,
    netbsd_poll_result_t     *result)
{
	nix_guest_info_t      gi;
	struct netbsd_pollfd *ofds = args->arg0;
	struct nix_pollfd    *fds = NULL;
	nix_env_t            *env = netbsd_us_syscall_get_nix_env(xus);

	LCLog(g_bsd_log, LCLogDebug, 0, "invoked", 0);

	nix_monitor_get_guest_info(xmon, &gi);

	nix_env_set_errno(env, 0);
	fds = __netbsd_pollfd_copy_from(env, &gi, ofds, args->arg1);
	if (fds != NULL) {
		*result = nix_poll(fds, args->arg1, args->arg2, env);

		if (nix_env_get_errno(env) == 0) {
			/* Copy back the results */
			__nix_try
			{
				size_t n;

				for (n = 0; n < args->arg1; n++)
					nix_pollfd_to_netbsd_pollfd(gi.endian, fds + n, ofds + n);
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
netbsd_lchown(
    nix_us_syscall_if_t        *xus,
    nix_monitor_t              *xmon,
    netbsd_lchown_args_t const *args,
    netbsd_lchown_result_t     *result)
{
	nix_env_t *env = netbsd_us_syscall_get_nix_env(xus);

	LCLog(g_bsd_log, LCLogDebug, 0, "invoked", 0);

	nix_env_set_errno(env, 0);
	*result = nix_lchown((char const *)args->arg0, args->arg1, args->arg2, env);
	return (nix_env_get_errno(env));
}

int
netbsd_getsid(
    nix_us_syscall_if_t        *xus,
    nix_monitor_t              *xmon,
    netbsd_getsid_args_t const *args,
    netbsd_getsid_result_t     *result)
{
	nix_env_t *env = netbsd_us_syscall_get_nix_env(xus);

	LCLog(g_bsd_log, LCLogDebug, 0, "invoked", 0);

	nix_env_set_errno(env, 0);
	*result = nix_getsid(args->arg0, env);
	return (nix_env_get_errno(env));
}

int
netbsd_pipe(
    nix_us_syscall_if_t      *xus,
    nix_monitor_t            *xmon,
    netbsd_pipe_args_t const *args,
    netbsd_pipe_result_t     *result)
{
	int        fds[2];
	int        rc;
	nix_env_t *env = netbsd_us_syscall_get_nix_env(xus);

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
	 * so pack the pair accordingly (see netbsd-guest.c set_result).
	 */
	*result = ((uint64_t)(uint32_t)fds[0] << 32) | (uint32_t)fds[1];
	return (0);
}

int
netbsd_preadv(
    nix_us_syscall_if_t        *xus,
    nix_monitor_t              *xmon,
    netbsd_preadv_args_t const *args,
    netbsd_preadv_result_t     *result)
{
	LCLog(g_bsd_log, LCLogDebug, 0, "invoked", 0);

	/* Insert here implementation code */

	return (ENOSYS);
}

int
netbsd_pwritev(
    nix_us_syscall_if_t         *xus,
    nix_monitor_t               *xmon,
    netbsd_pwritev_args_t const *args,
    netbsd_pwritev_result_t     *result)
{
	LCLog(g_bsd_log, LCLogDebug, 0, "invoked", 0);

	/* Insert here implementation code */

	return (ENOSYS);
}

int
netbsd_kqueue(
    nix_us_syscall_if_t        *xus,
    nix_monitor_t              *xmon,
    netbsd_kqueue_args_t const *args,
    netbsd_kqueue_result_t     *result)
{
	nix_env_t *env = netbsd_us_syscall_get_nix_env(xus);

	LCLog(g_bsd_log, LCLogDebug, 0, "invoked", 0);

	nix_env_set_errno(env, 0);
	*result = nix_bsd_kqueue(env);
	return (nix_env_get_errno(env));
}

int
netbsd_mlockall(
    nix_us_syscall_if_t          *xus,
    nix_monitor_t                *xmon,
    netbsd_mlockall_args_t const *args,
    netbsd_mlockall_result_t     *result)
{
	nix_env_t *env = netbsd_us_syscall_get_nix_env(xus);

	LCLog(g_bsd_log, LCLogDebug, 0, "invoked", 0);

	nix_env_set_errno(env, 0);
	*result = nix_mlockall(args->arg0, env);
	return (nix_env_get_errno(env));
}

int
netbsd_munlockall(
    nix_us_syscall_if_t            *xus,
    nix_monitor_t                  *xmon,
    netbsd_munlockall_args_t const *args,
    netbsd_munlockall_result_t     *result)
{
	nix_env_t *env = netbsd_us_syscall_get_nix_env(xus);

	LCLog(g_bsd_log, LCLogDebug, 0, "invoked", 0);

	nix_env_set_errno(env, 0);
	*result = nix_munlockall(env);
	return (nix_env_get_errno(env));
}

int
netbsd_shmget(
    nix_us_syscall_if_t        *xus,
    nix_monitor_t              *xmon,
    netbsd_shmget_args_t const *args,
    netbsd_shmget_result_t     *result)
{
	LCLog(g_bsd_log, LCLogDebug, 0, "invoked", 0);

	/* Insert here implementation code */

	return (ENOSYS);
}

int
netbsd_semop(
    nix_us_syscall_if_t       *xus,
    nix_monitor_t             *xmon,
    netbsd_semop_args_t const *args,
    netbsd_semop_result_t     *result)
{
	LCLog(g_bsd_log, LCLogDebug, 0, "invoked", 0);

	/* Insert here implementation code */

	return (ENOSYS);
}

int
netbsd_sched_yield(
    nix_us_syscall_if_t             *xus,
    nix_monitor_t                   *xmon,
    netbsd_sched_yield_args_t const *args,
    netbsd_sched_yield_result_t     *result)
{
	nix_env_t *env = netbsd_us_syscall_get_nix_env(xus);

	LCLog(g_bsd_log, LCLogDebug, 0, "invoked", 0);

	nix_env_set_errno(env, 0);
	*result = nix_rt_sched_yield(env);
	return (nix_env_get_errno(env));
}

int
netbsd___getcwd(
    nix_us_syscall_if_t          *xus,
    nix_monitor_t                *xmon,
    netbsd___getcwd_args_t const *args,
    netbsd___getcwd_result_t     *result)
{
	nix_env_t *env = netbsd_us_syscall_get_nix_env(xus);

	LCLog(g_bsd_log, LCLogDebug, 0, "invoked", 0);

	nix_env_set_errno(env, 0);
	*result = nix_getcwd((char *)args->arg0, args->arg1, env);
	return (nix_env_get_errno(env));
}

int
netbsd_obreak(
    nix_us_syscall_if_t        *xus,
    nix_monitor_t              *xmon,
    netbsd_obreak_args_t const *args,
    netbsd_obreak_result_t     *result)
{
	nix_env_t *env = netbsd_us_syscall_get_nix_env(xus);

	LCLog(g_bsd_log, LCLogDebug, 0, "invoked", 0);

	nix_env_set_errno(env, 0);
	nix_brk((uintmax_t)(uintptr_t)args->arg0, env);
	*result = 0;
	return (nix_env_get_errno(env));
}
int
netbsd_unmount(
    nix_us_syscall_if_t         *xus,
    nix_monitor_t               *xmon,
    netbsd_unmount_args_t const *args,
    netbsd_unmount_result_t     *result)
{
	nix_env_t *env = netbsd_us_syscall_get_nix_env(xus);

	LCLog(g_bsd_log, LCLogDebug, 0, "invoked", 0);

	nix_env_set_errno(env, 0);
	*result = nix_bsd_unmount((char const *)args->arg0, args->arg1, env);
	return (nix_env_get_errno(env));
}
int
netbsd___getlogin(
    nix_us_syscall_if_t            *xus,
    nix_monitor_t                  *xmon,
    netbsd___getlogin_args_t const *args,
    netbsd___getlogin_result_t     *result)
{
	nix_env_t *env = netbsd_us_syscall_get_nix_env(xus);

	LCLog(g_bsd_log, LCLogDebug, 0, "invoked", 0);

	nix_env_set_errno(env, 0);
	*result = nix_bsd_getlogin((char *)args->arg0, args->arg1, env);
	return (nix_env_get_errno(env));
}
int
netbsd___setlogin(
    nix_us_syscall_if_t            *xus,
    nix_monitor_t                  *xmon,
    netbsd___setlogin_args_t const *args,
    netbsd___setlogin_result_t     *result)
{
	nix_env_t *env = netbsd_us_syscall_get_nix_env(xus);

	LCLog(g_bsd_log, LCLogDebug, 0, "invoked", 0);

	nix_env_set_errno(env, 0);
	*result = nix_bsd_setlogin((char const *)args->arg0, env);
	return (nix_env_get_errno(env));
}
int
netbsd___posix_rename(
    nix_us_syscall_if_t                *xus,
    nix_monitor_t                      *xmon,
    netbsd___posix_rename_args_t const *args,
    netbsd___posix_rename_result_t     *result)
{
	nix_env_t *env = netbsd_us_syscall_get_nix_env(xus);

	LCLog(g_bsd_log, LCLogDebug, 0, "invoked", 0);

	nix_env_set_errno(env, 0);
	*result = nix_rename((char const *)args->arg0, (char const *)args->arg1, env);
	return (nix_env_get_errno(env));
}
int
netbsd___posix_chown(
    nix_us_syscall_if_t               *xus,
    nix_monitor_t                     *xmon,
    netbsd___posix_chown_args_t const *args,
    netbsd___posix_chown_result_t     *result)
{
	nix_env_t *env = netbsd_us_syscall_get_nix_env(xus);

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
netbsd___posix_fchown(
    nix_us_syscall_if_t                *xus,
    nix_monitor_t                      *xmon,
    netbsd___posix_fchown_args_t const *args,
    netbsd___posix_fchown_result_t     *result)
{
	nix_env_t *env = netbsd_us_syscall_get_nix_env(xus);

	LCLog(g_bsd_log, LCLogDebug, 0, "invoked", 0);

	nix_env_set_errno(env, 0);
	*result = nix_fchown(args->arg0, args->arg1, args->arg2, env);
	return (nix_env_get_errno(env));
}
int
netbsd___posix_lchown(
    nix_us_syscall_if_t                *xus,
    nix_monitor_t                      *xmon,
    netbsd___posix_lchown_args_t const *args,
    netbsd___posix_lchown_result_t     *result)
{
	nix_env_t *env = netbsd_us_syscall_get_nix_env(xus);

	LCLog(g_bsd_log, LCLogDebug, 0, "invoked", 0);

	nix_env_set_errno(env, 0);
	*result = nix_lchown((char const *)args->arg0, args->arg1, args->arg2, env);
	return (nix_env_get_errno(env));
}

int
netbsd_ovadvise(
    nix_us_syscall_if_t          *xus,
    nix_monitor_t                *xmon,
    netbsd_ovadvise_args_t const *args,
    netbsd_ovadvise_result_t     *result)
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
netbsd_getrandom(
    nix_us_syscall_if_t           *xus,
    nix_monitor_t                 *xmon,
    netbsd_getrandom_args_t const *args,
    netbsd_getrandom_result_t     *result)
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
netbsd___futex(
    nix_us_syscall_if_t         *xus,
    nix_monitor_t               *xmon,
    netbsd___futex_args_t const *args,
    netbsd___futex_result_t     *result)
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
netbsd___futex_set_robust_list(
    nix_us_syscall_if_t                         *xus,
    nix_monitor_t                               *xmon,
    netbsd___futex_set_robust_list_args_t const *args,
    netbsd___futex_set_robust_list_result_t     *result)
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
netbsd___futex_get_robust_list(
    nix_us_syscall_if_t                         *xus,
    nix_monitor_t                               *xmon,
    netbsd___futex_get_robust_list_args_t const *args,
    netbsd___futex_get_robust_list_result_t     *result)
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
netbsd_ntp_adjtime(
    nix_us_syscall_if_t             *xus,
    nix_monitor_t                   *xmon,
    netbsd_ntp_adjtime_args_t const *args,
    netbsd_ntp_adjtime_result_t     *result)
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
netbsd_timerfd_create(
    nix_us_syscall_if_t                *xus,
    nix_monitor_t                      *xmon,
    netbsd_timerfd_create_args_t const *args,
    netbsd_timerfd_create_result_t     *result)
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
netbsd_timerfd_settime(
    nix_us_syscall_if_t                 *xus,
    nix_monitor_t                       *xmon,
    netbsd_timerfd_settime_args_t const *args,
    netbsd_timerfd_settime_result_t     *result)
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
netbsd_timerfd_gettime(
    nix_us_syscall_if_t                 *xus,
    nix_monitor_t                       *xmon,
    netbsd_timerfd_gettime_args_t const *args,
    netbsd_timerfd_gettime_result_t     *result)
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
netbsd_getsockopt2(
    nix_us_syscall_if_t             *xus,
    nix_monitor_t                   *xmon,
    netbsd_getsockopt2_args_t const *args,
    netbsd_getsockopt2_result_t     *result)
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
netbsd_undelete(
    nix_us_syscall_if_t          *xus,
    nix_monitor_t                *xmon,
    netbsd_undelete_args_t const *args,
    netbsd_undelete_result_t     *result)
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
netbsd_semconfig(
    nix_us_syscall_if_t           *xus,
    nix_monitor_t                 *xmon,
    netbsd_semconfig_args_t const *args,
    netbsd_semconfig_result_t     *result)
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
netbsd_timer_create(
    nix_us_syscall_if_t              *xus,
    nix_monitor_t                    *xmon,
    netbsd_timer_create_args_t const *args,
    netbsd_timer_create_result_t     *result)
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
netbsd_timer_delete(
    nix_us_syscall_if_t              *xus,
    nix_monitor_t                    *xmon,
    netbsd_timer_delete_args_t const *args,
    netbsd_timer_delete_result_t     *result)
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
netbsd_timer_getoverrun(
    nix_us_syscall_if_t                  *xus,
    nix_monitor_t                        *xmon,
    netbsd_timer_getoverrun_args_t const *args,
    netbsd_timer_getoverrun_result_t     *result)
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
netbsd_fdatasync(
    nix_us_syscall_if_t           *xus,
    nix_monitor_t                 *xmon,
    netbsd_fdatasync_args_t const *args,
    netbsd_fdatasync_result_t     *result)
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
netbsd_sigqueueinfo(
    nix_us_syscall_if_t              *xus,
    nix_monitor_t                    *xmon,
    netbsd_sigqueueinfo_args_t const *args,
    netbsd_sigqueueinfo_result_t     *result)
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
netbsd_modctl(
    nix_us_syscall_if_t        *xus,
    nix_monitor_t              *xmon,
    netbsd_modctl_args_t const *args,
    netbsd_modctl_result_t     *result)
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
netbsd__ksem_init(
    nix_us_syscall_if_t            *xus,
    nix_monitor_t                  *xmon,
    netbsd__ksem_init_args_t const *args,
    netbsd__ksem_init_result_t     *result)
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
netbsd__ksem_open(
    nix_us_syscall_if_t            *xus,
    nix_monitor_t                  *xmon,
    netbsd__ksem_open_args_t const *args,
    netbsd__ksem_open_result_t     *result)
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
netbsd__ksem_unlink(
    nix_us_syscall_if_t              *xus,
    nix_monitor_t                    *xmon,
    netbsd__ksem_unlink_args_t const *args,
    netbsd__ksem_unlink_result_t     *result)
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
netbsd__ksem_close(
    nix_us_syscall_if_t             *xus,
    nix_monitor_t                   *xmon,
    netbsd__ksem_close_args_t const *args,
    netbsd__ksem_close_result_t     *result)
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
netbsd__ksem_post(
    nix_us_syscall_if_t            *xus,
    nix_monitor_t                  *xmon,
    netbsd__ksem_post_args_t const *args,
    netbsd__ksem_post_result_t     *result)
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
netbsd__ksem_wait(
    nix_us_syscall_if_t            *xus,
    nix_monitor_t                  *xmon,
    netbsd__ksem_wait_args_t const *args,
    netbsd__ksem_wait_result_t     *result)
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
netbsd__ksem_trywait(
    nix_us_syscall_if_t               *xus,
    nix_monitor_t                     *xmon,
    netbsd__ksem_trywait_args_t const *args,
    netbsd__ksem_trywait_result_t     *result)
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
netbsd__ksem_getvalue(
    nix_us_syscall_if_t                *xus,
    nix_monitor_t                      *xmon,
    netbsd__ksem_getvalue_args_t const *args,
    netbsd__ksem_getvalue_result_t     *result)
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
netbsd__ksem_destroy(
    nix_us_syscall_if_t               *xus,
    nix_monitor_t                     *xmon,
    netbsd__ksem_destroy_args_t const *args,
    netbsd__ksem_destroy_result_t     *result)
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
netbsd__ksem_timedwait(
    nix_us_syscall_if_t                 *xus,
    nix_monitor_t                       *xmon,
    netbsd__ksem_timedwait_args_t const *args,
    netbsd__ksem_timedwait_result_t     *result)
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
netbsd_mq_open(
    nix_us_syscall_if_t         *xus,
    nix_monitor_t               *xmon,
    netbsd_mq_open_args_t const *args,
    netbsd_mq_open_result_t     *result)
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
netbsd_mq_close(
    nix_us_syscall_if_t          *xus,
    nix_monitor_t                *xmon,
    netbsd_mq_close_args_t const *args,
    netbsd_mq_close_result_t     *result)
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
netbsd_mq_unlink(
    nix_us_syscall_if_t           *xus,
    nix_monitor_t                 *xmon,
    netbsd_mq_unlink_args_t const *args,
    netbsd_mq_unlink_result_t     *result)
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
netbsd_mq_getattr(
    nix_us_syscall_if_t            *xus,
    nix_monitor_t                  *xmon,
    netbsd_mq_getattr_args_t const *args,
    netbsd_mq_getattr_result_t     *result)
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
netbsd_mq_setattr(
    nix_us_syscall_if_t            *xus,
    nix_monitor_t                  *xmon,
    netbsd_mq_setattr_args_t const *args,
    netbsd_mq_setattr_result_t     *result)
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
netbsd_mq_notify(
    nix_us_syscall_if_t           *xus,
    nix_monitor_t                 *xmon,
    netbsd_mq_notify_args_t const *args,
    netbsd_mq_notify_result_t     *result)
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
netbsd_mq_send(
    nix_us_syscall_if_t         *xus,
    nix_monitor_t               *xmon,
    netbsd_mq_send_args_t const *args,
    netbsd_mq_send_result_t     *result)
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
netbsd_mq_receive(
    nix_us_syscall_if_t            *xus,
    nix_monitor_t                  *xmon,
    netbsd_mq_receive_args_t const *args,
    netbsd_mq_receive_result_t     *result)
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
netbsd_eventfd(
    nix_us_syscall_if_t         *xus,
    nix_monitor_t               *xmon,
    netbsd_eventfd_args_t const *args,
    netbsd_eventfd_result_t     *result)
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
netbsd_lchmod(
    nix_us_syscall_if_t        *xus,
    nix_monitor_t              *xmon,
    netbsd_lchmod_args_t const *args,
    netbsd_lchmod_result_t     *result)
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
netbsd___clone(
    nix_us_syscall_if_t         *xus,
    nix_monitor_t               *xmon,
    netbsd___clone_args_t const *args,
    netbsd___clone_result_t     *result)
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
netbsd_fktrace(
    nix_us_syscall_if_t         *xus,
    nix_monitor_t               *xmon,
    netbsd_fktrace_args_t const *args,
    netbsd_fktrace_result_t     *result)
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
netbsd_fchroot(
    nix_us_syscall_if_t         *xus,
    nix_monitor_t               *xmon,
    netbsd_fchroot_args_t const *args,
    netbsd_fchroot_result_t     *result)
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
netbsd_lchflags(
    nix_us_syscall_if_t          *xus,
    nix_monitor_t                *xmon,
    netbsd_lchflags_args_t const *args,
    netbsd_lchflags_result_t     *result)
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
netbsd_utrace(
    nix_us_syscall_if_t        *xus,
    nix_monitor_t              *xmon,
    netbsd_utrace_args_t const *args,
    netbsd_utrace_result_t     *result)
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
netbsd_getcontext(
    nix_us_syscall_if_t            *xus,
    nix_monitor_t                  *xmon,
    netbsd_getcontext_args_t const *args,
    netbsd_getcontext_result_t     *result)
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
netbsd_setcontext(
    nix_us_syscall_if_t            *xus,
    nix_monitor_t                  *xmon,
    netbsd_setcontext_args_t const *args,
    netbsd_setcontext_result_t     *result)
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
netbsd__lwp_create(
    nix_us_syscall_if_t             *xus,
    nix_monitor_t                   *xmon,
    netbsd__lwp_create_args_t const *args,
    netbsd__lwp_create_result_t     *result)
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
netbsd__lwp_exit(
    nix_us_syscall_if_t           *xus,
    nix_monitor_t                 *xmon,
    netbsd__lwp_exit_args_t const *args,
    netbsd__lwp_exit_result_t     *result)
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
netbsd__lwp_self(
    nix_us_syscall_if_t           *xus,
    nix_monitor_t                 *xmon,
    netbsd__lwp_self_args_t const *args,
    netbsd__lwp_self_result_t     *result)
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
netbsd__lwp_wait(
    nix_us_syscall_if_t           *xus,
    nix_monitor_t                 *xmon,
    netbsd__lwp_wait_args_t const *args,
    netbsd__lwp_wait_result_t     *result)
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
netbsd__lwp_suspend(
    nix_us_syscall_if_t              *xus,
    nix_monitor_t                    *xmon,
    netbsd__lwp_suspend_args_t const *args,
    netbsd__lwp_suspend_result_t     *result)
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
netbsd__lwp_continue(
    nix_us_syscall_if_t               *xus,
    nix_monitor_t                     *xmon,
    netbsd__lwp_continue_args_t const *args,
    netbsd__lwp_continue_result_t     *result)
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
netbsd__lwp_wakeup(
    nix_us_syscall_if_t             *xus,
    nix_monitor_t                   *xmon,
    netbsd__lwp_wakeup_args_t const *args,
    netbsd__lwp_wakeup_result_t     *result)
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
netbsd__lwp_getprivate(
    nix_us_syscall_if_t                 *xus,
    nix_monitor_t                       *xmon,
    netbsd__lwp_getprivate_args_t const *args,
    netbsd__lwp_getprivate_result_t     *result)
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
netbsd__lwp_setprivate(
    nix_us_syscall_if_t                 *xus,
    nix_monitor_t                       *xmon,
    netbsd__lwp_setprivate_args_t const *args,
    netbsd__lwp_setprivate_result_t     *result)
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
netbsd__lwp_kill(
    nix_us_syscall_if_t           *xus,
    nix_monitor_t                 *xmon,
    netbsd__lwp_kill_args_t const *args,
    netbsd__lwp_kill_result_t     *result)
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
netbsd__lwp_detach(
    nix_us_syscall_if_t             *xus,
    nix_monitor_t                   *xmon,
    netbsd__lwp_detach_args_t const *args,
    netbsd__lwp_detach_result_t     *result)
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
netbsd__lwp_unpark(
    nix_us_syscall_if_t             *xus,
    nix_monitor_t                   *xmon,
    netbsd__lwp_unpark_args_t const *args,
    netbsd__lwp_unpark_result_t     *result)
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
netbsd__lwp_unpark_all(
    nix_us_syscall_if_t                 *xus,
    nix_monitor_t                       *xmon,
    netbsd__lwp_unpark_all_args_t const *args,
    netbsd__lwp_unpark_all_result_t     *result)
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
netbsd__lwp_setname(
    nix_us_syscall_if_t              *xus,
    nix_monitor_t                    *xmon,
    netbsd__lwp_setname_args_t const *args,
    netbsd__lwp_setname_result_t     *result)
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
netbsd__lwp_getname(
    nix_us_syscall_if_t              *xus,
    nix_monitor_t                    *xmon,
    netbsd__lwp_getname_args_t const *args,
    netbsd__lwp_getname_result_t     *result)
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
netbsd__lwp_ctl(
    nix_us_syscall_if_t          *xus,
    nix_monitor_t                *xmon,
    netbsd__lwp_ctl_args_t const *args,
    netbsd__lwp_ctl_result_t     *result)
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
netbsd___sigaction_sigtramp(
    nix_us_syscall_if_t                      *xus,
    nix_monitor_t                            *xmon,
    netbsd___sigaction_sigtramp_args_t const *args,
    netbsd___sigaction_sigtramp_result_t     *result)
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
netbsd_rasctl(
    nix_us_syscall_if_t        *xus,
    nix_monitor_t              *xmon,
    netbsd_rasctl_args_t const *args,
    netbsd_rasctl_result_t     *result)
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
netbsd__sched_setparam(
    nix_us_syscall_if_t                 *xus,
    nix_monitor_t                       *xmon,
    netbsd__sched_setparam_args_t const *args,
    netbsd__sched_setparam_result_t     *result)
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
netbsd__sched_getparam(
    nix_us_syscall_if_t                 *xus,
    nix_monitor_t                       *xmon,
    netbsd__sched_getparam_args_t const *args,
    netbsd__sched_getparam_result_t     *result)
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
netbsd__sched_setaffinity(
    nix_us_syscall_if_t                    *xus,
    nix_monitor_t                          *xmon,
    netbsd__sched_setaffinity_args_t const *args,
    netbsd__sched_setaffinity_result_t     *result)
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
netbsd__sched_getaffinity(
    nix_us_syscall_if_t                    *xus,
    nix_monitor_t                          *xmon,
    netbsd__sched_getaffinity_args_t const *args,
    netbsd__sched_getaffinity_result_t     *result)
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
netbsd__sched_protect(
    nix_us_syscall_if_t                *xus,
    nix_monitor_t                      *xmon,
    netbsd__sched_protect_args_t const *args,
    netbsd__sched_protect_result_t     *result)
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
netbsd_fsync_range(
    nix_us_syscall_if_t             *xus,
    nix_monitor_t                   *xmon,
    netbsd_fsync_range_args_t const *args,
    netbsd_fsync_range_result_t     *result)
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
netbsd_uuidgen(
    nix_us_syscall_if_t         *xus,
    nix_monitor_t               *xmon,
    netbsd_uuidgen_args_t const *args,
    netbsd_uuidgen_result_t     *result)
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
netbsd_extattrctl(
    nix_us_syscall_if_t            *xus,
    nix_monitor_t                  *xmon,
    netbsd_extattrctl_args_t const *args,
    netbsd_extattrctl_result_t     *result)
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
netbsd_extattr_set_file(
    nix_us_syscall_if_t                  *xus,
    nix_monitor_t                        *xmon,
    netbsd_extattr_set_file_args_t const *args,
    netbsd_extattr_set_file_result_t     *result)
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
netbsd_extattr_get_file(
    nix_us_syscall_if_t                  *xus,
    nix_monitor_t                        *xmon,
    netbsd_extattr_get_file_args_t const *args,
    netbsd_extattr_get_file_result_t     *result)
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
netbsd_extattr_delete_file(
    nix_us_syscall_if_t                     *xus,
    nix_monitor_t                           *xmon,
    netbsd_extattr_delete_file_args_t const *args,
    netbsd_extattr_delete_file_result_t     *result)
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
netbsd_extattr_set_fd(
    nix_us_syscall_if_t                *xus,
    nix_monitor_t                      *xmon,
    netbsd_extattr_set_fd_args_t const *args,
    netbsd_extattr_set_fd_result_t     *result)
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
netbsd_extattr_get_fd(
    nix_us_syscall_if_t                *xus,
    nix_monitor_t                      *xmon,
    netbsd_extattr_get_fd_args_t const *args,
    netbsd_extattr_get_fd_result_t     *result)
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
netbsd_extattr_delete_fd(
    nix_us_syscall_if_t                   *xus,
    nix_monitor_t                         *xmon,
    netbsd_extattr_delete_fd_args_t const *args,
    netbsd_extattr_delete_fd_result_t     *result)
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
netbsd_extattr_set_link(
    nix_us_syscall_if_t                  *xus,
    nix_monitor_t                        *xmon,
    netbsd_extattr_set_link_args_t const *args,
    netbsd_extattr_set_link_result_t     *result)
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
netbsd_extattr_get_link(
    nix_us_syscall_if_t                  *xus,
    nix_monitor_t                        *xmon,
    netbsd_extattr_get_link_args_t const *args,
    netbsd_extattr_get_link_result_t     *result)
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
netbsd_extattr_delete_link(
    nix_us_syscall_if_t                     *xus,
    nix_monitor_t                           *xmon,
    netbsd_extattr_delete_link_args_t const *args,
    netbsd_extattr_delete_link_result_t     *result)
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
netbsd_extattr_list_fd(
    nix_us_syscall_if_t                 *xus,
    nix_monitor_t                       *xmon,
    netbsd_extattr_list_fd_args_t const *args,
    netbsd_extattr_list_fd_result_t     *result)
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
netbsd_extattr_list_file(
    nix_us_syscall_if_t                   *xus,
    nix_monitor_t                         *xmon,
    netbsd_extattr_list_file_args_t const *args,
    netbsd_extattr_list_file_result_t     *result)
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
netbsd_extattr_list_link(
    nix_us_syscall_if_t                   *xus,
    nix_monitor_t                         *xmon,
    netbsd_extattr_list_link_args_t const *args,
    netbsd_extattr_list_link_result_t     *result)
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
netbsd_setxattr(
    nix_us_syscall_if_t          *xus,
    nix_monitor_t                *xmon,
    netbsd_setxattr_args_t const *args,
    netbsd_setxattr_result_t     *result)
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
netbsd_lsetxattr(
    nix_us_syscall_if_t           *xus,
    nix_monitor_t                 *xmon,
    netbsd_lsetxattr_args_t const *args,
    netbsd_lsetxattr_result_t     *result)
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
netbsd_fsetxattr(
    nix_us_syscall_if_t           *xus,
    nix_monitor_t                 *xmon,
    netbsd_fsetxattr_args_t const *args,
    netbsd_fsetxattr_result_t     *result)
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
netbsd_getxattr(
    nix_us_syscall_if_t          *xus,
    nix_monitor_t                *xmon,
    netbsd_getxattr_args_t const *args,
    netbsd_getxattr_result_t     *result)
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
netbsd_lgetxattr(
    nix_us_syscall_if_t           *xus,
    nix_monitor_t                 *xmon,
    netbsd_lgetxattr_args_t const *args,
    netbsd_lgetxattr_result_t     *result)
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
netbsd_fgetxattr(
    nix_us_syscall_if_t           *xus,
    nix_monitor_t                 *xmon,
    netbsd_fgetxattr_args_t const *args,
    netbsd_fgetxattr_result_t     *result)
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
netbsd_listxattr(
    nix_us_syscall_if_t           *xus,
    nix_monitor_t                 *xmon,
    netbsd_listxattr_args_t const *args,
    netbsd_listxattr_result_t     *result)
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
netbsd_llistxattr(
    nix_us_syscall_if_t            *xus,
    nix_monitor_t                  *xmon,
    netbsd_llistxattr_args_t const *args,
    netbsd_llistxattr_result_t     *result)
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
netbsd_flistxattr(
    nix_us_syscall_if_t            *xus,
    nix_monitor_t                  *xmon,
    netbsd_flistxattr_args_t const *args,
    netbsd_flistxattr_result_t     *result)
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
netbsd_removexattr(
    nix_us_syscall_if_t             *xus,
    nix_monitor_t                   *xmon,
    netbsd_removexattr_args_t const *args,
    netbsd_removexattr_result_t     *result)
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
netbsd_lremovexattr(
    nix_us_syscall_if_t              *xus,
    nix_monitor_t                    *xmon,
    netbsd_lremovexattr_args_t const *args,
    netbsd_lremovexattr_result_t     *result)
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
netbsd_fremovexattr(
    nix_us_syscall_if_t              *xus,
    nix_monitor_t                    *xmon,
    netbsd_fremovexattr_args_t const *args,
    netbsd_fremovexattr_result_t     *result)
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
netbsd_aio_cancel(
    nix_us_syscall_if_t            *xus,
    nix_monitor_t                  *xmon,
    netbsd_aio_cancel_args_t const *args,
    netbsd_aio_cancel_result_t     *result)
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
netbsd_aio_error(
    nix_us_syscall_if_t           *xus,
    nix_monitor_t                 *xmon,
    netbsd_aio_error_args_t const *args,
    netbsd_aio_error_result_t     *result)
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
netbsd_aio_fsync(
    nix_us_syscall_if_t           *xus,
    nix_monitor_t                 *xmon,
    netbsd_aio_fsync_args_t const *args,
    netbsd_aio_fsync_result_t     *result)
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
netbsd_aio_read(
    nix_us_syscall_if_t          *xus,
    nix_monitor_t                *xmon,
    netbsd_aio_read_args_t const *args,
    netbsd_aio_read_result_t     *result)
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
netbsd_aio_return(
    nix_us_syscall_if_t            *xus,
    nix_monitor_t                  *xmon,
    netbsd_aio_return_args_t const *args,
    netbsd_aio_return_result_t     *result)
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
netbsd_aio_write(
    nix_us_syscall_if_t           *xus,
    nix_monitor_t                 *xmon,
    netbsd_aio_write_args_t const *args,
    netbsd_aio_write_result_t     *result)
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
netbsd_lio_listio(
    nix_us_syscall_if_t            *xus,
    nix_monitor_t                  *xmon,
    netbsd_lio_listio_args_t const *args,
    netbsd_lio_listio_result_t     *result)
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
netbsd_mremap(
    nix_us_syscall_if_t        *xus,
    nix_monitor_t              *xmon,
    netbsd_mremap_args_t const *args,
    netbsd_mremap_result_t     *result)
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
netbsd_pset_create(
    nix_us_syscall_if_t             *xus,
    nix_monitor_t                   *xmon,
    netbsd_pset_create_args_t const *args,
    netbsd_pset_create_result_t     *result)
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
netbsd_pset_destroy(
    nix_us_syscall_if_t              *xus,
    nix_monitor_t                    *xmon,
    netbsd_pset_destroy_args_t const *args,
    netbsd_pset_destroy_result_t     *result)
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
netbsd_pset_assign(
    nix_us_syscall_if_t             *xus,
    nix_monitor_t                   *xmon,
    netbsd_pset_assign_args_t const *args,
    netbsd_pset_assign_result_t     *result)
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
netbsd__pset_bind(
    nix_us_syscall_if_t            *xus,
    nix_monitor_t                  *xmon,
    netbsd__pset_bind_args_t const *args,
    netbsd__pset_bind_result_t     *result)
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
netbsd_pipe2(
    nix_us_syscall_if_t       *xus,
    nix_monitor_t             *xmon,
    netbsd_pipe2_args_t const *args,
    netbsd_pipe2_result_t     *result)
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
netbsd_dup3(
    nix_us_syscall_if_t      *xus,
    nix_monitor_t            *xmon,
    netbsd_dup3_args_t const *args,
    netbsd_dup3_result_t     *result)
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
netbsd_kqueue1(
    nix_us_syscall_if_t         *xus,
    nix_monitor_t               *xmon,
    netbsd_kqueue1_args_t const *args,
    netbsd_kqueue1_result_t     *result)
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
netbsd_paccept(
    nix_us_syscall_if_t         *xus,
    nix_monitor_t               *xmon,
    netbsd_paccept_args_t const *args,
    netbsd_paccept_result_t     *result)
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
netbsd_linkat(
    nix_us_syscall_if_t        *xus,
    nix_monitor_t              *xmon,
    netbsd_linkat_args_t const *args,
    netbsd_linkat_result_t     *result)
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
netbsd_renameat(
    nix_us_syscall_if_t          *xus,
    nix_monitor_t                *xmon,
    netbsd_renameat_args_t const *args,
    netbsd_renameat_result_t     *result)
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
netbsd_mkfifoat(
    nix_us_syscall_if_t          *xus,
    nix_monitor_t                *xmon,
    netbsd_mkfifoat_args_t const *args,
    netbsd_mkfifoat_result_t     *result)
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
netbsd_mknodat(
    nix_us_syscall_if_t         *xus,
    nix_monitor_t               *xmon,
    netbsd_mknodat_args_t const *args,
    netbsd_mknodat_result_t     *result)
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
netbsd_mkdirat(
    nix_us_syscall_if_t         *xus,
    nix_monitor_t               *xmon,
    netbsd_mkdirat_args_t const *args,
    netbsd_mkdirat_result_t     *result)
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
netbsd_faccessat(
    nix_us_syscall_if_t           *xus,
    nix_monitor_t                 *xmon,
    netbsd_faccessat_args_t const *args,
    netbsd_faccessat_result_t     *result)
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
netbsd_fchmodat(
    nix_us_syscall_if_t          *xus,
    nix_monitor_t                *xmon,
    netbsd_fchmodat_args_t const *args,
    netbsd_fchmodat_result_t     *result)
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
netbsd_fchownat(
    nix_us_syscall_if_t          *xus,
    nix_monitor_t                *xmon,
    netbsd_fchownat_args_t const *args,
    netbsd_fchownat_result_t     *result)
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
netbsd_fexecve(
    nix_us_syscall_if_t         *xus,
    nix_monitor_t               *xmon,
    netbsd_fexecve_args_t const *args,
    netbsd_fexecve_result_t     *result)
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
netbsd_fstatat(
    nix_us_syscall_if_t         *xus,
    nix_monitor_t               *xmon,
    netbsd_fstatat_args_t const *args,
    netbsd_fstatat_result_t     *result)
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
netbsd_utimensat(
    nix_us_syscall_if_t           *xus,
    nix_monitor_t                 *xmon,
    netbsd_utimensat_args_t const *args,
    netbsd_utimensat_result_t     *result)
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
netbsd_openat(
    nix_us_syscall_if_t        *xus,
    nix_monitor_t              *xmon,
    netbsd_openat_args_t const *args,
    netbsd_openat_result_t     *result)
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
netbsd_readlinkat(
    nix_us_syscall_if_t            *xus,
    nix_monitor_t                  *xmon,
    netbsd_readlinkat_args_t const *args,
    netbsd_readlinkat_result_t     *result)
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
netbsd_symlinkat(
    nix_us_syscall_if_t           *xus,
    nix_monitor_t                 *xmon,
    netbsd_symlinkat_args_t const *args,
    netbsd_symlinkat_result_t     *result)
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
netbsd_unlinkat(
    nix_us_syscall_if_t          *xus,
    nix_monitor_t                *xmon,
    netbsd_unlinkat_args_t const *args,
    netbsd_unlinkat_result_t     *result)
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
netbsd_futimens(
    nix_us_syscall_if_t          *xus,
    nix_monitor_t                *xmon,
    netbsd_futimens_args_t const *args,
    netbsd_futimens_result_t     *result)
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
netbsd___quotactl(
    nix_us_syscall_if_t            *xus,
    nix_monitor_t                  *xmon,
    netbsd___quotactl_args_t const *args,
    netbsd___quotactl_result_t     *result)
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
netbsd_recvmmsg(
    nix_us_syscall_if_t          *xus,
    nix_monitor_t                *xmon,
    netbsd_recvmmsg_args_t const *args,
    netbsd_recvmmsg_result_t     *result)
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
netbsd_sendmmsg(
    nix_us_syscall_if_t          *xus,
    nix_monitor_t                *xmon,
    netbsd_sendmmsg_args_t const *args,
    netbsd_sendmmsg_result_t     *result)
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
netbsd_fdiscard(
    nix_us_syscall_if_t          *xus,
    nix_monitor_t                *xmon,
    netbsd_fdiscard_args_t const *args,
    netbsd_fdiscard_result_t     *result)
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
netbsd_wait6(
    nix_us_syscall_if_t       *xus,
    nix_monitor_t             *xmon,
    netbsd_wait6_args_t const *args,
    netbsd_wait6_result_t     *result)
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
netbsd_clock_getcpuclockid2(
    nix_us_syscall_if_t                      *xus,
    nix_monitor_t                            *xmon,
    netbsd_clock_getcpuclockid2_args_t const *args,
    netbsd_clock_getcpuclockid2_result_t     *result)
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
netbsd___acl_get_link(
    nix_us_syscall_if_t                *xus,
    nix_monitor_t                      *xmon,
    netbsd___acl_get_link_args_t const *args,
    netbsd___acl_get_link_result_t     *result)
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
netbsd___acl_set_link(
    nix_us_syscall_if_t                *xus,
    nix_monitor_t                      *xmon,
    netbsd___acl_set_link_args_t const *args,
    netbsd___acl_set_link_result_t     *result)
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
netbsd___acl_delete_link(
    nix_us_syscall_if_t                   *xus,
    nix_monitor_t                         *xmon,
    netbsd___acl_delete_link_args_t const *args,
    netbsd___acl_delete_link_result_t     *result)
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
netbsd___acl_aclcheck_link(
    nix_us_syscall_if_t                     *xus,
    nix_monitor_t                           *xmon,
    netbsd___acl_aclcheck_link_args_t const *args,
    netbsd___acl_aclcheck_link_result_t     *result)
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
netbsd___acl_get_file(
    nix_us_syscall_if_t                *xus,
    nix_monitor_t                      *xmon,
    netbsd___acl_get_file_args_t const *args,
    netbsd___acl_get_file_result_t     *result)
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
netbsd___acl_set_file(
    nix_us_syscall_if_t                *xus,
    nix_monitor_t                      *xmon,
    netbsd___acl_set_file_args_t const *args,
    netbsd___acl_set_file_result_t     *result)
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
netbsd___acl_get_fd(
    nix_us_syscall_if_t              *xus,
    nix_monitor_t                    *xmon,
    netbsd___acl_get_fd_args_t const *args,
    netbsd___acl_get_fd_result_t     *result)
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
netbsd___acl_set_fd(
    nix_us_syscall_if_t              *xus,
    nix_monitor_t                    *xmon,
    netbsd___acl_set_fd_args_t const *args,
    netbsd___acl_set_fd_result_t     *result)
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
netbsd___acl_delete_file(
    nix_us_syscall_if_t                   *xus,
    nix_monitor_t                         *xmon,
    netbsd___acl_delete_file_args_t const *args,
    netbsd___acl_delete_file_result_t     *result)
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
netbsd___acl_delete_fd(
    nix_us_syscall_if_t                 *xus,
    nix_monitor_t                       *xmon,
    netbsd___acl_delete_fd_args_t const *args,
    netbsd___acl_delete_fd_result_t     *result)
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
netbsd___acl_aclcheck_file(
    nix_us_syscall_if_t                     *xus,
    nix_monitor_t                           *xmon,
    netbsd___acl_aclcheck_file_args_t const *args,
    netbsd___acl_aclcheck_file_result_t     *result)
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
netbsd___acl_aclcheck_fd(
    nix_us_syscall_if_t                   *xus,
    nix_monitor_t                         *xmon,
    netbsd___acl_aclcheck_fd_args_t const *args,
    netbsd___acl_aclcheck_fd_result_t     *result)
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
netbsd_lpathconf(
    nix_us_syscall_if_t           *xus,
    nix_monitor_t                 *xmon,
    netbsd_lpathconf_args_t const *args,
    netbsd_lpathconf_result_t     *result)
{
	(void)xus;
	(void)xmon;
	(void)args;
	(void)result;

	LCLog(g_bsd_log, LCLogWarning, 0,
	      "unimplemented NetBSD 10.1 syscall `lpathconf'", 0);

	return (ENOSYS);
}
