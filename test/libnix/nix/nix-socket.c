#include "nix-config.h"

#include <string.h>
#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h> /* socklen_t, sockaddr_storage */
#include <errno.h>
#else
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/un.h>
#ifdef HAVE_NETINET_IN_SYSTM_H
#include <netinet/in_systm.h> /* BSD n_time/n_long types; unused here, absent on Haiku's base headers */
#endif
#include <netinet/in.h>
#include <unistd.h>
#ifdef HAVE_UCRED_H
#include <ucred.h>
#endif
#endif

#include "nix.h"
#include "nix-fd.h"
#include "nix-host.h"

#ifndef min
#define min(a, b) ((a) < (b) ? (a) : (b))
#endif

extern void *g_LCLogImpl;

#ifdef _WIN32
/* Winsock differs from BSD sockets in three ways the shared code below assumes away: it must be
   started (WSAStartup) before use, it reports errors via WSAGetLastError rather than errno, and
   AF_UNIX/socketpair/getpeereid have no analog. Bridge the first two here; the AF_UNIX paths are
   guarded out. The winsock calls are wrapped (then macro-aliased to their POSIX names) so the
   shared bodies need no per-call edits; the wrappers are defined before the #defines, so they
   reach the real winsock functions without recursing. */
static int
nix__wsa_to_errno(int w)
{
	switch (w) {
	case WSAEWOULDBLOCK:
		return EAGAIN;
	case WSAEINTR:
		return EINTR;
	case WSAEBADF:
		return EBADF;
	case WSAEACCES:
		return EACCES;
	case WSAEFAULT:
		return EFAULT;
	case WSAEINVAL:
		return EINVAL;
	case WSAEMFILE:
		return EMFILE;
	case WSAENOTSOCK:
		return EBADF;
	case WSAEADDRINUSE:
		return EADDRINUSE;
	case WSAEADDRNOTAVAIL:
		return EADDRNOTAVAIL;
	case WSAECONNREFUSED:
		return ECONNREFUSED;
	case WSAENOTCONN:
		return ENOTCONN;
	case WSAECONNRESET:
		return ECONNRESET;
	case WSAETIMEDOUT:
		return ETIMEDOUT;
	default:
		return EIO;
	}
}

static void
nix__wsa_ensure(void)
{
	static int started = 0;
	if (!started) {
		WSADATA wsa;
		WSAStartup(MAKEWORD(2, 2), &wsa); /* idempotent enough for a single-process emulator */
		started = 1;
	}
}

static __inline int
nix_w_socket(int a, int b, int c)
{
	int r = (int)socket(a, b, c);
	if (r < 0)
		errno = nix__wsa_to_errno(WSAGetLastError());
	return r;
}
static __inline int
nix_w_bind(int s, struct sockaddr const *a, int l)
{
	int r = bind((SOCKET)s, a, l);
	if (r != 0)
		errno = nix__wsa_to_errno(WSAGetLastError());
	return r;
}
static __inline int
nix_w_connect(int s, struct sockaddr const *a, int l)
{
	int r = connect((SOCKET)s, a, l);
	if (r != 0)
		errno = nix__wsa_to_errno(WSAGetLastError());
	return r;
}
static __inline int
nix_w_listen(int s, int b)
{
	int r = listen((SOCKET)s, b);
	if (r != 0)
		errno = nix__wsa_to_errno(WSAGetLastError());
	return r;
}
static __inline int
nix_w_accept(int s, struct sockaddr *a, int *l)
{
	int r = (int)accept((SOCKET)s, a, l);
	if (r < 0)
		errno = nix__wsa_to_errno(WSAGetLastError());
	return r;
}
static __inline int
nix_w_shutdown(int s, int h)
{
	int r = shutdown((SOCKET)s, h);
	if (r != 0)
		errno = nix__wsa_to_errno(WSAGetLastError());
	return r;
}
static __inline int
nix_w_getpeername(int s, struct sockaddr *a, int *l)
{
	int r = getpeername((SOCKET)s, a, l);
	if (r != 0)
		errno = nix__wsa_to_errno(WSAGetLastError());
	return r;
}
static __inline int
nix_w_getsockname(int s, struct sockaddr *a, int *l)
{
	int r = getsockname((SOCKET)s, a, l);
	if (r != 0)
		errno = nix__wsa_to_errno(WSAGetLastError());
	return r;
}
static __inline int
nix_w_recvfrom(int s, void *b, int n, int f, struct sockaddr *a, int *l)
{
	int r = recvfrom((SOCKET)s, (char *)b, n, f, a, l);
	if (r < 0)
		errno = nix__wsa_to_errno(WSAGetLastError());
	return r;
}
static __inline int
nix_w_sendto(int s, void const *b, int n, int f, struct sockaddr const *a, int l)
{
	int r = sendto((SOCKET)s, (char const *)b, n, f, a, l);
	if (r < 0)
		errno = nix__wsa_to_errno(WSAGetLastError());
	return r;
}

