/*
 * nix-platform-win32-aflocal-advanced.c
 *
 * Advanced AF_LOCAL features for Windows:
 * - Peer credentials (SO_PEERCRED)
 * - Rights transfer (SCM_RIGHTS) via sendmsg/recvmsg
 *
 * Uses Windows APIs:
 * - GetNamedPipeClientProcessId() - Get client PID
 * - GetNamedPipeServerProcessId() - Get server PID
 * - OpenProcess() + GetTokenInformation() - Get user/group info
 * - DuplicateHandle() - Transfer file descriptors between processes
 */

#include "nix-platform.h"

#if defined(NIX_HOST_WIN32)

#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

/* Forward declarations from nix-platform-win32-aflocal.c */
typedef struct af_local_socket af_local_socket_t;
extern af_local_socket_t *aflocal_find(int sockfd);
extern CRITICAL_SECTION g_aflocal_lock;

/* Access to socket internals */
struct af_local_socket {
	int sockfd;
	int type;
	int state;
	char path[108];
	HANDLE handle;
	HANDLE server_handle;
	void *overlapped;
	int listening;
	int connected;
	int backlog;
	DWORD peer_pid;              /* Peer process ID */
	HANDLE peer_process;         /* Peer process handle */
	struct af_local_socket *next;
};

/* Peer credentials structure */
typedef struct {
	DWORD pid;
	DWORD uid;
	DWORD gid;
} nix_ucred_t;

/* Message header for sendmsg/recvmsg */
typedef struct {
	void *msg_name;           /* Address (unused for connected sockets) */
	size_t msg_namelen;       /* Address length */
	void *msg_iov;            /* I/O vector */
	size_t msg_iovlen;        /* Number of elements in msg_iov */
	void *msg_control;        /* Ancillary data */
	size_t msg_controllen;    /* Ancillary data length */
	int msg_flags;            /* Flags on received message */
} nix_msghdr_t;

/* I/O vector element */
typedef struct {
	void *iov_base;
	size_t iov_len;
} nix_iovec_t;

/* Control message header */
typedef struct {
	size_t cmsg_len;    /* Length including header */
	int cmsg_level;     /* Originating protocol */
	int cmsg_type;      /* Protocol-specific type */
	/* Followed by unsigned char cmsg_data[] */
} nix_cmsghdr_t;

/* Constants */
#define NIX_SOL_SOCKET    1
#define NIX_SO_PEERCRED   17

#define NIX_SCM_RIGHTS    1
#define NIX_SCM_CREDENTIALS 2

#define NIX_CMSG_ALIGN(len) (((len) + sizeof(size_t) - 1) & ~(sizeof(size_t) - 1))
#define NIX_CMSG_SPACE(len) (NIX_CMSG_ALIGN(sizeof(nix_cmsghdr_t)) + NIX_CMSG_ALIGN(len))
#define NIX_CMSG_LEN(len)   (NIX_CMSG_ALIGN(sizeof(nix_cmsghdr_t)) + (len))

#define NIX_CMSG_FIRSTHDR(mhdr) \
	((mhdr)->msg_controllen >= sizeof(nix_cmsghdr_t) ? \
	 (nix_cmsghdr_t *)(mhdr)->msg_control : (nix_cmsghdr_t *)NULL)

#define NIX_CMSG_DATA(cmsg) \
	((unsigned char *)((nix_cmsghdr_t *)(cmsg) + 1))

#define NIX_CMSG_NXTHDR(mhdr, cmsg) \
	__nix_cmsg_nxthdr((mhdr), (cmsg))

