/*
 * nix-platform-win32-aflocal.c
 *
 * AF_LOCAL (Unix Domain Sockets) emulation for Windows
 * Implements SOCK_STREAM, SOCK_DGRAM, and SOCK_SEQPACKET with proper semantics
 *
 * Emulation Strategy:
 * - SOCK_STREAM: Windows named pipes (byte stream, connection-oriented)
 * - SOCK_DGRAM: Mailslots (datagram, connectionless)
 * - SOCK_SEQPACKET: Named pipes with message mode (message boundaries preserved)
 */

#include "nix-platform.h"

#if defined(NIX_HOST_WIN32)

#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

/* Maximum path length for AF_LOCAL addresses */
#define UNIX_PATH_MAX 108

/* AF_LOCAL socket state */
typedef struct af_local_socket {
	int sockfd;                    /* Virtual socket fd */
	int type;                      /* SOCK_STREAM, SOCK_DGRAM, SOCK_SEQPACKET */
	int state;                     /* Socket state */
	char path[UNIX_PATH_MAX];      /* Bound path */
	HANDLE handle;                 /* Named pipe or mailslot handle */
	HANDLE server_handle;          /* Server pipe handle (for accept) */
	OVERLAPPED overlapped;         /* For async I/O */
	int listening;                 /* Is listening? */
	int connected;                 /* Is connected? */
	int backlog;                   /* Listen backlog */
	struct af_local_socket *next;  /* Linked list */
} af_local_socket_t;

/* Socket states */
#define AFLOCAL_STATE_CREATED     0
#define AFLOCAL_STATE_BOUND       1
#define AFLOCAL_STATE_LISTENING   2
#define AFLOCAL_STATE_CONNECTED   3
#define AFLOCAL_STATE_CLOSED      4

/* Global socket list */
static af_local_socket_t *g_aflocal_sockets = NULL;
static CRITICAL_SECTION g_aflocal_lock;
static int g_aflocal_initialized = 0;
static int g_aflocal_next_fd = 10000;  /* Start from high number to avoid conflicts */

/*
 * ========================================================================
 * INITIALIZATION AND CLEANUP
 * ========================================================================
 */

static void
aflocal_init(void)
{
	if (g_aflocal_initialized)
		return;

	InitializeCriticalSection(&g_aflocal_lock);
	g_aflocal_sockets = NULL;
	g_aflocal_initialized = 1;
}

/*
 * ========================================================================
 * SOCKET TRACKING
 * ========================================================================
 */

static af_local_socket_t *
aflocal_alloc(int type)
{
	aflocal_init();

	af_local_socket_t *sock = (af_local_socket_t *)malloc(sizeof(af_local_socket_t));
	if (sock == NULL)
		return NULL;

	memset(sock, 0, sizeof(af_local_socket_t));

	EnterCriticalSection(&g_aflocal_lock);
	sock->sockfd = g_aflocal_next_fd++;
	sock->type = type;
	sock->state = AFLOCAL_STATE_CREATED;
	sock->handle = INVALID_HANDLE_VALUE;
	sock->server_handle = INVALID_HANDLE_VALUE;
	sock->listening = 0;
	sock->connected = 0;
	sock->backlog = 5;
	sock->next = g_aflocal_sockets;
	g_aflocal_sockets = sock;
	LeaveCriticalSection(&g_aflocal_lock);

	return sock;
}

static af_local_socket_t *
aflocal_find(int sockfd)
{
	aflocal_init();

	EnterCriticalSection(&g_aflocal_lock);
	af_local_socket_t *sock = g_aflocal_sockets;
	while (sock != NULL) {
		if (sock->sockfd == sockfd) {
			LeaveCriticalSection(&g_aflocal_lock);
			return sock;
		}
		sock = sock->next;
	}
	LeaveCriticalSection(&g_aflocal_lock);
	return NULL;
}

