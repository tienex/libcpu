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

#endif /* NIX_HOST_WIN32 */

#endif /* __nix_platform_win32_h */