/* Helper for CMSG_NXTHDR */
static nix_cmsghdr_t *
__nix_cmsg_nxthdr(nix_msghdr_t *msg, nix_cmsghdr_t *cmsg)
{
	nix_cmsghdr_t *next;

	if (cmsg->cmsg_len < sizeof(nix_cmsghdr_t))
		return NULL;

	next = (nix_cmsghdr_t *)((unsigned char *)cmsg + NIX_CMSG_ALIGN(cmsg->cmsg_len));

	if ((unsigned char *)(next + 1) > (unsigned char *)msg->msg_control + msg->msg_controllen)
		return NULL;

	if ((unsigned char *)next + NIX_CMSG_ALIGN(next->cmsg_len) >
		(unsigned char *)msg->msg_control + msg->msg_controllen)
		return NULL;

	return next;
}

/*
 * ========================================================================
 * PEER CREDENTIALS
 * ========================================================================
 */

/*
 * Get user and group ID from process token
 */
static int
get_process_credentials(HANDLE process, DWORD *uid, DWORD *gid)
{
	HANDLE token = NULL;
	DWORD needed = 0;
	TOKEN_USER *user_info = NULL;
	TOKEN_PRIMARY_GROUP *group_info = NULL;
	int ret = -1;

	if (!OpenProcessToken(process, TOKEN_QUERY, &token)) {
		return -1;
	}

	/* Get user SID */
	GetTokenInformation(token, TokenUser, NULL, 0, &needed);
	if (needed == 0) {
		CloseHandle(token);
		return -1;
	}

	user_info = (TOKEN_USER *)malloc(needed);
	if (user_info == NULL) {
		CloseHandle(token);
		return -1;
	}

	if (!GetTokenInformation(token, TokenUser, user_info, needed, &needed)) {
		free(user_info);
		CloseHandle(token);
		return -1;
	}

	/* Convert SID to RID (use as UID) */
	if (IsValidSid(user_info->User.Sid)) {
		DWORD sid_count = *GetSidSubAuthorityCount(user_info->User.Sid);
		if (sid_count > 0) {
			*uid = *GetSidSubAuthority(user_info->User.Sid, sid_count - 1);
		} else {
			*uid = 0;
		}
	} else {
		*uid = 0;
	}

	free(user_info);

	/* Get primary group SID */
	GetTokenInformation(token, TokenPrimaryGroup, NULL, 0, &needed);
	if (needed == 0) {
		CloseHandle(token);
		return -1;
	}

	group_info = (TOKEN_PRIMARY_GROUP *)malloc(needed);
	if (group_info == NULL) {
		CloseHandle(token);
		return -1;
	}

	if (!GetTokenInformation(token, TokenPrimaryGroup, group_info, needed, &needed)) {
		free(group_info);
		CloseHandle(token);
		return -1;
	}

	/* Convert SID to RID (use as GID) */
	if (IsValidSid(group_info->PrimaryGroup)) {
		DWORD sid_count = *GetSidSubAuthorityCount(group_info->PrimaryGroup);
		if (sid_count > 0) {
			*gid = *GetSidSubAuthority(group_info->PrimaryGroup, sid_count - 1);
		} else {
			*gid = 0;
		}
	} else {
		*gid = 0;
	}

	free(group_info);
	CloseHandle(token);

	return 0;
}

/*
 * Get peer credentials
 */
int
nix_platform_win32_aflocal_getpeercred(int sockfd, nix_ucred_t *cred)
{
	af_local_socket_t *sock;
	DWORD peer_pid = 0;
	HANDLE peer_process = NULL;
	DWORD uid = 0, gid = 0;

	sock = aflocal_find(sockfd);
	if (sock == NULL) {
		nix_platform_set_errno(EBADF);
		return -1;
	}

	if (!sock->connected) {
		nix_platform_set_errno(ENOTCONN);
		return -1;
	}

	/* Only works for named pipes (SOCK_STREAM, SOCK_SEQPACKET) */
	if (sock->type == 2) {  /* SOCK_DGRAM */
		nix_platform_set_errno(EOPNOTSUPP);
		return -1;
	}

	/* Get peer process ID from named pipe */
	if (sock->listening) {
		/* We are the server, get client PID */
		if (!GetNamedPipeClientProcessId(sock->handle, &peer_pid)) {
			nix_platform_set_errno(EINVAL);
			return -1;
		}
	} else {
		/* We are the client, get server PID */
		if (!GetNamedPipeServerProcessId(sock->handle, &peer_pid)) {
			nix_platform_set_errno(EINVAL);
			return -1;
		}
	}

	/* Open peer process to get token information */
	peer_process = OpenProcess(PROCESS_QUERY_INFORMATION, FALSE, peer_pid);
	if (peer_process == NULL) {
		/* If we can't open the process, return what we have */
		cred->pid = peer_pid;
		cred->uid = 0;
		cred->gid = 0;
		return 0;
	}

	/* Get user and group IDs */
	if (get_process_credentials(peer_process, &uid, &gid) < 0) {
		uid = 0;
		gid = 0;
	}

	CloseHandle(peer_process);

	cred->pid = peer_pid;
	cred->uid = uid;
	cred->gid = gid;

	return 0;
}

