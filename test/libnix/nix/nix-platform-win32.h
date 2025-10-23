/*
 * nix-platform-win32.h
 *
 * Windows-specific platform functions for full POSIX emulation
 */

#ifndef __nix_platform_win32_h
#define __nix_platform_win32_h

#if defined(NIX_HOST_WIN32)

#include <stddef.h>
#include <stdint.h>
#include <sys/types.h>

/* Windows-specific types */
#ifndef nix_host_pid_t
typedef int nix_host_pid_t;
#endif

/* Signal handler type */
typedef void (*nix_signal_handler_t)(int);

#define NIX_SIG_DFL  ((nix_signal_handler_t)0)
#define NIX_SIG_IGN  ((nix_signal_handler_t)1)
#define NIX_SIG_ERR  ((nix_signal_handler_t)-1)

/*
 * Signal Functions
 */

/* Process pending signals - should be called periodically */
int nix_platform_win32_signal_process_pending(void);

/* Set signal handler */
nix_signal_handler_t nix_platform_win32_signal(int signo, nix_signal_handler_t handler);

/* Send signal to process */
int nix_platform_win32_kill(nix_host_pid_t pid, int signo);

/* Raise signal (send to self) */
int nix_platform_win32_raise(int signo);

/* Signal mask operations */
int nix_platform_win32_sigprocmask(int how, const uint64_t *set, uint64_t *oldset);

/* Get pending signals */
int nix_platform_win32_sigpending(uint64_t *set);

/* Suspend until signal received */
int nix_platform_win32_sigsuspend(const uint64_t *mask);

/* Full sigaction */
int nix_platform_win32_sigaction(int signo, const void *act, void *oldact);

/* Alternate signal stack */
int nix_platform_win32_sigaltstack(const void *ss, void *oss);

/* Interval timers */
int nix_platform_win32_setitimer(int which, const void *value, void *ovalue);
int nix_platform_win32_getitimer(int which, void *value);

/*
 * Memory Mapping Functions
 */

/* Memory map file or anonymous memory */
void *nix_platform_win32_mmap(void *addr, size_t length, int prot, int flags,
							  int fd, off_t offset);

/* Unmap memory */
int nix_platform_win32_munmap(void *addr, size_t length);

/* Change memory protection */
int nix_platform_win32_mprotect(void *addr, size_t length, int prot);

/* Synchronize mapped region with backing store */
int nix_platform_win32_msync(void *addr, size_t length, int flags);

/* Give advice about memory usage */
int nix_platform_win32_madvise(void *addr, size_t length, int advice);

/* Lock memory pages */
int nix_platform_win32_mlock(void *addr, size_t length);

/* Unlock memory pages */
int nix_platform_win32_munlock(void *addr, size_t length);

/* Lock all memory pages */
int nix_platform_win32_mlockall(int flags);

/* Unlock all memory pages */
int nix_platform_win32_munlockall(void);

/* Determine page residency */
int nix_platform_win32_mincore(void *addr, size_t length, unsigned char *vec);

/* Get system page size */
size_t nix_platform_win32_getpagesize(void);

/*
 * ICMP (Internet Control Message Protocol) Functions
 */

/* Create ICMP raw socket (requires Administrator privileges) */
int nix_platform_win32_icmp_socket(int family);

/* Send ICMP Echo Request (ping request) */
ssize_t nix_platform_win32_icmp_echo_request(
	int sockfd,
	const struct sockaddr *dest_addr,
	socklen_t addrlen,
	uint16_t id,
	uint16_t sequence,
	const void *data,
	size_t data_len
);

/* Receive ICMP Echo Reply (ping reply) */
ssize_t nix_platform_win32_icmp_echo_reply(
	int sockfd,
	struct sockaddr *src_addr,
	socklen_t *addrlen,
	uint16_t *id,
	uint16_t *sequence,
	void *data,
	size_t data_len,
	uint8_t *ttl
);

/* Simplified ping using Windows IcmpSendEcho API (no admin needed) */
int nix_platform_win32_icmp_ping(
	const char *host,
	uint32_t timeout_ms,
	uint32_t *rtt_ms,
	uint8_t *ttl
);

/* Set TTL (Time to Live) for ICMP socket */
int nix_platform_win32_icmp_set_ttl(int sockfd, int ttl);

/* Get TTL (Time to Live) for ICMP socket */
int nix_platform_win32_icmp_get_ttl(int sockfd);

/* Set receive timeout for ICMP socket */
int nix_platform_win32_icmp_set_timeout(int sockfd, uint32_t timeout_ms);