static void
aflocal_free(int sockfd)
{
	aflocal_init();

	EnterCriticalSection(&g_aflocal_lock);

	af_local_socket_t *sock = g_aflocal_sockets;
	af_local_socket_t *prev = NULL;

	while (sock != NULL) {
		if (sock->sockfd == sockfd) {
			/* Remove from list */
			if (prev == NULL) {
				g_aflocal_sockets = sock->next;
			} else {
				prev->next = sock->next;
			}

			/* Close handles */
			if (sock->handle != INVALID_HANDLE_VALUE) {
				CloseHandle(sock->handle);
			}
			if (sock->server_handle != INVALID_HANDLE_VALUE) {
				CloseHandle(sock->server_handle);
			}

			free(sock);
			LeaveCriticalSection(&g_aflocal_lock);
			return;
		}
		prev = sock;
		sock = sock->next;
	}

	LeaveCriticalSection(&g_aflocal_lock);
}

/*
 * ========================================================================
 * PATH CONVERSION
 * ========================================================================
 */

/* Convert Unix path to Windows named pipe path */
static void
unix_path_to_pipe(const char *unix_path, char *pipe_path, size_t pipe_path_len)
{
	/* Convert /tmp/mysocket to \\.\pipe\mysocket */
	const char *name = strrchr(unix_path, '/');
	if (name != NULL) {
		name++;  /* Skip the '/' */
	} else {
		name = unix_path;
	}

	/* Remove .sock or .socket extension if present */
	char clean_name[256];
	strncpy(clean_name, name, sizeof(clean_name) - 1);
	clean_name[sizeof(clean_name) - 1] = '\0';

	char *ext = strstr(clean_name, ".sock");
	if (ext != NULL) {
		*ext = '\0';
	}

	_snprintf(pipe_path, pipe_path_len, "\\\\.\\pipe\\nix_aflocal_%s", clean_name);
}

/* Convert Unix path to Windows mailslot path */
static void
unix_path_to_mailslot(const char *unix_path, char *mailslot_path, size_t mailslot_path_len)
{
	/* Convert /tmp/mysocket to \\.\mailslot\mysocket */
	const char *name = strrchr(unix_path, '/');
	if (name != NULL) {
		name++;
	} else {
		name = unix_path;
	}

	char clean_name[256];
	strncpy(clean_name, name, sizeof(clean_name) - 1);
	clean_name[sizeof(clean_name) - 1] = '\0';

	char *ext = strstr(clean_name, ".sock");
	if (ext != NULL) {
		*ext = '\0';
	}

	_snprintf(mailslot_path, mailslot_path_len, "\\\\.\\mailslot\\nix_aflocal_%s", clean_name);
}

/*
 * ========================================================================
 * SOCKET CREATION
 * ========================================================================
 */

int
nix_platform_win32_aflocal_socket(int type, int protocol)
{
	/* Validate socket type */
	if (type != 1 && type != 2 && type != 5) {  /* SOCK_STREAM, SOCK_DGRAM, SOCK_SEQPACKET */
		nix_platform_set_errno(EPROTONOSUPPORT);
		return -1;
	}

	af_local_socket_t *sock = aflocal_alloc(type);
	if (sock == NULL) {
		nix_platform_set_errno(ENOMEM);
		return -1;
	}

	return sock->sockfd;
}

/*
 * ========================================================================
 * BIND
 * ========================================================================
 */

int
nix_platform_win32_aflocal_bind(int sockfd, const char *path)
{
	af_local_socket_t *sock = aflocal_find(sockfd);
	if (sock == NULL) {
		nix_platform_set_errno(EBADF);
		return -1;
	}

	if (sock->state != AFLOCAL_STATE_CREATED) {
		nix_platform_set_errno(EINVAL);
		return -1;
	}

	/* Save path */
	strncpy(sock->path, path, sizeof(sock->path) - 1);
	sock->path[sizeof(sock->path) - 1] = '\0';

	if (sock->type == 2) {  /* SOCK_DGRAM */
		/* Create mailslot for receiving */
		char mailslot_path[MAX_PATH];
		unix_path_to_mailslot(path, mailslot_path, sizeof(mailslot_path));

		sock->handle = CreateMailslotA(
			mailslot_path,
			0,                      /* No maximum message size */
			MAILSLOT_WAIT_FOREVER,  /* No timeout */
			NULL                    /* Default security */
		);

		if (sock->handle == INVALID_HANDLE_VALUE) {
			DWORD err = GetLastError();
			if (err == ERROR_ALREADY_EXISTS) {
				nix_platform_set_errno(EADDRINUSE);
			} else {
				nix_platform_set_errno(EINVAL);
			}
			return -1;
		}

	} else if (sock->type == 1 || sock->type == 5) {  /* SOCK_STREAM or SOCK_SEQPACKET */
		/* For stream/seqpacket sockets, we'll create the pipe in listen() */
		/* Just mark as bound for now */
	}

	sock->state = AFLOCAL_STATE_BOUND;
	return 0;
}