/*
 * ========================================================================
 * SENDMSG / RECVMSG
 * ========================================================================
 */

/*
 * Control message protocol:
 * For SCM_RIGHTS, we send/receive file descriptor handles via the pipe.
 *
 * Wire format:
 * 1. Control message header (cmsg_len, cmsg_level, cmsg_type)
 * 2. For SCM_RIGHTS: array of HANDLE values (as DWORD64)
 * 3. Regular message data
 *
 * The sender duplicates handles into the receiver's process.
 * The receiver receives the duplicated handles.
 */

/*
 * Send message with control data
 */
ssize_t
nix_platform_win32_aflocal_sendmsg(int sockfd, const nix_msghdr_t *msg, int flags)
{
	af_local_socket_t *sock;
	DWORD written = 0;
	size_t total_written = 0;
	nix_cmsghdr_t *cmsg;
	DWORD peer_pid = 0;
	HANDLE peer_process = NULL;
	int i;

	sock = aflocal_find(sockfd);
	if (sock == NULL) {
		nix_platform_set_errno(EBADF);
		return -1;
	}

	if (!sock->connected) {
		nix_platform_set_errno(ENOTCONN);
		return -1;
	}

	if (sock->type == 2) {  /* SOCK_DGRAM */
		nix_platform_set_errno(EOPNOTSUPP);
		return -1;
	}

	/* Process control messages */
	if (msg->msg_controllen > 0 && msg->msg_control != NULL) {
		/* Get peer process ID */
		if (sock->listening) {
			if (!GetNamedPipeClientProcessId(sock->handle, &peer_pid)) {
				nix_platform_set_errno(EINVAL);
				return -1;
			}
		} else {
			if (!GetNamedPipeServerProcessId(sock->handle, &peer_pid)) {
				nix_platform_set_errno(EINVAL);
				return -1;
			}
		}

		/* Open peer process for handle duplication */
		peer_process = OpenProcess(PROCESS_DUP_HANDLE, FALSE, peer_pid);
		if (peer_process == NULL) {
			nix_platform_set_errno(EACCES);
			return -1;
		}

		/* Iterate through control messages */
		for (cmsg = NIX_CMSG_FIRSTHDR(msg); cmsg != NULL; cmsg = NIX_CMSG_NXTHDR(msg, cmsg)) {
			if (cmsg->cmsg_level == NIX_SOL_SOCKET && cmsg->cmsg_type == NIX_SCM_RIGHTS) {
				/* SCM_RIGHTS: duplicate file descriptors */
				int *fds = (int *)NIX_CMSG_DATA(cmsg);
				int fd_count = (cmsg->cmsg_len - sizeof(nix_cmsghdr_t)) / sizeof(int);
				HANDLE *dup_handles = (HANDLE *)malloc(fd_count * sizeof(HANDLE));

				if (dup_handles == NULL) {
					CloseHandle(peer_process);
					nix_platform_set_errno(ENOMEM);
					return -1;
				}

				/* Duplicate each file descriptor to peer process */
				for (i = 0; i < fd_count; i++) {
					HANDLE source_handle = (HANDLE)_get_osfhandle(fds[i]);
					HANDLE target_handle = INVALID_HANDLE_VALUE;

					if (source_handle == INVALID_HANDLE_VALUE) {
						/* Skip invalid FDs */
						dup_handles[i] = INVALID_HANDLE_VALUE;
						continue;
					}

					if (!DuplicateHandle(
						GetCurrentProcess(),
						source_handle,
						peer_process,
						&target_handle,
						0,
						FALSE,
						DUPLICATE_SAME_ACCESS)) {
						/* Duplication failed */
						dup_handles[i] = INVALID_HANDLE_VALUE;
					} else {
						dup_handles[i] = target_handle;
					}
				}

				/* Send control message header */
				nix_cmsghdr_t hdr;
				hdr.cmsg_len = sizeof(nix_cmsghdr_t) + fd_count * sizeof(DWORD64);
				hdr.cmsg_level = NIX_SOL_SOCKET;
				hdr.cmsg_type = NIX_SCM_RIGHTS;

				if (!WriteFile(sock->handle, &hdr, sizeof(hdr), &written, NULL)) {
					free(dup_handles);
					CloseHandle(peer_process);
					nix_platform_set_errno(EIO);
					return -1;
				}

				/* Send duplicated handles as DWORD64 */
				DWORD64 *handle_vals = (DWORD64 *)malloc(fd_count * sizeof(DWORD64));
				for (i = 0; i < fd_count; i++) {
					handle_vals[i] = (DWORD64)(uintptr_t)dup_handles[i];
				}

				if (!WriteFile(sock->handle, handle_vals, fd_count * sizeof(DWORD64), &written, NULL)) {
					free(handle_vals);
					free(dup_handles);
					CloseHandle(peer_process);
					nix_platform_set_errno(EIO);
					return -1;
				}

				free(handle_vals);
				free(dup_handles);
			}
		}

		CloseHandle(peer_process);
	}

	/* Send regular data from iovec */
	if (msg->msg_iov != NULL && msg->msg_iovlen > 0) {
		nix_iovec_t *iov = (nix_iovec_t *)msg->msg_iov;

		for (i = 0; i < (int)msg->msg_iovlen; i++) {
			if (iov[i].iov_len == 0)
				continue;

			if (!WriteFile(sock->handle, iov[i].iov_base, (DWORD)iov[i].iov_len, &written, NULL)) {
				if (total_written > 0)
					return total_written;
				nix_platform_set_errno(EIO);
				return -1;
			}

			total_written += written;
		}
	}

	return total_written;
}