/* Get ICMP message type name (for debugging) */
const char *nix_platform_win32_icmp_type_name(uint8_t type);

/* Check if running with administrator privileges */
int nix_platform_win32_icmp_check_admin(void);

/*
 * AF_LOCAL (Unix Domain Sockets) Functions
 */

/* Create AF_LOCAL socket */
int nix_platform_win32_aflocal_socket(int type, int protocol);

/* Bind AF_LOCAL socket to path */
int nix_platform_win32_aflocal_bind(int sockfd, const char *path);

/* Listen for connections */
int nix_platform_win32_aflocal_listen(int sockfd, int backlog);

/* Accept connection */
int nix_platform_win32_aflocal_accept(int sockfd, char *addr, size_t *addrlen);

/* Connect to AF_LOCAL socket */
int nix_platform_win32_aflocal_connect(int sockfd, const char *path);

/* Send data */
ssize_t nix_platform_win32_aflocal_send(int sockfd, const void *buf, size_t len, int flags);

/* Receive data */
ssize_t nix_platform_win32_aflocal_recv(int sockfd, void *buf, size_t len, int flags);

/* Send datagram to address */
ssize_t nix_platform_win32_aflocal_sendto(int sockfd, const void *buf, size_t len,
										  int flags, const char *dest_path);

/* Receive datagram with source address */
ssize_t nix_platform_win32_aflocal_recvfrom(int sockfd, void *buf, size_t len,
											int flags, char *src_path, size_t *pathlen);

/* Create connected socket pair */
int nix_platform_win32_aflocal_socketpair(int type, int protocol, int sv[2]);

/* Close socket */
int nix_platform_win32_aflocal_close(int sockfd);

/* Shutdown connection */
int nix_platform_win32_aflocal_shutdown(int sockfd, int how);

/* Get socket options */
int nix_platform_win32_aflocal_getsockopt(int sockfd, int level, int optname,
										   void *optval, socklen_t *optlen);

/*
 * Advanced AF_LOCAL Features
 */

/* Peer credentials structure */
typedef struct {
	DWORD pid;  /* Process ID */
	DWORD uid;  /* User ID (RID from SID) */
	DWORD gid;  /* Group ID (RID from SID) */
} nix_ucred_t;

/* I/O vector for scatter/gather I/O */
typedef struct {
	void *iov_base;    /* Starting address */
	size_t iov_len;    /* Number of bytes */
} nix_iovec_t;

/* Message header for sendmsg/recvmsg */
typedef struct {
	void *msg_name;           /* Address (unused for connected sockets) */
	size_t msg_namelen;       /* Address length */
	nix_iovec_t *msg_iov;     /* I/O vector */
	size_t msg_iovlen;        /* Number of elements in msg_iov */
	void *msg_control;        /* Ancillary data */
	size_t msg_controllen;    /* Ancillary data length */
	int msg_flags;            /* Flags on received message */
} nix_msghdr_t;

/* Control message header */
typedef struct {
	size_t cmsg_len;    /* Length including header */
	int cmsg_level;     /* Originating protocol */
	int cmsg_type;      /* Protocol-specific type */
	/* Followed by unsigned char cmsg_data[] */
} nix_cmsghdr_t;

/* Control message macros */
#define NIX_CMSG_ALIGN(len) (((len) + sizeof(size_t) - 1) & ~(sizeof(size_t) - 1))
#define NIX_CMSG_SPACE(len) (NIX_CMSG_ALIGN(sizeof(nix_cmsghdr_t)) + NIX_CMSG_ALIGN(len))
#define NIX_CMSG_LEN(len)   (NIX_CMSG_ALIGN(sizeof(nix_cmsghdr_t)) + (len))

#define NIX_CMSG_FIRSTHDR(mhdr) \
	((mhdr)->msg_controllen >= sizeof(nix_cmsghdr_t) ? \
	 (nix_cmsghdr_t *)(mhdr)->msg_control : (nix_cmsghdr_t *)NULL)

#define NIX_CMSG_DATA(cmsg) \
	((unsigned char *)((nix_cmsghdr_t *)(cmsg) + 1))

/* Get peer credentials */
int nix_platform_win32_aflocal_getpeercred(int sockfd, nix_ucred_t *cred);

/* Send message with control data */
ssize_t nix_platform_win32_aflocal_sendmsg(int sockfd, const nix_msghdr_t *msg, int flags);

/* Receive message with control data */
ssize_t nix_platform_win32_aflocal_recvmsg(int sockfd, nix_msghdr_t *msg, int flags);