/*
 * ========================================================================
 * LISTEN
 * ========================================================================
 */

int
nix_platform_win32_aflocal_listen(int sockfd, int backlog)
{
	af_local_socket_t *sock = aflocal_find(sockfd);
	if (sock == NULL) {
		nix_platform_set_errno(EBADF);
		return -1;
	}

	if (sock->type == 2) {  /* SOCK_DGRAM */
		nix_platform_set_errno(EOPNOTSUPP);
		return -1;
	}

	if (sock->state != AFLOCAL_STATE_BOUND) {
		nix_platform_set_errno(EINVAL);
		return -1;
	}

	/* Create named pipe for listening */
	char pipe_path[MAX_PATH];
	unix_path_to_pipe(sock->path, pipe_path, sizeof(pipe_path));

	DWORD open_mode = PIPE_ACCESS_DUPLEX | FILE_FLAG_OVERLAPPED;
	DWORD pipe_mode;

	if (sock->type == 5) {  /* SOCK_SEQPACKET */
		pipe_mode = PIPE_TYPE_MESSAGE | PIPE_READMODE_MESSAGE | PIPE_WAIT;
	} else {  /* SOCK_STREAM */
		pipe_mode = PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT;
	}

	sock->server_handle = CreateNamedPipeA(
		pipe_path,
		open_mode,
		pipe_mode,
		PIPE_UNLIMITED_INSTANCES,  /* Allow multiple instances */
		4096,  /* Output buffer size */
		4096,  /* Input buffer size */
		0,     /* Default timeout */
		NULL   /* Default security */
	);

	if (sock->server_handle == INVALID_HANDLE_VALUE) {
		DWORD err = GetLastError();
		if (err == ERROR_ACCESS_DENIED) {
			nix_platform_set_errno(EACCES);
		} else {
			nix_platform_set_errno(EINVAL);
		}
		return -1;
	}

	sock->backlog = backlog;
	sock->listening = 1;
	sock->state = AFLOCAL_STATE_LISTENING;

	return 0;
}

/*
 * ========================================================================
 * ACCEPT
 * ========================================================================
 */

int
nix_platform_win32_aflocal_accept(int sockfd, char *addr, size_t *addrlen)
{
	af_local_socket_t *server_sock = aflocal_find(sockfd);
	if (server_sock == NULL) {
		nix_platform_set_errno(EBADF);
		return -1;
	}

	if (!server_sock->listening) {
		nix_platform_set_errno(EINVAL);
		return -1;
	}

	/* Wait for client connection */
	BOOL connected = ConnectNamedPipe(server_sock->server_handle, NULL);
	if (!connected && GetLastError() != ERROR_PIPE_CONNECTED) {
		nix_platform_set_errno(EIO);
		return -1;
	}

	/* Create new socket for the accepted connection */
	af_local_socket_t *client_sock = aflocal_alloc(server_sock->type);
	if (client_sock == NULL) {
		DisconnectNamedPipe(server_sock->server_handle);
		nix_platform_set_errno(ENOMEM);
		return -1;
	}

	/* Transfer the connected pipe to the client socket */
	client_sock->handle = server_sock->server_handle;
	client_sock->connected = 1;
	client_sock->state = AFLOCAL_STATE_CONNECTED;
	strcpy(client_sock->path, server_sock->path);

	/* Create a new pipe instance for the server to accept more connections */
	char pipe_path[MAX_PATH];
	unix_path_to_pipe(server_sock->path, pipe_path, sizeof(pipe_path));

	DWORD open_mode = PIPE_ACCESS_DUPLEX | FILE_FLAG_OVERLAPPED;
	DWORD pipe_mode;

	if (server_sock->type == 5) {  /* SOCK_SEQPACKET */
		pipe_mode = PIPE_TYPE_MESSAGE | PIPE_READMODE_MESSAGE | PIPE_WAIT;
	} else {  /* SOCK_STREAM */
		pipe_mode = PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT;
	}

	server_sock->server_handle = CreateNamedPipeA(
		pipe_path,
		open_mode,
		pipe_mode,
		PIPE_UNLIMITED_INSTANCES,
		4096, 4096, 0, NULL
	);

	/* Return client address if requested */
	if (addr != NULL && addrlen != NULL) {
		size_t len = strlen(server_sock->path) + 1;
		if (len > *addrlen) {
			len = *addrlen;
		}
		memcpy(addr, server_sock->path, len);
		*addrlen = len;
	}

	return client_sock->sockfd;
}