#define socket(a, b, c)            nix_w_socket((a), (b), (c))
#define bind(s, a, l)              nix_w_bind((s), (a), (l))
#define connect(s, a, l)           nix_w_connect((s), (a), (l))
#define listen(s, b)               nix_w_listen((s), (b))
#define accept(s, a, l)            nix_w_accept((s), (a), (l))
#define shutdown(s, h)             nix_w_shutdown((s), (h))
#define getpeername(s, a, l)       nix_w_getpeername((s), (a), (l))
#define getsockname(s, a, l)       nix_w_getsockname((s), (a), (l))
#define recvfrom(s, b, n, f, a, l) nix_w_recvfrom((s), (b), (n), (f), (a), (l))
#define sendto(s, b, n, f, a, l)   nix_w_sendto((s), (b), (n), (f), (a), (l))
#endif /* _WIN32 */

static void
nix_sockaddr_in_to_sockaddr_in(struct nix_sockaddr_in const *in,
                               struct sockaddr_in           *out)
{
#if defined(__APPLE__) || defined(__FreeBSD__) || defined(__NetBSD__) || defined(__OpenBSD__) || defined(__DragonFlyBSD__)
	out->sin_len = sizeof(*out);
#endif
	out->sin_family = in->sin_family;
	out->sin_port = in->sin_port;
	out->sin_addr.s_addr = in->sin_addr;
}

static void
sockaddr_in_to_nix_sockaddr_in(struct sockaddr_in const *in,
                               struct nix_sockaddr_in   *out)
{
#if defined(__APPLE__) || defined(__FreeBSD__) || defined(__NetBSD__) || defined(__OpenBSD__) || defined(__DragonFlyBSD__)
	LCAssert(g_LCLogImpl, in->sin_len == sizeof(*in));
#endif
	out->sin_family = in->sin_family;
	out->sin_port = in->sin_port;
	out->sin_addr = in->sin_addr.s_addr;
}

#ifndef _WIN32 /* AF_UNIX (struct sockaddr_un) has no win32 analog */
static void
nix_sockaddr_un_to_sockaddr_un(struct nix_sockaddr_un const *in,
                               struct sockaddr_un           *out)
{
#if defined(__APPLE__) || defined(__FreeBSD__) || defined(__NetBSD__) || defined(__OpenBSD__) || defined(__DragonFlyBSD__)
	// out->sun_len         = sizeof (*out);
#endif
	out->sun_family = in->sun_family;
	strncpy(out->sun_path, in->sun_path, min(sizeof(in->sun_path), sizeof(out->sun_path)));
}

static void
sockaddr_un_to_nix_sockaddr_un(struct sockaddr_un const *in,
                               struct nix_sockaddr_un   *out)
{
#if defined(__APPLE__) || defined(__FreeBSD__) || defined(__NetBSD__) || defined(__OpenBSD__) || defined(__DragonFlyBSD__)
	LCAssert(g_LCLogImpl, in->sun_len == sizeof(*in));
#endif
	out->sun_family = in->sun_family;
	strncpy(out->sun_path, in->sun_path, min(sizeof(in->sun_path), sizeof(out->sun_path)));
}
#endif /* !_WIN32 -- AF_UNIX helpers */