/*
 * Receive message with control data
 */
ssize_t
nix_platform_win32_aflocal_recvmsg(int sockfd, nix_msghdr_t *msg, int flags)
{
	af_local_socket_t *sock;
	DWORD read_bytes = 0;
	size_t total_read = 0;
	nix_cmsghdr_t *cmsg_buf = NULL;
	size_t cmsg_space = 0;
	int i;

	sock = aflocal_find(sockfd);
	if (sock == NULL) {
		nix_platform_set_errno(EBADF);
		return -1;
	}

	if (!sock->connected) {
		nix_platform_set_errno(ENOTCONN);
		return -1;
	}

	if (sock->type == 2) {  /* SOCK_DGRAM */
		nix_platform_set_errno(EOPNOTSUPP);
		return -1;
	}

	/* Initialize control message space */
	msg->msg_controllen = 0;
	msg->msg_flags = 0;

	/* Peek for control messages */
	DWORD bytes_avail = 0;
	if (PeekNamedPipe(sock->handle, NULL, 0, NULL, &bytes_avail, NULL)) {
		if (bytes_avail >= sizeof(nix_cmsghdr_t)) {
			/* Might have control messages, try to read header */
			nix_cmsghdr_t hdr;
			DWORD peeked = 0;

			if (ReadFile(sock->handle, &hdr, sizeof(hdr), &peeked, NULL)) {
				if (peeked == sizeof(hdr) &&
					hdr.cmsg_level == NIX_SOL_SOCKET &&
					hdr.cmsg_type == NIX_SCM_RIGHTS) {

					/* SCM_RIGHTS message */
					int fd_count = (hdr.cmsg_len - sizeof(nix_cmsghdr_t)) / sizeof(DWORD64);
					DWORD64 *handle_vals = (DWORD64 *)malloc(fd_count * sizeof(DWORD64));

					if (handle_vals != NULL) {
						if (ReadFile(sock->handle, handle_vals, fd_count * sizeof(DWORD64), &read_bytes, NULL)) {
							/* Convert handles to file descriptors */
							if (msg->msg_control != NULL && msg->msg_controllen >= NIX_CMSG_SPACE(fd_count * sizeof(int))) {
								cmsg_buf = (nix_cmsghdr_t *)msg->msg_control;
								cmsg_buf->cmsg_len = NIX_CMSG_LEN(fd_count * sizeof(int));
								cmsg_buf->cmsg_level = NIX_SOL_SOCKET;
								cmsg_buf->cmsg_type = NIX_SCM_RIGHTS;

								int *fds = (int *)NIX_CMSG_DATA(cmsg_buf);
								for (i = 0; i < fd_count; i++) {
									HANDLE h = (HANDLE)(uintptr_t)handle_vals[i];
									if (h != INVALID_HANDLE_VALUE) {
										/* Convert HANDLE to file descriptor */
										fds[i] = _open_osfhandle((intptr_t)h, 0);
									} else {
										fds[i] = -1;
									}
								}

								msg->msg_controllen = NIX_CMSG_SPACE(fd_count * sizeof(int));
							} else {
								/* Not enough space, close handles */
								for (i = 0; i < fd_count; i++) {
									HANDLE h = (HANDLE)(uintptr_t)handle_vals[i];
									if (h != INVALID_HANDLE_VALUE) {
										CloseHandle(h);
									}
								}
								msg->msg_flags |= 0x0008;  /* MSG_CTRUNC */
							}
						}
						free(handle_vals);
					}
				}
			}
		}
	}

	/* Read regular data into iovec */
	if (msg->msg_iov != NULL && msg->msg_iovlen > 0) {
		nix_iovec_t *iov = (nix_iovec_t *)msg->msg_iov;

		for (i = 0; i < (int)msg->msg_iovlen; i++) {
			if (iov[i].iov_len == 0)
				continue;

			if (!ReadFile(sock->handle, iov[i].iov_base, (DWORD)iov[i].iov_len, &read_bytes, NULL)) {
				if (total_read > 0)
					return total_read;

				DWORD err = GetLastError();
				if (err == ERROR_BROKEN_PIPE || err == ERROR_PIPE_NOT_CONNECTED) {
					return 0;  /* EOF */
				}

				nix_platform_set_errno(EIO);
				return -1;
			}

			total_read += read_bytes;

			/* If we got less than requested, stop here */
			if (read_bytes < iov[i].iov_len)
				break;
		}
	}

	return total_read;
}

/*
 * ========================================================================
 * GETSOCKOPT EXTENSION
 * ========================================================================
 */

/*
 * Extended getsockopt for SO_PEERCRED
 */
int
nix_platform_win32_aflocal_getsockopt_ex(int sockfd, int level, int optname,
										 void *optval, socklen_t *optlen)
{
	if (level == NIX_SOL_SOCKET && optname == NIX_SO_PEERCRED) {
		if (*optlen < sizeof(nix_ucred_t)) {
			nix_platform_set_errno(EINVAL);
			return -1;
		}

		if (nix_platform_win32_aflocal_getpeercred(sockfd, (nix_ucred_t *)optval) < 0) {
			return -1;
		}

		*optlen = sizeof(nix_ucred_t);
		return 0;
	}

	/* Fall back to regular getsockopt */
	nix_platform_set_errno(ENOPROTOOPT);
	return -1;
}

#endif /* NIX_HOST_WIN32 */