/*
 * ========================================================================
 * CONNECT
 * ========================================================================
 */

int
nix_platform_win32_aflocal_connect(int sockfd, const char *path)
{
	af_local_socket_t *sock = aflocal_find(sockfd);
	if (sock == NULL) {
		nix_platform_set_errno(EBADF);
		return -1;
	}

	if (sock->type == 2) {  /* SOCK_DGRAM */
		/* For datagram sockets, just save the peer address */
		strncpy(sock->path, path, sizeof(sock->path) - 1);
		sock->path[sizeof(sock->path) - 1] = '\0';
		sock->connected = 1;
		sock->state = AFLOCAL_STATE_CONNECTED;
		return 0;
	}

	/* SOCK_STREAM or SOCK_SEQPACKET */
	char pipe_path[MAX_PATH];
	unix_path_to_pipe(path, pipe_path, sizeof(pipe_path));

	/* Try to open the named pipe */
	HANDLE pipe = CreateFileA(
		pipe_path,
		GENERIC_READ | GENERIC_WRITE,
		0,      /* No sharing */
		NULL,   /* Default security */
		OPEN_EXISTING,
		0,      /* No special flags */
		NULL    /* No template */
	);

	if (pipe == INVALID_HANDLE_VALUE) {
		DWORD err = GetLastError();
		if (err == ERROR_FILE_NOT_FOUND || err == ERROR_PATH_NOT_FOUND) {
			nix_platform_set_errno(ECONNREFUSED);
		} else if (err == ERROR_PIPE_BUSY) {
			/* Wait for pipe to become available */
			if (!WaitNamedPipeA(pipe_path, 5000)) {  /* 5 second timeout */
				nix_platform_set_errno(ETIMEDOUT);
				return -1;
			}
			/* Try again */
			pipe = CreateFileA(pipe_path, GENERIC_READ | GENERIC_WRITE, 0, NULL,
							   OPEN_EXISTING, 0, NULL);
			if (pipe == INVALID_HANDLE_VALUE) {
				nix_platform_set_errno(ECONNREFUSED);
				return -1;
			}
		} else {
			nix_platform_set_errno(ECONNREFUSED);
		}
		return -1;
	}

	/* Set pipe mode for message-oriented sockets */
	if (sock->type == 5) {  /* SOCK_SEQPACKET */
		DWORD mode = PIPE_READMODE_MESSAGE | PIPE_WAIT;
		SetNamedPipeHandleState(pipe, &mode, NULL, NULL);
	}

	sock->handle = pipe;
	sock->connected = 1;
	sock->state = AFLOCAL_STATE_CONNECTED;
	strncpy(sock->path, path, sizeof(sock->path) - 1);
	sock->path[sizeof(sock->path) - 1] = '\0';

	return 0;
}

/*
 * ========================================================================
 * SEND / RECV
 * ========================================================================
 */