static int
nix_sockaddr_to_sockaddr(struct nix_sockaddr const *in,
                         nix_socklen_t              inlen,
                         struct sockaddr           *out,
                         socklen_t                 *outlen)
{
	switch (in->sa_family) {
#ifndef _WIN32
	case NIX_AF_UNIX:
		LCAssert(g_LCLogImpl, inlen == sizeof(struct nix_sockaddr_un));
		nix_sockaddr_un_to_sockaddr_un((struct nix_sockaddr_un const *)in,
		                               (struct sockaddr_un *)out);
		*outlen = sizeof(struct sockaddr_un);
		return (1);
#endif

	case NIX_AF_INET:
		LCAssert(g_LCLogImpl, inlen == sizeof(struct nix_sockaddr_in));
		nix_sockaddr_in_to_sockaddr_in((struct nix_sockaddr_in const *)in,
		                               (struct sockaddr_in *)out);
		*outlen = sizeof(struct sockaddr_in);
		return (1);
	}

	return (0);
}

static int
sockaddr_to_nix_sockaddr(struct sockaddr const *in,
                         socklen_t              inlen,
                         struct nix_sockaddr   *out,
                         nix_socklen_t         *outlen)
{
	switch (in->sa_family) {
#ifndef _WIN32
	case NIX_AF_UNIX:
		LCAssert(g_LCLogImpl, inlen >= sizeof(struct sockaddr_un));
		LCAssert(g_LCLogImpl, *outlen >= sizeof(struct nix_sockaddr_un));
		sockaddr_un_to_nix_sockaddr_un((struct sockaddr_un const *)in,
		                               (struct nix_sockaddr_un *)out);
		*outlen = sizeof(struct nix_sockaddr_un);
		return (1);
#endif

	case NIX_AF_INET:
		LCAssert(g_LCLogImpl, inlen >= sizeof(struct sockaddr_in));
		LCAssert(g_LCLogImpl, *outlen >= sizeof(struct nix_sockaddr_in));
		sockaddr_in_to_nix_sockaddr_in((struct sockaddr_in const *)in,
		                               (struct nix_sockaddr_in *)out);
		*outlen = sizeof(struct nix_sockaddr_in);
		return (1);
	}

	return (0);
}

int
nix_socket(int family, int type, int protocol, nix_env_t *env)
{
	int fd;
	int gfd;

	LCLog(g_LCLogImpl, LCLogDebug, 0, "family=%d, type=%d, protocol=%d", family, type, protocol);

#ifdef _WIN32
	nix__wsa_ensure();
#endif

	fd = socket(family, type, protocol);
	if (fd < 0) {
		nix_env_set_errno(env, errno);
		return (-1);
	}

	if ((gfd = nix_fd_alloc(fd, env)) < 0) {
		nix_env_set_errno(env, ENFILE);
		close(fd);
		return (-1);
	}

#ifdef _WIN32
	nix_fd_set_socket(gfd, 1); /* route this fd's I/O through recv/send/closesocket */
#endif

	return (gfd);
}

int
nix_socketpair(int family, int type, int protocol, int *sv, nix_env_t *env)
{
#ifdef _WIN32
	/* No socketpair on win32 (it is an AF_UNIX construct). A loopback-TCP emulation is possible
	   but out of scope for the file-op-era surface; report unsupported. */
	(void)family;
	(void)type;
	(void)protocol;
	(void)sv;
	return (nix_nosys(env));
#else
	int d;
	int rc;
	int rsv[2];

	d = 0;

	LCLog(g_LCLogImpl, LCLogDebug, 0, "domain=%d, type=%d, protocol=%d, sv=%p", family, type, protocol, sv);

	if (sv == NULL) {
		nix_env_set_errno(env, EFAULT);
		return (-1);
	}

	rc = socketpair(family, type, protocol, rsv);
	if (rc < 0) {
		nix_env_set_errno(env, errno);
		return (-1);
	}

	if ((sv[d] = nix_fd_alloc(rsv[d], env)) < 0)
		goto errnfile;

	d++;
	if ((sv[d] = nix_fd_alloc(rsv[d], env)) < 0)
		goto errnfile;

	return (0);

errnfile:
	nix_fd_release(sv[d], env);
	close(rsv[1]);
	close(rsv[0]);
	nix_env_set_errno(env, ENFILE);
	return (0);
#endif /* _WIN32 */
}