/* Extended getsockopt for SO_PEERCRED */
int nix_platform_win32_aflocal_getsockopt_ex(int sockfd, int level, int optname,
											  void *optval, socklen_t *optlen);

/*
 * Symbolic Links and Hard Links
 */

/* Create symbolic link (Vista+ native, NT+ via junctions) */
int nix_platform_win32_symlink(const char *target, const char *linkpath);

/* Read symbolic link target */
ssize_t nix_platform_win32_readlink(const char *path, char *buf, size_t bufsiz);

/* Create hard link (Windows 2000+) */
int nix_platform_win32_link(const char *oldpath, const char *newpath);

/*
 * Extended Attributes (via Alternate Data Streams)
 */

/* Set extended attribute with offset support */
int nix_platform_win32_setxattr(const char *path, const char *name,
								 const void *value, size_t size, size_t offset, int flags);

/* Get extended attribute with offset support */
ssize_t nix_platform_win32_getxattr(const char *path, const char *name,
									void *value, size_t size, size_t offset);

/* List extended attributes */
ssize_t nix_platform_win32_listxattr(const char *path, char *list, size_t size);

/* Remove extended attribute */
int nix_platform_win32_removexattr(const char *path, const char *name);

/*
 * macOS Compatibility Functions
 */

/* Get resource fork (com.apple.ResourceFork) with offset */
ssize_t nix_platform_win32_getresourcefork(const char *path, void *data,
											size_t size, size_t offset);

/* Set resource fork with offset */
int nix_platform_win32_setresourcefork(const char *path, const void *data,
										size_t size, size_t offset);

/* Get Finder info (com.apple.FinderInfo, 32 bytes) */
ssize_t nix_platform_win32_getfinderinfo(const char *path, void *info);

/* Set Finder info (32 bytes) */
int nix_platform_win32_setfinderinfo(const char *path, const void *info);

/*
 * BSD File Flags (chflags)
 */

/* Set file flags */
int nix_platform_win32_chflags(const char *path, unsigned long flags);

/* Set file flags by fd */
int nix_platform_win32_fchflags(int fd, unsigned long flags);

/* Set file flags without following symlinks */
int nix_platform_win32_lchflags(const char *path, unsigned long flags);

/*
 * lstat and lch* Functions (symlink-aware stat/chmod/chown)
 */

/* Stat without following symlinks */
int nix_platform_win32_lstat(const char *path, struct stat *st);

/* Chown without following symlinks */
int nix_platform_win32_lchown(const char *path, uid_t owner, gid_t group);

/* Chmod without following symlinks */
int nix_platform_win32_lchmod(const char *path, mode_t mode);

/*
 * lutime*/futimes Functions (time modification)
 */

/* utimes without following symlinks */
int nix_platform_win32_lutimes(const char *path, const struct timeval tv[2]);

/* utimes by file descriptor */
int nix_platform_win32_futimes(int fd, const struct timeval tv[2]);

/*
 * fcntl (file descriptor control)
 */

/* File control operations */
int nix_platform_win32_fcntl(int fd, int cmd, ...);

/*
 * *at Functions (directory-relative operations)
 */

/* Open relative to directory fd */
int nix_platform_win32_openat(int dirfd, const char *pathname, int flags, ...);

/* Stat relative to directory fd */
int nix_platform_win32_fstatat(int dirfd, const char *pathname, struct stat *st, int flags);

/* Chown relative to directory fd */
int nix_platform_win32_fchownat(int dirfd, const char *pathname, uid_t owner, gid_t group, int flags);

/* Chmod relative to directory fd */
int nix_platform_win32_fchmodat(int dirfd, const char *pathname, mode_t mode, int flags);

/* utimens relative to directory fd */
int nix_platform_win32_utimensat(int dirfd, const char *pathname,
								  const struct timespec times[2], int flags);

/* Symlink relative to directory fd */
int nix_platform_win32_symlinkat(const char *target, int newdirfd, const char *linkpath);

/* Hard link relative to directory fd */
int nix_platform_win32_linkat(int olddirfd, const char *oldpath,
							   int newdirfd, const char *newpath, int flags);

/* Readlink relative to directory fd */
ssize_t nix_platform_win32_readlinkat(int dirfd, const char *pathname, char *buf, size_t bufsiz);

/* Unlink relative to directory fd */
int nix_platform_win32_unlinkat(int dirfd, const char *pathname, int flags);

/* Mkdir relative to directory fd */
int nix_platform_win32_mkdirat(int dirfd, const char *pathname, mode_t mode);