ssize_t
nix_platform_win32_aflocal_send(int sockfd, const void *buf, size_t len, int flags)
{
	af_local_socket_t *sock = aflocal_find(sockfd);
	if (sock == NULL) {
		nix_platform_set_errno(EBADF);
		return -1;
	}

	if (sock->type == 2) {  /* SOCK_DGRAM */
		/* Send to connected peer or last specified address */
		if (!sock->connected) {
			nix_platform_set_errno(EDESTADDRREQ);
			return -1;
		}

		char mailslot_path[MAX_PATH];
		unix_path_to_mailslot(sock->path, mailslot_path, sizeof(mailslot_path));

		HANDLE mailslot = CreateFileA(
			mailslot_path,
			GENERIC_WRITE,
			FILE_SHARE_READ,
			NULL,
			OPEN_EXISTING,
			0,
			NULL
		);

		if (mailslot == INVALID_HANDLE_VALUE) {
			nix_platform_set_errno(ECONNREFUSED);
			return -1;
		}

		DWORD written;
		BOOL success = WriteFile(mailslot, buf, (DWORD)len, &written, NULL);
		CloseHandle(mailslot);

		if (!success) {
			nix_platform_set_errno(EIO);
			return -1;
		}

		return (ssize_t)written;
	}

	/* SOCK_STREAM or SOCK_SEQPACKET */
	if (!sock->connected || sock->handle == INVALID_HANDLE_VALUE) {
		nix_platform_set_errno(ENOTCONN);
		return -1;
	}

	DWORD written;
	BOOL success = WriteFile(sock->handle, buf, (DWORD)len, &written, NULL);

	if (!success) {
		DWORD err = GetLastError();
		if (err == ERROR_BROKEN_PIPE || err == ERROR_NO_DATA) {
			nix_platform_set_errno(EPIPE);
		} else {
			nix_platform_set_errno(EIO);
		}
		return -1;
	}

	return (ssize_t)written;
}

ssize_t
nix_platform_win32_aflocal_recv(int sockfd, void *buf, size_t len, int flags)
{
	af_local_socket_t *sock = aflocal_find(sockfd);
	if (sock == NULL) {
		nix_platform_set_errno(EBADF);
		return -1;
	}

	if (sock->handle == INVALID_HANDLE_VALUE) {
		nix_platform_set_errno(ENOTCONN);
		return -1;
	}

	DWORD read_bytes;
	BOOL success = ReadFile(sock->handle, buf, (DWORD)len, &read_bytes, NULL);

	if (!success) {
		DWORD err = GetLastError();
		if (err == ERROR_BROKEN_PIPE || err == ERROR_NO_DATA) {
			/* Connection closed */
			return 0;
		} else {
			nix_platform_set_errno(EIO);
			return -1;
		}
	}

	return (ssize_t)read_bytes;
}

/*
 * ========================================================================
 * SENDTO / RECVFROM (for SOCK_DGRAM)
 * ========================================================================
 */

ssize_t
nix_platform_win32_aflocal_sendto(int sockfd, const void *buf, size_t len,
								   int flags, const char *dest_path)
{
	af_local_socket_t *sock = aflocal_find(sockfd);
	if (sock == NULL) {
		nix_platform_set_errno(EBADF);
		return -1;
	}

	if (sock->type != 2) {  /* Not SOCK_DGRAM */
		nix_platform_set_errno(EISCONN);
		return -1;
	}

	char mailslot_path[MAX_PATH];
	unix_path_to_mailslot(dest_path, mailslot_path, sizeof(mailslot_path));

	HANDLE mailslot = CreateFileA(
		mailslot_path,
		GENERIC_WRITE,
		FILE_SHARE_READ,
		NULL,
		OPEN_EXISTING,
		0,
		NULL
	);

	if (mailslot == INVALID_HANDLE_VALUE) {
		nix_platform_set_errno(ECONNREFUSED);
		return -1;
	}

	DWORD written;
	BOOL success = WriteFile(mailslot, buf, (DWORD)len, &written, NULL);
	CloseHandle(mailslot);

	if (!success) {
		nix_platform_set_errno(EIO);
		return -1;
	}

	return (ssize_t)written;
}