int
nix_connect(int fd, struct nix_sockaddr const *sa, size_t salen, nix_env_t *env)
{
	struct sockaddr_storage ss;
	socklen_t               sslen;
	int                     rfd;
	int                     rc;
	socklen_t              *psalen = &sslen;
	struct sockaddr        *psa = (struct sockaddr *)&ss;

	LCLog(g_LCLogImpl, LCLogDebug, 0, "fd=%d, sa=%p, salen=%zu", fd, sa, salen);

	if (sa == NULL) {
		nix_env_set_errno(env, EFAULT);
		return (-1);
	}

	if ((rfd = nix_fd_get(fd)) < 0) {
		nix_env_set_errno(env, EBADF);
		return (-1);
	}

	rc = 0;

	__nix_try
	{
		if (!nix_sockaddr_to_sockaddr(sa, salen, psa, psalen)) {
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

	    if (rc < 0) return (-1);

	if (connect(rfd, psa, *psalen) != 0) {
		nix_env_set_errno(env, errno);
		return (-1);
	}

	return (0);
}

int
nix_bind(int fd, struct nix_sockaddr const *sa, size_t salen, nix_env_t *env)
{
	struct sockaddr_storage ss;
	socklen_t               sslen;
	int                     rfd;
	int                     rc;

	LCLog(g_LCLogImpl, LCLogDebug, 0, "fd=%d, sa=%p, salen=%zu", fd, sa, salen);

	if (sa == NULL) {
		nix_env_set_errno(env, EFAULT);
		return (-1);
	}

	if ((rfd = nix_fd_get(fd)) < 0) {
		nix_env_set_errno(env, EBADF);
		return (-1);
	}

	rc = 0;

	__nix_try
	{
		if (!nix_sockaddr_to_sockaddr(sa, salen, (struct sockaddr *)&ss, &sslen)) {
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

	    if (rc < 0) return (-1);

	if (bind(rfd, (struct sockaddr *)&ss, sslen) < 0) {
		nix_env_set_errno(env, errno);
		return (-1);
	}

	return (0);
}

int
nix_accept(int fd, struct nix_sockaddr *sa, nix_socklen_t *salen, nix_env_t *env)
{
	int                     rfd;
	int                     afd;
	int                     gfd;
	struct sockaddr_storage ss;
	socklen_t               sslen = sizeof(ss);

	LCLog(g_LCLogImpl, LCLogDebug, 0, "fd=%d, sa=%p, salen=%p", fd, sa, salen);

	if (sa == NULL || salen == NULL) {
		nix_env_set_errno(env, EFAULT);
		return (-1);
	}

	if ((rfd = nix_fd_get(fd)) < 0) {
		nix_env_set_errno(env, EBADF);
		return (-1);
	}

	if ((afd = accept(rfd, (struct sockaddr *)&ss, &sslen)) < 0) {
		nix_env_set_errno(env, errno);
		return (-1);
	}

	if ((gfd = nix_fd_alloc(afd, env)) < 0) {
		close(afd);
		return (-1);
	}

#ifdef _WIN32
	nix_fd_set_socket(gfd, 1); /* accepted connection is also a socket fd */
#endif

	__nix_try
	{
		sockaddr_to_nix_sockaddr((struct sockaddr const *)&ss, sslen, sa, salen);
	}
	__nix_catch_any
	{
		nix_fd_release(gfd, env);
		close(afd);

		nix_env_set_errno(env, EFAULT);
		return (-1);
	}
	__nix_end_try

	    return (gfd); /* accept(2) returns the new connection's descriptor, not 0 */
}

int
nix_listen(int fd, int backlog, nix_env_t *env)
{
	int rfd;

	LCLog(g_LCLogImpl, LCLogDebug, 0, "fd=%d, backlog=%d", fd, backlog);

	if (backlog < 0) {
		nix_env_set_errno(env, EINVAL);
		return (-1);
	}

	if ((rfd = nix_fd_get(fd)) < 0) {
		nix_env_set_errno(env, EBADF);
		return (-1);
	}

	if (listen(rfd, backlog) != 0) {
		nix_env_set_errno(env, errno);
		return (-1);
	}

	return (0);
}

int
nix_shutdown(int fd, int how, nix_env_t *env)
{
	int rfd;

	LCLog(g_LCLogImpl, LCLogDebug, 0, "fd=%d, how=%d", fd, how);

	if ((rfd = nix_fd_get(fd)) < 0) {
		nix_env_set_errno(env, EBADF);
		return (-1);
	}

	if (shutdown(rfd, how) != 0) {
		nix_env_set_errno(env, errno);
		return (-1);
	}

	return (0);
}

int
nix_getpeereid(int fd, nix_uid_t *euid, nix_gid_t *egid, nix_env_t *env)
{
	int rfd;
#if defined(HAVE_GETPEERUCRED)
	ucred_t *uc;
#elif defined(HAVE_GETPEEREID)
	uid_t uid;
	gid_t gid;
#endif
	int rc;

	LCLog(g_LCLogImpl, LCLogDebug, 0, "fd=%d, euid=%p, egid=%p", fd, euid, egid);

	if (euid == NULL || egid == NULL) {
		nix_env_set_errno(env, EFAULT);
		return (-1);
	}

	if ((rfd = nix_fd_get(fd)) < 0) {
		nix_env_set_errno(env, EBADF);
		return (-1);
	}

#if defined(HAVE_GETPEEREID)
	rc = getpeereid(rfd, &uid, &gid);
#elif defined(HAVE_GETPEERUCRED)
	rc = getpeerucred(rfd, &uc);
#else
	rc = -1;
	errno = ENOSYS;
#endif

	if (rc != 0) {
		nix_env_set_errno(env, errno);
		return (-1);
	}

	__nix_try
	{
#if defined(HAVE_GETPEERUCRED)
		*euid = ucred_geteuid(uc);
		*egid = ucred_getegid(uc);
#elif defined(HAVE_GETPEEREID)
		*euid = uid;
		*egid = gid;
#endif
	}
	__nix_catch_any
	{
		nix_env_set_errno(env, EFAULT);
		rc = -1;
	}
	__nix_end_try

#if defined(HAVE_GETPEERUCRED)
	    ucred_free(uc);
#endif

	return (0);
}

int
nix_getpeername(int fd, struct nix_sockaddr *sa, nix_socklen_t *salen,
                nix_env_t *env)
{
	int                     rfd;
	struct sockaddr_storage ss;
	socklen_t               sslen = sizeof(ss);

	LCLog(g_LCLogImpl, LCLogDebug, 0, "fd=%d, sa=%p, salen=%p", fd, sa, salen);

	if (sa == NULL || salen == NULL) {
		nix_env_set_errno(env, EFAULT);
		return (-1);
	}

	if ((rfd = nix_fd_get(fd)) < 0) {
		nix_env_set_errno(env, EBADF);
		return (-1);
	}

	if (getpeername(rfd, (struct sockaddr *)&ss, &sslen) != 0) {
		nix_env_set_errno(env, errno);
		return (-1);
	}

	__nix_try
	{
		sockaddr_to_nix_sockaddr((struct sockaddr const *)&ss, sslen,
		                         sa, salen);
	}
	__nix_catch_any
	{
		nix_env_set_errno(env, EFAULT);
		return (-1);
	}
	__nix_end_try

	    return (0);
}

int
nix_getsockname(int fd, struct nix_sockaddr *sa, nix_socklen_t *salen, nix_env_t *env)
{
	int                     rfd;
	struct sockaddr_storage ss;
	socklen_t               sslen = sizeof(ss);

	LCLog(g_LCLogImpl, LCLogDebug, 0, "fd=%d, sa=%p, salen=%p", fd, sa, salen);

	if (sa == NULL || salen == NULL) {
		nix_env_set_errno(env, EFAULT);
		return (-1);
	}

	if ((rfd = nix_fd_get(fd)) < 0) {
		nix_env_set_errno(env, EBADF);
		return (-1);
	}

	if (getsockname(rfd, (struct sockaddr *)&ss, &sslen) != 0) {
		nix_env_set_errno(env, errno);
		return (-1);
	}

	__nix_try
	{
		sockaddr_to_nix_sockaddr((struct sockaddr const *)&ss, sslen,
		                         sa, salen);
	}
	__nix_catch_any
	{
		nix_env_set_errno(env, EFAULT);
		return (-1);
	}
	__nix_end_try

	    return (0);
}

nix_ssize_t
nix_recvfrom(int fd, void *buf, size_t len, int flags, struct nix_sockaddr *sa, nix_socklen_t *salen, nix_env_t *env)
{
	struct sockaddr_storage ss;
	ssize_t                 rc;
	int                     rfd;
	socklen_t               sslen = sizeof(ss);
	struct sockaddr        *psa = sa != NULL ? (struct sockaddr *)&ss : NULL;
	socklen_t              *psalen = salen != NULL ? &sslen : NULL;

	LCLog(g_LCLogImpl, LCLogDebug, 0,
	        "fd=%d, buf=%p, len=%zu, flags=%x, sa=%p, salen=%p",
	        fd, buf, len, flags, sa, salen);

	if (buf == NULL) {
		nix_env_set_errno(env, EFAULT);
		return (-1);
	}

	if (psa != NULL && psalen == NULL) {
		nix_env_set_errno(env, EFAULT);
		return (-1);
	}

	if ((rfd = nix_fd_get(fd)) < 0) {
		nix_env_set_errno(env, EBADF);
		return (-1);
	}

	if (len == 0)
		return (0);

	/*XXX convert flags */

	if ((rc = recvfrom(rfd, buf, len, flags, psa, psalen)) < 0) {
		nix_env_set_errno(env, errno);
		return (-1);
	}

	if (psa != NULL) {
		int rc = 0;

		__nix_try
		{
			if (!sockaddr_to_nix_sockaddr(psa, *psalen, sa, salen)) {
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

		    if (rc < 0) return (-1);
	}

	return (rc);
}

#if 0
nix_ssize_t
nix_recvmsg(int fd, void *buf, size_t len, int flags, struct nix_sockaddr *sa,
	size_t *salen, nix_env_t *env)
{
	struct sockaddr_storage  ss;
	socklen_t                sslen  = sizeof(ss);
	struct sockaddr_storage *pss    = sa != NULL ? &ss : NULL;
	socklen_t               *psslen = salen != NULL ? &sslen : NULL;
	ssize_t                  rc;
	int                      rfd;

	if (buf == NULL) {
		nix_env_set_errno(env, EFAULT);
		return (-1);
	}

	if (pss != NULL && psslen == NULL) {
		nix_env_set_errno(env, EFAULT);
		return (-1);
	}

	if ((rfd = nix_fd_get(fd)) < 0) {
		nix_env_set_errno(env, EBADF);
		return (-1);
	}

	if (len == 0)
		return (0);

	/*XXX convert flags */

	if ((rc = recvfrom(rfd, buf, len, flags,
		(struct sockaddr *)pss, psslen)) < 0) {
		nix_env_set_errno(env, errno);
		return (-1);
	}

	if (pss != NULL) {
		int rc = 0;

		__nix_try
		{
			if (!sockaddr_to_nix_sockaddr((struct sockaddr *)pss,
				*psslen, sa, salen)) {
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

		if (rc < 0)
			return (-1);
	  }

	return (rc);
}
#endif

nix_ssize_t
nix_sendto(int fd, void const *buf, size_t len, int flags,
           struct nix_sockaddr const *sa, nix_socklen_t salen, nix_env_t *env)
{
	struct sockaddr_storage ss;
	int                     rfd;
	ssize_t                 rc;
	socklen_t               sslen = 0;
	socklen_t              *psalen = NULL;
	struct sockaddr        *psa = NULL;

	LCLog(g_LCLogImpl, LCLogDebug, 0, "fd=%d, buf=%p, len=%zu, flags=%x, sa=%p, salen=%zu", fd, buf, len, flags, sa, salen);

	if (buf == NULL) {
		nix_env_set_errno(env, EFAULT);
		return ((-1));
	}

	if ((rfd = nix_fd_get(fd)) < 0) {
		nix_env_set_errno(env, EBADF);
		return (-1);
	}

	if (len == 0)
		return (0);

	if (sa != NULL) {
		int rc = 0;

		__nix_try
		{
			psa = (struct sockaddr *)&ss;
			psalen = &sslen;
			sslen = sizeof(ss);
			if (!nix_sockaddr_to_sockaddr(sa, salen, psa, psalen)) {
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

		    if (rc < 0) return (-1);
	}

	/*XXX convert flags */

	if ((rc = sendto(rfd, buf, len, flags, psa,
	                 psalen == NULL ? 0 : *psalen)) < 0) {
		nix_env_set_errno(env, errno);
		return (-1);
	}

	return (rc);
}