/*
 * POSIX Protection Flags (for reference)
 */
#define NIX_PROT_NONE   0x00
#define NIX_PROT_READ   0x01
#define NIX_PROT_WRITE  0x02
#define NIX_PROT_EXEC   0x04

/*
 * POSIX mmap Flags (for reference)
 */
#define NIX_MAP_SHARED     0x0001
#define NIX_MAP_PRIVATE    0x0002
#define NIX_MAP_FIXED      0x0010
#define NIX_MAP_ANONYMOUS  0x0020
#define NIX_MAP_ANON       NIX_MAP_ANONYMOUS

/*
 * msync Flags
 */
#define NIX_MS_ASYNC       0x01
#define NIX_MS_INVALIDATE  0x02
#define NIX_MS_SYNC        0x04

/*
 * madvise Advice
 */
#define NIX_MADV_NORMAL     0
#define NIX_MADV_RANDOM     1
#define NIX_MADV_SEQUENTIAL 2
#define NIX_MADV_WILLNEED   3
#define NIX_MADV_DONTNEED   4
#define NIX_MADV_FREE       8

/*
 * mlockall Flags
 */
#define NIX_MCL_CURRENT  1
#define NIX_MCL_FUTURE   2

/*
 * Signal Mask Operations
 */
#define NIX_SIG_BLOCK    0
#define NIX_SIG_UNBLOCK  1
#define NIX_SIG_SETMASK  2

/*
 * ICMP Message Types
 */
#define NIX_ICMP_ECHO_REPLY         0
#define NIX_ICMP_DEST_UNREACH       3
#define NIX_ICMP_SOURCE_QUENCH      4
#define NIX_ICMP_REDIRECT           5
#define NIX_ICMP_ECHO_REQUEST       8
#define NIX_ICMP_ROUTER_ADVERT      9
#define NIX_ICMP_ROUTER_SOLICIT     10
#define NIX_ICMP_TIME_EXCEEDED      11
#define NIX_ICMP_PARAM_PROBLEM      12
#define NIX_ICMP_TIMESTAMP_REQUEST  13
#define NIX_ICMP_TIMESTAMP_REPLY    14
#define NIX_ICMP_INFO_REQUEST       15
#define NIX_ICMP_INFO_REPLY         16
#define NIX_ICMP_ADDRESS_REQUEST    17
#define NIX_ICMP_ADDRESS_REPLY      18

/*
 * ICMP Destination Unreachable Codes
 */
#define NIX_ICMP_NET_UNREACH        0
#define NIX_ICMP_HOST_UNREACH       1
#define NIX_ICMP_PROT_UNREACH       2
#define NIX_ICMP_PORT_UNREACH       3
#define NIX_ICMP_FRAG_NEEDED        4
#define NIX_ICMP_SR_FAILED          5

/*
 * ICMP Time Exceeded Codes
 */
#define NIX_ICMP_EXC_TTL            0
#define NIX_ICMP_EXC_FRAGTIME       1

/*
 * Protocol Numbers
 */
#ifndef IPPROTO_ICMP
#define IPPROTO_ICMP    1
#endif
#ifndef IPPROTO_ICMPV6
#define IPPROTO_ICMPV6  58
#endif

/*
 * Standard Signal Numbers (subset)
 */
#define NIX_SIGHUP     1
#define NIX_SIGINT     2
#define NIX_SIGQUIT    3
#define NIX_SIGILL     4
#define NIX_SIGTRAP    5
#define NIX_SIGABRT    6
#define NIX_SIGBUS     7
#define NIX_SIGFPE     8
#define NIX_SIGKILL    9
#define NIX_SIGUSR1   10
#define NIX_SIGSEGV   11
#define NIX_SIGUSR2   12
#define NIX_SIGPIPE   13
#define NIX_SIGALRM   14
#define NIX_SIGTERM   15
#define NIX_SIGSTKFLT 16
#define NIX_SIGCHLD   17
#define NIX_SIGCONT   18
#define NIX_SIGSTOP   19
#define NIX_SIGTSTP   20
#define NIX_SIGTTIN   21
#define NIX_SIGTTOU   22
#define NIX_SIGURG    23
#define NIX_SIGXCPU   24
#define NIX_SIGXFSZ   25
#define NIX_SIGVTALRM 26
#define NIX_SIGPROF   27
#define NIX_SIGWINCH  28
#define NIX_SIGIO     29
#define NIX_SIGPWR    30
#define NIX_SIGSYS    31