ssize_t
nix_platform_win32_aflocal_recvfrom(int sockfd, void *buf, size_t len,
									int flags, char *src_path, size_t *pathlen)
{
	af_local_socket_t *sock = aflocal_find(sockfd);
	if (sock == NULL) {
		nix_platform_set_errno(EBADF);
		return -1;
	}

	if (sock->type != 2 || sock->handle == INVALID_HANDLE_VALUE) {
		nix_platform_set_errno(EINVAL);
		return -1;
	}

	DWORD read_bytes;
	BOOL success = ReadFile(sock->handle, buf, (DWORD)len, &read_bytes, NULL);

	if (!success) {
		nix_platform_set_errno(EIO);
		return -1;
	}

	/* For mailslots, we can't determine the sender, so just return the bound path */
	if (src_path != NULL && pathlen != NULL) {
		size_t copy_len = strlen(sock->path) + 1;
		if (copy_len > *pathlen) {
			copy_len = *pathlen;
		}
		memcpy(src_path, sock->path, copy_len);
		*pathlen = copy_len;
	}

	return (ssize_t)read_bytes;
}

/*
 * ========================================================================
 * SOCKETPAIR
 * ========================================================================
 */

int
nix_platform_win32_aflocal_socketpair(int type, int protocol, int sv[2])
{
	/* Create a connected pair of AF_LOCAL sockets */
	if (type != 1 && type != 2 && type != 5) {
		nix_platform_set_errno(EPROTONOSUPPORT);
		return -1;
	}

	/* Generate unique pipe name */
	char pipe_name[MAX_PATH];
	static LONG counter = 0;
	InterlockedIncrement(&counter);
	_snprintf(pipe_name, sizeof(pipe_name),
			  "\\\\.\\pipe\\nix_socketpair_%u_%ld",
			  GetCurrentProcessId(), counter);

	if (type == 2) {  /* SOCK_DGRAM */
		/* For SOCK_DGRAM, use two mailslots */
		char mailslot1[MAX_PATH], mailslot2[MAX_PATH];
		_snprintf(mailslot1, sizeof(mailslot1),
				  "\\\\.\\mailslot\\nix_socketpair_%u_%ld_1",
				  GetCurrentProcessId(), counter);
		_snprintf(mailslot2, sizeof(mailslot2),
				  "\\\\.\\mailslot\\nix_socketpair_%u_%ld_2",
				  GetCurrentProcessId(), counter);

		/* Create two sockets */
		af_local_socket_t *sock1 = aflocal_alloc(type);
		af_local_socket_t *sock2 = aflocal_alloc(type);

		if (sock1 == NULL || sock2 == NULL) {
			if (sock1) aflocal_free(sock1->sockfd);
			if (sock2) aflocal_free(sock2->sockfd);
			nix_platform_set_errno(ENOMEM);
			return -1;
		}

		/* Create mailslots */
		sock1->handle = CreateMailslotA(mailslot1, 0, MAILSLOT_WAIT_FOREVER, NULL);
		sock2->handle = CreateMailslotA(mailslot2, 0, MAILSLOT_WAIT_FOREVER, NULL);

		if (sock1->handle == INVALID_HANDLE_VALUE ||
			sock2->handle == INVALID_HANDLE_VALUE) {
			aflocal_free(sock1->sockfd);
			aflocal_free(sock2->sockfd);
			nix_platform_set_errno(EINVAL);
			return -1;
		}

		/* Connect them to each other */
		strcpy(sock1->path, mailslot2);
		strcpy(sock2->path, mailslot1);
		sock1->connected = 1;
		sock2->connected = 1;
		sock1->state = AFLOCAL_STATE_CONNECTED;
		sock2->state = AFLOCAL_STATE_CONNECTED;

		sv[0] = sock1->sockfd;
		sv[1] = sock2->sockfd;

	} else {  /* SOCK_STREAM or SOCK_SEQPACKET */
		/* Use named pipes */
		DWORD pipe_mode;
		if (type == 5) {  /* SOCK_SEQPACKET */
			pipe_mode = PIPE_TYPE_MESSAGE | PIPE_READMODE_MESSAGE | PIPE_WAIT;
		} else {
			pipe_mode = PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT;
		}

		/* Create server pipe */
		HANDLE server_pipe = CreateNamedPipeA(
			pipe_name,
			PIPE_ACCESS_DUPLEX,
			pipe_mode,
			1,      /* Only one instance needed */
			4096, 4096, 0, NULL
		);

		if (server_pipe == INVALID_HANDLE_VALUE) {
			nix_platform_set_errno(EINVAL);
			return -1;
		}

		/* Connect client */
		HANDLE client_pipe = CreateFileA(
			pipe_name,
			GENERIC_READ | GENERIC_WRITE,
			0, NULL, OPEN_EXISTING, 0, NULL
		);

		if (client_pipe == INVALID_HANDLE_VALUE) {
			CloseHandle(server_pipe);
			nix_platform_set_errno(EINVAL);
			return -1;
		}

		/* Create socket structures */
		af_local_socket_t *sock1 = aflocal_alloc(type);
		af_local_socket_t *sock2 = aflocal_alloc(type);

		if (sock1 == NULL || sock2 == NULL) {
			if (sock1) aflocal_free(sock1->sockfd);
			if (sock2) aflocal_free(sock2->sockfd);
			CloseHandle(server_pipe);
			CloseHandle(client_pipe);
			nix_platform_set_errno(ENOMEM);
			return -1;
		}

		sock1->handle = server_pipe;
		sock2->handle = client_pipe;
		sock1->connected = 1;
		sock2->connected = 1;
		sock1->state = AFLOCAL_STATE_CONNECTED;
		sock2->state = AFLOCAL_STATE_CONNECTED;

		sv[0] = sock1->sockfd;
		sv[1] = sock2->sockfd;
	}

	return 0;
}

/*
 * ========================================================================
 * CLOSE
 * ========================================================================
 */

int
nix_platform_win32_aflocal_close(int sockfd)
{
	af_local_socket_t *sock = aflocal_find(sockfd);
	if (sock == NULL) {
		nix_platform_set_errno(EBADF);
		return -1;
	}

	/* Close handles and free */
	aflocal_free(sockfd);

	return 0;
}

/*
 * ========================================================================
 * SHUTDOWN
 * ========================================================================
 */

int
nix_platform_win32_aflocal_shutdown(int sockfd, int how)
{
	af_local_socket_t *sock = aflocal_find(sockfd);
	if (sock == NULL) {
		nix_platform_set_errno(EBADF);
		return -1;
	}

	if (!sock->connected) {
		nix_platform_set_errno(ENOTCONN);
		return -1;
	}

	/* For named pipes, we can't do partial shutdown */
	/* Just disconnect */
	if (sock->handle != INVALID_HANDLE_VALUE) {
		if (sock->type != 2) {  /* Not SOCK_DGRAM */
			DisconnectNamedPipe(sock->handle);
		}
	}

	return 0;
}

/*
 * ========================================================================
 * SOCKET OPTIONS
 * ========================================================================
 */

int
nix_platform_win32_aflocal_getsockopt(int sockfd, int level, int optname,
									  void *optval, socklen_t *optlen)
{
	af_local_socket_t *sock = aflocal_find(sockfd);
	if (sock == NULL) {
		nix_platform_set_errno(EBADF);
		return -1;
	}

	/* Basic socket options */
	if (level == 1) {  /* SOL_SOCKET */
		switch (optname) {
		case 7:  /* SO_TYPE */
			if (*optlen >= sizeof(int)) {
				*(int *)optval = sock->type;
				*optlen = sizeof(int);
				return 0;
			}
			break;

		case 4:  /* SO_ERROR */
			if (*optlen >= sizeof(int)) {
				*(int *)optval = 0;
				*optlen = sizeof(int);
				return 0;
			}
			break;
		}
	}

	nix_platform_set_errno(ENOPROTOOPT);
	return -1;
}

#endif /* NIX_HOST_WIN32 */