/*
 * Socket Types (for AF_LOCAL)
 */
#ifndef SOCK_STREAM
#define SOCK_STREAM    1
#endif
#ifndef SOCK_DGRAM
#define SOCK_DGRAM     2
#endif
#ifndef SOCK_SEQPACKET
#define SOCK_SEQPACKET 5
#endif

/*
 * Shutdown options
 */
#ifndef SHUT_RD
#define SHUT_RD   0
#endif
#ifndef SHUT_WR
#define SHUT_WR   1
#endif
#ifndef SHUT_RDWR
#define SHUT_RDWR 2
#endif

/*
 * Socket options for AF_LOCAL
 */
#ifndef SOL_SOCKET
#define SOL_SOCKET   1
#endif
#ifndef SO_PEERCRED
#define SO_PEERCRED  17
#endif
#ifndef SO_TYPE
#define SO_TYPE      3
#endif
#ifndef SO_ERROR
#define SO_ERROR     4
#endif

/*
 * Control message types
 */
#ifndef SCM_RIGHTS
#define SCM_RIGHTS       1
#endif
#ifndef SCM_CREDENTIALS
#define SCM_CREDENTIALS  2
#endif

/*
 * Message flags
 */
#ifndef MSG_CTRUNC
#define MSG_CTRUNC  0x0008  /* Control data truncated */
#endif
#ifndef MSG_TRUNC
#define MSG_TRUNC   0x0020  /* Message truncated */
#endif

/*
 * Extended attribute flags
 */
#ifndef XATTR_CREATE
#define XATTR_CREATE  0x01  /* Create only, fail if exists */
#endif
#ifndef XATTR_REPLACE
#define XATTR_REPLACE 0x02  /* Replace only, fail if doesn't exist */
#endif

/*
 * Extended attribute constants
 */
#define XATTR_NAME_MAX     127
#define XATTR_SIZE_MAX     (64 * 1024)
#define XATTR_LIST_MAX     (64 * 1024)

/*
 * macOS extended attribute names
 */
#define XATTR_RESOURCEFORK_NAME "com.apple.ResourceFork"
#define XATTR_FINDERINFO_NAME   "com.apple.FinderInfo"

/*
 * Error codes for xattr
 */
#ifndef ENOATTR
#define ENOATTR  93  /* Attribute not found */
#endif

/*
 * BSD File Flags
 */
#ifndef UF_NODUMP
#define UF_NODUMP      0x00000001  /* Do not dump file */
#define UF_IMMUTABLE   0x00000002  /* File may not be changed */
#define UF_APPEND      0x00000004  /* Writes may only append */
#define UF_OPAQUE      0x00000008  /* Directory is opaque (union mounts) */
#define UF_NOUNLINK    0x00000010  /* File may not be removed or renamed */
#define UF_HIDDEN      0x00008000  /* Windows hidden file */

#define SF_ARCHIVED    0x00010000  /* File is archived */
#define SF_IMMUTABLE   0x00020000  /* File may not be changed */
#define SF_APPEND      0x00040000  /* Writes may only append */
#define SF_NOUNLINK    0x00100000  /* File may not be removed or renamed */
#endif

/*
 * fcntl commands
 */
#ifndef F_DUPFD
#define F_DUPFD        0   /* Duplicate file descriptor */
#define F_GETFD        1   /* Get file descriptor flags */
#define F_SETFD        2   /* Set file descriptor flags */
#define F_GETFL        3   /* Get file status flags */
#define F_SETFL        4   /* Set file status flags */
#define F_GETOWN       5   /* Get owner */
#define F_SETOWN       6   /* Set owner */
#define F_GETLK        7   /* Get lock */
#define F_SETLK        8   /* Set lock */
#define F_SETLKW       9   /* Set lock and wait */
#endif

/*
 * File descriptor flags (for F_GETFD/F_SETFD)
 */
#ifndef FD_CLOEXEC
#define FD_CLOEXEC     1   /* Close on exec */
#endif

/*
 * *at function flags
 */
#ifndef AT_FDCWD
#define AT_FDCWD              -100  /* Use current working directory */
#define AT_SYMLINK_NOFOLLOW   0x100 /* Do not follow symbolic links */
#define AT_SYMLINK_FOLLOW     0x400 /* Follow symbolic links */
#define AT_REMOVEDIR          0x200 /* Remove directory instead of file */
#define AT_EACCESS            0x200 /* Use effective IDs for access check */
#endif

#endif /* NIX_HOST_WIN32 */

#endif /* __nix_platform_win32_h */
