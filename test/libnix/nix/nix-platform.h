/*
 * nix-platform.h
 *
 * Platform Detection and Abstraction Layer for libnix
 *
 * This header provides comprehensive platform detection across multiple
 * operating systems and architectures, enabling libnix to be hosted on:
 * - POSIX/Unix systems (Linux, BSD, macOS, etc.)
 * - Win32/Win64 (Windows NT 4.0+)
 * - Haiku (BeOS successor)
 * - OS/2 2.x+ (Warp and later)
 * - OpenVMS (Alpha, VAX, Itanium)
 */

#ifndef __nix_platform_h
#define __nix_platform_h

/*
 * ========================================================================
 * PLATFORM DETECTION
 * ========================================================================
 */

/* Detect Operating System */
#if defined(_WIN32) || defined(_WIN64) || defined(__WIN32__) || defined(__WINDOWS__)
# define NIX_HOST_WIN32 1
# ifdef _WIN64
#  define NIX_HOST_WIN64 1
# endif
#elif defined(__HAIKU__)
# define NIX_HOST_HAIKU 1
#elif defined(__OS2__) || defined(__EMX__)
# define NIX_HOST_OS2 1
#elif defined(__VMS) || defined(VMS) || defined(__VMS_VER)
# define NIX_HOST_OPENVMS 1
#elif defined(__linux__) || defined(__linux) || defined(linux)
# define NIX_HOST_LINUX 1
# define NIX_HOST_UNIX 1
#elif defined(__FreeBSD__) || defined(__FreeBSD_kernel__)
# define NIX_HOST_FREEBSD 1
# define NIX_HOST_BSD 1
# define NIX_HOST_UNIX 1
#elif defined(__OpenBSD__)
# define NIX_HOST_OPENBSD 1
# define NIX_HOST_BSD 1
# define NIX_HOST_UNIX 1
#elif defined(__NetBSD__)
# define NIX_HOST_NETBSD 1
# define NIX_HOST_BSD 1
# define NIX_HOST_UNIX 1
#elif defined(__DragonFly__)
# define NIX_HOST_DRAGONFLYBSD 1
# define NIX_HOST_BSD 1
# define NIX_HOST_UNIX 1
#elif defined(__APPLE__) && defined(__MACH__)
# define NIX_HOST_DARWIN 1
# define NIX_HOST_MACOS 1
# define NIX_HOST_BSD 1
# define NIX_HOST_UNIX 1
#elif defined(__sun) || defined(sun)
# define NIX_HOST_SOLARIS 1
# define NIX_HOST_UNIX 1
#elif defined(__hpux) || defined(hpux) || defined(_hpux)
# define NIX_HOST_HPUX 1
# define NIX_HOST_UNIX 1
#elif defined(_AIX)
# define NIX_HOST_AIX 1
# define NIX_HOST_UNIX 1
#elif defined(__QNX__) || defined(__QNXNTO__)
# define NIX_HOST_QNX 1
# define NIX_HOST_UNIX 1
#else
# warning "Unknown operating system - assuming generic Unix"
# define NIX_HOST_GENERIC_UNIX 1
# define NIX_HOST_UNIX 1
#endif

/* Detect Architecture */
#if defined(__x86_64__) || defined(__x86_64) || defined(__amd64__) || defined(__amd64) || defined(_M_X64) || defined(_M_AMD64)
# define NIX_HOST_ARCH_X86_64 1
# define NIX_HOST_ARCH_64BIT 1
# define NIX_HOST_ARCH "x86_64"
#elif defined(__i386__) || defined(__i386) || defined(_M_IX86) || defined(_X86_) || defined(__X86__)
# define NIX_HOST_ARCH_I386 1
# define NIX_HOST_ARCH_32BIT 1
# define NIX_HOST_ARCH "i386"
#elif defined(__aarch64__) || defined(_M_ARM64)
# define NIX_HOST_ARCH_AARCH64 1
# define NIX_HOST_ARCH_64BIT 1
# define NIX_HOST_ARCH "aarch64"
#elif defined(__arm__) || defined(_M_ARM)
# define NIX_HOST_ARCH_ARM 1
# define NIX_HOST_ARCH_32BIT 1
# define NIX_HOST_ARCH "arm"
#elif defined(__alpha__) || defined(__alpha)
# define NIX_HOST_ARCH_ALPHA 1
# define NIX_HOST_ARCH_64BIT 1
# define NIX_HOST_ARCH "alpha"
#elif defined(__ia64__) || defined(_M_IA64)
# define NIX_HOST_ARCH_IA64 1
# define NIX_HOST_ARCH_64BIT 1
# define NIX_HOST_ARCH "ia64"
#elif defined(__powerpc64__) || defined(__ppc64__) || defined(_ARCH_PPC64)
# define NIX_HOST_ARCH_PPC64 1
# define NIX_HOST_ARCH_64BIT 1
# define NIX_HOST_ARCH "powerpc64"
#elif defined(__powerpc__) || defined(__ppc__) || defined(_ARCH_PPC)
# define NIX_HOST_ARCH_PPC 1
# define NIX_HOST_ARCH_32BIT 1
# define NIX_HOST_ARCH "powerpc"
#elif defined(__m68k__)
# define NIX_HOST_ARCH_M68K 1
# define NIX_HOST_ARCH_32BIT 1
# define NIX_HOST_ARCH "m68k"
#elif defined(__mips64)
# define NIX_HOST_ARCH_MIPS64 1
# define NIX_HOST_ARCH_64BIT 1
# define NIX_HOST_ARCH "mips64"
#elif defined(__mips__)
# define NIX_HOST_ARCH_MIPS 1
# define NIX_HOST_ARCH_32BIT 1
# define NIX_HOST_ARCH "mips"
#elif defined(__sparc64__)
# define NIX_HOST_ARCH_SPARC64 1
# define NIX_HOST_ARCH_64BIT 1
# define NIX_HOST_ARCH "sparc64"
#elif defined(__sparc__)
# define NIX_HOST_ARCH_SPARC 1
# define NIX_HOST_ARCH_32BIT 1
# define NIX_HOST_ARCH "sparc"
#elif defined(__riscv) && (__riscv_xlen == 64)
# define NIX_HOST_ARCH_RISCV64 1
# define NIX_HOST_ARCH_64BIT 1
# define NIX_HOST_ARCH "riscv64"
#elif defined(__riscv)
# define NIX_HOST_ARCH_RISCV32 1
# define NIX_HOST_ARCH_32BIT 1
# define NIX_HOST_ARCH "riscv32"
#else
# warning "Unknown architecture"
# define NIX_HOST_ARCH "unknown"
#endif

/*
 * ========================================================================
 * PLATFORM-SPECIFIC INCLUDES
 * ========================================================================
 */

#if defined(NIX_HOST_WIN32)
/* Windows includes */
# ifndef WIN32_LEAN_AND_MEAN
#  define WIN32_LEAN_AND_MEAN
# endif
# ifndef NOMINMAX
#  define NOMINMAX
# endif
# include <windows.h>
# include <winsock2.h>
# include <ws2tcpip.h>
# include <io.h>
# include <process.h>
# include <direct.h>
# include <tlhelp32.h>
# include <psapi.h>

/* NT API declarations for advanced process/memory operations */
typedef LONG NTSTATUS;
typedef struct _OBJECT_ATTRIBUTES OBJECT_ATTRIBUTES;
typedef OBJECT_ATTRIBUTES *POBJECT_ATTRIBUTES;

#define NT_SUCCESS(Status) (((NTSTATUS)(Status)) >= 0)
#define STATUS_SUCCESS ((NTSTATUS)0x00000000L)

typedef struct _UNICODE_STRING {
    USHORT Length;
    USHORT MaximumLength;
    PWSTR  Buffer;
} UNICODE_STRING, *PUNICODE_STRING;

typedef struct _OBJECT_ATTRIBUTES {
    ULONG Length;
    HANDLE RootDirectory;
    PUNICODE_STRING ObjectName;
    ULONG Attributes;
    PVOID SecurityDescriptor;
    PVOID SecurityQualityOfService;
} OBJECT_ATTRIBUTES;

#define InitializeObjectAttributes(p, n, a, r, s) { \
    (p)->Length = sizeof(OBJECT_ATTRIBUTES); \
    (p)->RootDirectory = r; \
    (p)->Attributes = a; \
    (p)->ObjectName = n; \
    (p)->SecurityDescriptor = s; \
    (p)->SecurityQualityOfService = NULL; \
}

typedef struct _CLIENT_ID {
    HANDLE UniqueProcess;
    HANDLE UniqueThread;
} CLIENT_ID, *PCLIENT_ID;

typedef struct _PROCESS_BASIC_INFORMATION {
    PVOID Reserved1;
    PVOID PebBaseAddress;
    PVOID Reserved2[2];
    ULONG_PTR UniqueProcessId;
    PVOID Reserved3;
} PROCESS_BASIC_INFORMATION;

typedef enum _PROCESSINFOCLASS {
    ProcessBasicInformation = 0,
    ProcessDebugPort = 7,
    ProcessWow64Information = 26,
    ProcessImageFileName = 27,
    ProcessBreakOnTermination = 29
} PROCESSINFOCLASS;

typedef NTSTATUS (NTAPI *NtQueryInformationProcess_t)(
    HANDLE ProcessHandle,
    PROCESSINFOCLASS ProcessInformationClass,
    PVOID ProcessInformation,
    ULONG ProcessInformationLength,
    PULONG ReturnLength
);

typedef NTSTATUS (NTAPI *NtCreateProcess_t)(
    PHANDLE ProcessHandle,
    ACCESS_MASK DesiredAccess,
    POBJECT_ATTRIBUTES ObjectAttributes,
    HANDLE ParentProcess,
    BOOLEAN InheritObjectTable,
    HANDLE SectionHandle,
    HANDLE DebugPort,
    HANDLE ExceptionPort
);

typedef NTSTATUS (NTAPI *NtCreateThread_t)(
    PHANDLE ThreadHandle,
    ACCESS_MASK DesiredAccess,
    POBJECT_ATTRIBUTES ObjectAttributes,
    HANDLE ProcessHandle,
    PCLIENT_ID ClientId,
    PVOID ThreadContext,
    PVOID InitialTeb,
    BOOLEAN CreateSuspended
);

typedef NTSTATUS (NTAPI *RtlCreateUserThread_t)(
    HANDLE ProcessHandle,
    PSECURITY_DESCRIPTOR SecurityDescriptor,
    BOOLEAN CreateSuspended,
    ULONG StackZeroBits,
    SIZE_T StackReserve,
    SIZE_T StackCommit,
    PVOID StartAddress,
    PVOID Parameter,
    PHANDLE ThreadHandle,
    PCLIENT_ID ClientId
);

#elif defined(NIX_HOST_HAIKU)
/* Haiku includes */
# include <OS.h>
# include <kernel/OS.h>
# include <kernel/fs_attr.h>
# include <kernel/image.h>
# include <support/Errors.h>

#elif defined(NIX_HOST_OS2)
/* OS/2 includes */
# define INCL_BASE
# define INCL_DOS
# define INCL_ERRORS
# include <os2.h>

#elif defined(NIX_HOST_OPENVMS)
/* OpenVMS includes */
# include <descrip.h>
# include <ssdef.h>
# include <starlet.h>
# include <lib$routines.h>
# include <rms.h>

#elif defined(NIX_HOST_UNIX)
/* Standard POSIX/Unix includes */
# include <unistd.h>
# include <sys/types.h>
# include <sys/stat.h>
# include <sys/time.h>
# include <fcntl.h>
# include <errno.h>
# include <signal.h>

#endif

/* Common includes for all platforms */
#include <stdint.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <time.h>

/* Platform-specific time structures */
#if defined(NIX_HOST_WIN32)
struct timeval {
    long tv_sec;
    long tv_usec;
};
struct timespec {
    time_t tv_sec;
    long tv_nsec;
};
struct utimbuf {
    time_t actime;
    time_t modtime;
};
struct iovec {
    void *iov_base;
    size_t iov_len;
};
/* poll structures */
struct pollfd {
    int fd;
    short events;
    short revents;
};
typedef unsigned long nfds_t;
typedef unsigned int socklen_t;
#define POLLIN     0x0001
#define POLLOUT    0x0004
#define POLLERR    0x0008
#define POLLHUP    0x0010
#define POLLNVAL   0x0020

#elif defined(NIX_HOST_UNIX)
# include <sys/time.h>
# include <sys/uio.h>
# include <utime.h>
# include <poll.h>
# include <sys/socket.h>
#else
struct timeval {
    time_t tv_sec;
    long tv_usec;
};
struct timespec {
    time_t tv_sec;
    long tv_nsec;
};
struct utimbuf {
    time_t actime;
    time_t modtime;
};
struct iovec {
    void *iov_base;
    size_t iov_len;
};
#endif

/*
 * ========================================================================
 * PLATFORM-SPECIFIC TYPE DEFINITIONS
 * ========================================================================
 */

#if defined(NIX_HOST_WIN32)
/* Windows type mappings */
typedef HANDLE          nix_host_fd_t;
typedef DWORD           nix_host_pid_t;
typedef DWORD           nix_host_uid_t;
typedef DWORD           nix_host_gid_t;
typedef DWORD           nix_host_mode_t;
typedef LONG            nix_host_off_t;
typedef DWORD           nix_host_dev_t;
typedef DWORD           nix_host_ino_t;
typedef DWORD           nix_host_nlink_t;

# define NIX_HOST_INVALID_FD    INVALID_HANDLE_VALUE
# define NIX_HOST_PATH_MAX      MAX_PATH
# define NIX_HOST_PATH_SEP      '\\'
# define NIX_HOST_PATH_SEP_STR  "\\"

/* Wait options */
# define WNOHANG    1
# define WUNTRACED  2
# define WCONTINUED 8

/* Wait status macros */
# define WIFEXITED(status)    (((status) & 0x7f) == 0)
# define WEXITSTATUS(status)  (((status) & 0xff00) >> 8)
# define WIFSIGNALED(status)  (((status) & 0x7f) > 0 && ((status) & 0x7f) < 0x7f)
# define WTERMSIG(status)     ((status) & 0x7f)
# define WIFSTOPPED(status)   (((status) & 0xff) == 0x7f)
# define WSTOPSIG(status)     WEXITSTATUS(status)
# define WIFCONTINUED(status) ((status) == 0xffff)

/* Priority which values */
# define PRIO_PROCESS 0
# define PRIO_PGRP    1
# define PRIO_USER    2

/* Resource usage who values */
# define RUSAGE_SELF     0
# define RUSAGE_CHILDREN (-1)
# define RUSAGE_THREAD   1

/* System V IPC definitions */
typedef int key_t;

/* IPC permissions structure */
struct ipc_perm {
    key_t key;              /* Key supplied to msgget/semget/shmget */
    unsigned short uid;     /* Owner's user ID */
    unsigned short gid;     /* Owner's group ID */
    unsigned short cuid;    /* Creator's user ID */
    unsigned short cgid;    /* Creator's group ID */
    unsigned short mode;    /* Permissions */
    unsigned short seq;     /* Sequence number */
};

/* Message queue structures */
struct msqid_ds {
    struct ipc_perm msg_perm;   /* Permissions */
    void *msg_first;            /* First message (internal) */
    void *msg_last;             /* Last message (internal) */
    unsigned long msg_cbytes;   /* Current bytes in queue */
    unsigned long msg_qnum;     /* Current number of messages */
    unsigned long msg_qbytes;   /* Max bytes in queue */
    int msg_lspid;              /* PID of last msgsnd */
    int msg_lrpid;              /* PID of last msgrcv */
    time_t msg_stime;           /* Time of last msgsnd */
    time_t msg_rtime;           /* Time of last msgrcv */
    time_t msg_ctime;           /* Time of last change */
};

/* Semaphore structures */
struct semid_ds {
    struct ipc_perm sem_perm;   /* Permissions */
    time_t sem_otime;           /* Last semop time */
    time_t sem_ctime;           /* Last change time */
    unsigned short sem_nsems;   /* Number of semaphores */
};

struct sembuf {
    unsigned short sem_num;     /* Semaphore number */
    short sem_op;               /* Semaphore operation */
    short sem_flg;              /* Operation flags */
};

/* Shared memory structures */
struct shmid_ds {
    struct ipc_perm shm_perm;   /* Permissions */
    size_t shm_segsz;           /* Size of segment */
    time_t shm_atime;           /* Last attach time */
    time_t shm_dtime;           /* Last detach time */
    time_t shm_ctime;           /* Last change time */
    int shm_cpid;               /* Creator PID */
    int shm_lpid;               /* Last shmat/shmdt PID */
    unsigned short shm_nattch;  /* Number of current attaches */
};

/* IPC control commands */
# define IPC_CREAT  01000   /* Create if key doesn't exist */
# define IPC_EXCL   02000   /* Fail if key exists */
# define IPC_NOWAIT 04000   /* Return error on wait */

# define IPC_RMID   0       /* Remove identifier */
# define IPC_SET    1       /* Set options */
# define IPC_STAT   2       /* Get options */
# define IPC_INFO   3       /* Get info */

/* Special key values */
# define IPC_PRIVATE ((key_t)0)

/* Message queue limits */
# define MSGMAX     8192    /* Max message size */
# define MSGMNB     16384   /* Max bytes in queue */
# define MSGMNI     128     /* Max number of queues */

/* Semaphore limits */
# define SEMMNI     128     /* Max number of semaphore sets */
# define SEMMSL     32      /* Max semaphores per set */
# define SEMOPM     32      /* Max operations per semop call */
# define SEMVMX     32767   /* Max semaphore value */

/* Semaphore operation flags */
# define SEM_UNDO   0x1000  /* Undo on process exit */
# define GETVAL     12      /* Get semval */
# define SETVAL     16      /* Set semval */
# define GETPID     11      /* Get sempid */
# define GETNCNT    14      /* Get semncnt */
# define GETZCNT    15      /* Get semzcnt */
# define GETALL     13      /* Get all semval */
# define SETALL     17      /* Set all semval */

/* Shared memory flags */
# define SHM_RDONLY 010000  /* Attach read-only */
# define SHM_RND    020000  /* Round attach address */
# define SHM_REMAP  040000  /* Take-over region on attach */

#elif defined(NIX_HOST_HAIKU)
/* Haiku type mappings */
typedef int             nix_host_fd_t;
typedef team_id         nix_host_pid_t;
typedef uid_t           nix_host_uid_t;
typedef gid_t           nix_host_gid_t;
typedef mode_t          nix_host_mode_t;
typedef off_t           nix_host_off_t;
typedef dev_t           nix_host_dev_t;
typedef ino_t           nix_host_ino_t;
typedef nlink_t         nix_host_nlink_t;

# define NIX_HOST_INVALID_FD    (-1)
# define NIX_HOST_PATH_MAX      PATH_MAX
# define NIX_HOST_PATH_SEP      '/'
# define NIX_HOST_PATH_SEP_STR  "/"

#elif defined(NIX_HOST_OS2)
/* OS/2 type mappings */
typedef HFILE           nix_host_fd_t;
typedef PID             nix_host_pid_t;
typedef ULONG           nix_host_uid_t;
typedef ULONG           nix_host_gid_t;
typedef ULONG           nix_host_mode_t;
typedef LONG            nix_host_off_t;
typedef ULONG           nix_host_dev_t;
typedef ULONG           nix_host_ino_t;
typedef ULONG           nix_host_nlink_t;

# define NIX_HOST_INVALID_FD    ((HFILE)-1)
# define NIX_HOST_PATH_MAX      260
# define NIX_HOST_PATH_SEP      '\\'
# define NIX_HOST_PATH_SEP_STR  "\\"

#elif defined(NIX_HOST_OPENVMS)
/* OpenVMS type mappings */
typedef int             nix_host_fd_t;
typedef uint32_t        nix_host_pid_t;
typedef uint32_t        nix_host_uid_t;
typedef uint32_t        nix_host_gid_t;
typedef uint32_t        nix_host_mode_t;
typedef int64_t         nix_host_off_t;
typedef uint32_t        nix_host_dev_t;
typedef uint64_t        nix_host_ino_t;
typedef uint32_t        nix_host_nlink_t;

# define NIX_HOST_INVALID_FD    (-1)
# define NIX_HOST_PATH_MAX      256
# define NIX_HOST_PATH_SEP      ']'
# define NIX_HOST_PATH_SEP_STR  "]"

#else
/* Standard POSIX type mappings */
typedef int             nix_host_fd_t;
typedef pid_t           nix_host_pid_t;
typedef uid_t           nix_host_uid_t;
typedef gid_t           nix_host_gid_t;
typedef mode_t          nix_host_mode_t;
typedef off_t           nix_host_off_t;
typedef dev_t           nix_host_dev_t;
typedef ino_t           nix_host_ino_t;
typedef nlink_t         nix_host_nlink_t;

# define NIX_HOST_INVALID_FD    (-1)
# define NIX_HOST_PATH_MAX      1024
# define NIX_HOST_PATH_SEP      '/'
# define NIX_HOST_PATH_SEP_STR  "/"

#endif

/*
 * ========================================================================
 * PLATFORM-SPECIFIC FUNCTION DECLARATIONS
 * ========================================================================
 */

#ifdef __cplusplus
extern "C" {
#endif

/* Platform initialization */
int nix_platform_init(void);
void nix_platform_shutdown(void);

/* Platform version detection (for runtime API selection) */
#if defined(NIX_HOST_WIN32)
int nix_platform_win32_version_major(void);  /* Returns Windows version (3, 4, 5, 6, 10) */
int nix_platform_win32_version_minor(void);
int nix_platform_win32_has_api(const char *api_name);  /* Check if API is available */
#endif

/* Platform-specific file operations */
nix_host_fd_t nix_platform_open(const char *path, int flags, int mode);
int nix_platform_close(nix_host_fd_t fd);
ssize_t nix_platform_read(nix_host_fd_t fd, void *buf, size_t count);
ssize_t nix_platform_write(nix_host_fd_t fd, const void *buf, size_t count);
nix_host_off_t nix_platform_lseek(nix_host_fd_t fd, nix_host_off_t offset, int whence);
int nix_platform_dup(nix_host_fd_t fd);
int nix_platform_dup2(nix_host_fd_t oldfd, nix_host_fd_t newfd);
int nix_platform_access(const char *path, int mode);
int nix_platform_unlink(const char *path);
int nix_platform_rename(const char *oldpath, const char *newpath);
int nix_platform_fsync(nix_host_fd_t fd);
int nix_platform_truncate(const char *path, nix_host_off_t length);
int nix_platform_ftruncate(nix_host_fd_t fd, nix_host_off_t length);
ssize_t nix_platform_pread(nix_host_fd_t fd, void *buf, size_t count, nix_host_off_t offset);
ssize_t nix_platform_pwrite(nix_host_fd_t fd, const void *buf, size_t count, nix_host_off_t offset);
ssize_t nix_platform_readv(nix_host_fd_t fd, const struct iovec *iov, int iovcnt);
ssize_t nix_platform_writev(nix_host_fd_t fd, const struct iovec *iov, int iovcnt);
int nix_platform_chmod(const char *path, nix_host_mode_t mode);
int nix_platform_fchmod(nix_host_fd_t fd, nix_host_mode_t mode);
int nix_platform_chown(const char *path, nix_host_uid_t owner, nix_host_gid_t group);
int nix_platform_fchown(nix_host_fd_t fd, nix_host_uid_t owner, nix_host_gid_t group);
int nix_platform_link(const char *path1, const char *path2);
int nix_platform_symlink(const char *path1, const char *path2);
ssize_t nix_platform_readlink(const char *path, char *buf, size_t bufsiz);
int nix_platform_utime(const char *path, const struct utimbuf *times);
int nix_platform_sync(void);

/* Platform-specific directory operations */
int nix_platform_mkdir(const char *path, nix_host_mode_t mode);
int nix_platform_rmdir(const char *path);
int nix_platform_chdir(const char *path);
int nix_platform_fchdir(nix_host_fd_t fd);
char *nix_platform_getcwd(char *buf, size_t size);

/* Platform-specific stat operations */
struct nix_platform_stat {
    nix_host_dev_t     st_dev;
    nix_host_ino_t     st_ino;
    nix_host_mode_t    st_mode;
    nix_host_nlink_t   st_nlink;
    nix_host_uid_t     st_uid;
    nix_host_gid_t     st_gid;
    nix_host_dev_t     st_rdev;
    nix_host_off_t     st_size;
    time_t             st_atime;
    time_t             st_mtime;
    time_t             st_ctime;
    long               st_blksize;
    long               st_blocks;
};

int nix_platform_stat(const char *path, struct nix_platform_stat *buf);
int nix_platform_fstat(nix_host_fd_t fd, struct nix_platform_stat *buf);
int nix_platform_lstat(const char *path, struct nix_platform_stat *buf);

/* Platform-specific socket operations (for Win32 Winsock abstraction) */
#if defined(NIX_HOST_WIN32)
int nix_platform_socket_init(void);  /* Initialize Winsock (called by nix_platform_init) */
void nix_platform_socket_cleanup(void);  /* Cleanup Winsock */
int nix_platform_socket_to_fd(SOCKET sock);  /* Convert SOCKET to fd-like handle */
SOCKET nix_platform_fd_to_socket(int fd);  /* Convert fd-like handle to SOCKET */
#endif

/* Platform-specific I/O operations */
int nix_platform_pipe(int pipefd[2]);
int nix_platform_fcntl(nix_host_fd_t fd, int cmd, long arg);
int nix_platform_select(int nfds, fd_set *readfds, fd_set *writefds, fd_set *exceptfds, struct timeval *timeout);
int nix_platform_isatty(nix_host_fd_t fd);

/* Platform-specific process operations */

/* Process creation and termination */
nix_host_pid_t nix_platform_getpid(void);
nix_host_pid_t nix_platform_getppid(void);
nix_host_pid_t nix_platform_fork(void);
nix_host_pid_t nix_platform_vfork(void);
void nix_platform_exit(int status);
void nix_platform__exit(int status);

/* Process waiting */
nix_host_pid_t nix_platform_wait(int *status);
nix_host_pid_t nix_platform_waitpid(nix_host_pid_t pid, int *status, int options);
nix_host_pid_t nix_platform_wait3(int *status, int options, void *rusage);
nix_host_pid_t nix_platform_wait4(nix_host_pid_t pid, int *status, int options, void *rusage);

/* Process execution */
int nix_platform_execve(const char *path, char *const argv[], char *const envp[]);
int nix_platform_execv(const char *path, char *const argv[]);
int nix_platform_execvp(const char *file, char *const argv[]);
int nix_platform_execvpe(const char *file, char *const argv[], char *const envp[]);
int nix_platform_execl(const char *path, const char *arg, ...);
int nix_platform_execlp(const char *file, const char *arg, ...);
int nix_platform_execle(const char *path, const char *arg, ...);

/* Process and session groups */
nix_host_pid_t nix_platform_getpgid(nix_host_pid_t pid);
nix_host_pid_t nix_platform_getpgrp(void);
int nix_platform_setpgid(nix_host_pid_t pid, nix_host_pid_t pgid);
int nix_platform_setpgrp(void);
nix_host_pid_t nix_platform_getsid(nix_host_pid_t pid);
nix_host_pid_t nix_platform_setsid(void);

/* User and group IDs */
unsigned int nix_platform_getuid(void);
unsigned int nix_platform_geteuid(void);
unsigned int nix_platform_getgid(void);
unsigned int nix_platform_getegid(void);
int nix_platform_setuid(unsigned int uid);
int nix_platform_seteuid(unsigned int euid);
int nix_platform_setgid(unsigned int gid);
int nix_platform_setegid(unsigned int egid);
int nix_platform_setreuid(unsigned int ruid, unsigned int euid);
int nix_platform_setregid(unsigned int rgid, unsigned int egid);

/* Process priority */
int nix_platform_nice(int inc);
int nix_platform_getpriority(int which, int who);
int nix_platform_setpriority(int which, int who, int prio);

/* Resource usage */
int nix_platform_getrusage(int who, void *usage);

/* Signal sending (moved from below for grouping) */
int nix_platform_kill(nix_host_pid_t pid, int sig);

/* Session and terminal control */
nix_host_pid_t nix_platform_tcgetpgrp(int fd);
int nix_platform_tcsetpgrp(int fd, nix_host_pid_t pgrp);
char *nix_platform_ttyname(int fd);
int nix_platform_ttyname_r(int fd, char *buf, size_t buflen);
int nix_platform_isatty_ex(int fd);  /* Extended isatty */
char *nix_platform_ctermid(char *s);
int nix_platform_vhangup(void);
int nix_platform_revoke(const char *file);

/* System V IPC - Key generation */
key_t nix_platform_ftok(const char *pathname, int proj_id);

/* System V IPC - Message queues */
int nix_platform_msgget(key_t key, int msgflg);
int nix_platform_msgsnd(int msqid, const void *msgp, size_t msgsz, int msgflg);
ssize_t nix_platform_msgrcv(int msqid, void *msgp, size_t msgsz, long msgtyp, int msgflg);
int nix_platform_msgctl(int msqid, int cmd, struct msqid_ds *buf);

/* System V IPC - Semaphores */
int nix_platform_semget(key_t key, int nsems, int semflg);
int nix_platform_semop(int semid, struct sembuf *sops, size_t nsops);
int nix_platform_semctl(int semid, int semnum, int cmd, ...);

/* System V IPC - Shared memory */
int nix_platform_shmget(key_t key, size_t size, int shmflg);
void *nix_platform_shmat(int shmid, const void *shmaddr, int shmflg);
int nix_platform_shmdt(const void *shmaddr);
int nix_platform_shmctl(int shmid, int cmd, struct shmid_ds *buf);

/* Platform-specific hostname operations */
int nix_platform_gethostname(char *name, size_t len);
int nix_platform_sethostname(const char *name, size_t len);

/* Platform-specific time operations */
int nix_platform_gettimeofday(struct timeval *tv, void *tz);
int nix_platform_nanosleep(const struct timespec *req, struct timespec *rem);
unsigned int nix_platform_sleep(unsigned int seconds);

/* Platform-specific signal operations */
typedef void (*nix_signal_handler_t)(int);
#define NIX_SIG_DFL  ((nix_signal_handler_t)0)
#define NIX_SIG_IGN  ((nix_signal_handler_t)1)
#define NIX_SIG_ERR  ((nix_signal_handler_t)-1)

nix_signal_handler_t nix_platform_signal(int signum, nix_signal_handler_t handler);
int nix_platform_raise(int signum);
int nix_platform_kill_signal(nix_host_pid_t pid, int signum);

/* Platform-specific socket operations */
int nix_platform_socket(int domain, int type, int protocol);
int nix_platform_socketpair(int domain, int type, int protocol, int sv[2]);
int nix_platform_bind(int sockfd, const struct sockaddr *addr, socklen_t addrlen);
int nix_platform_listen(int sockfd, int backlog);
int nix_platform_accept(int sockfd, struct sockaddr *addr, socklen_t *addrlen);
int nix_platform_connect(int sockfd, const struct sockaddr *addr, socklen_t addrlen);
ssize_t nix_platform_send(int sockfd, const void *buf, size_t len, int flags);
ssize_t nix_platform_recv(int sockfd, void *buf, size_t len, int flags);
ssize_t nix_platform_sendto(int sockfd, const void *buf, size_t len, int flags,
                             const struct sockaddr *dest_addr, socklen_t addrlen);
ssize_t nix_platform_recvfrom(int sockfd, void *buf, size_t len, int flags,
                               struct sockaddr *src_addr, socklen_t *addrlen);
int nix_platform_shutdown_socket(int sockfd, int how);
int nix_platform_getsockname(int sockfd, struct sockaddr *addr, socklen_t *addrlen);
int nix_platform_getpeername(int sockfd, struct sockaddr *addr, socklen_t *addrlen);
int nix_platform_setsockopt(int sockfd, int level, int optname, const void *optval, socklen_t optlen);
int nix_platform_getsockopt(int sockfd, int level, int optname, void *optval, socklen_t *optlen);

/* Platform-specific event notification */
int nix_platform_poll(struct pollfd *fds, nfds_t nfds, int timeout);
int nix_platform_ppoll(struct pollfd *fds, nfds_t nfds, const struct timespec *timeout, const void *sigmask);
int nix_platform_pselect(int nfds, fd_set *readfds, fd_set *writefds, fd_set *exceptfds,
                          const struct timespec *timeout, const void *sigmask);

/* Platform-specific error handling */
int nix_platform_get_errno(void);
void nix_platform_set_errno(int error);

/* Platform utilities */
const char *nix_platform_get_name(void);
const char *nix_platform_get_arch(void);
int nix_platform_is_case_sensitive_fs(void);

#ifdef __cplusplus
}
#endif

/*
 * ========================================================================
 * PLATFORM CAPABILITY DETECTION
 * ========================================================================
 */

/* Define capabilities based on platform */
#if defined(NIX_HOST_WIN32)
# define NIX_HAS_POSIX_SIGNALS  0
# define NIX_HAS_FORK           0
# define NIX_HAS_MMAP           1  /* Via CreateFileMapping */
# define NIX_HAS_PTHREADS       0
# define NIX_HAS_SOCKETS        1  /* Winsock */
# define NIX_HAS_SELECT         1
# define NIX_HAS_POLL           0
# define NIX_HAS_EPOLL          0
# define NIX_HAS_KQUEUE         0

#elif defined(NIX_HOST_HAIKU)
# define NIX_HAS_POSIX_SIGNALS  1
# define NIX_HAS_FORK           1
# define NIX_HAS_MMAP           1
# define NIX_HAS_PTHREADS       1
# define NIX_HAS_SOCKETS        1
# define NIX_HAS_SELECT         1
# define NIX_HAS_POLL           1
# define NIX_HAS_EPOLL          0
# define NIX_HAS_KQUEUE         0

#elif defined(NIX_HOST_OS2)
# define NIX_HAS_POSIX_SIGNALS  0
# define NIX_HAS_FORK           1  /* Via DosExecPgm */
# define NIX_HAS_MMAP           1  /* Via DosAllocMem */
# define NIX_HAS_PTHREADS       1  /* OS/2 threads */
# define NIX_HAS_SOCKETS        1  /* TCP/IP stack */
# define NIX_HAS_SELECT         1
# define NIX_HAS_POLL           0
# define NIX_HAS_EPOLL          0
# define NIX_HAS_KQUEUE         0

#elif defined(NIX_HOST_OPENVMS)
# define NIX_HAS_POSIX_SIGNALS  1  /* Limited */
# define NIX_HAS_FORK           0
# define NIX_HAS_MMAP           0
# define NIX_HAS_PTHREADS       1
# define NIX_HAS_SOCKETS        1
# define NIX_HAS_SELECT         1
# define NIX_HAS_POLL           0
# define NIX_HAS_EPOLL          0
# define NIX_HAS_KQUEUE         0

#elif defined(NIX_HOST_BSD)
# define NIX_HAS_POSIX_SIGNALS  1
# define NIX_HAS_FORK           1
# define NIX_HAS_MMAP           1
# define NIX_HAS_PTHREADS       1
# define NIX_HAS_SOCKETS        1
# define NIX_HAS_SELECT         1
# define NIX_HAS_POLL           1
# define NIX_HAS_EPOLL          0
# define NIX_HAS_KQUEUE         1

#elif defined(NIX_HOST_LINUX)
# define NIX_HAS_POSIX_SIGNALS  1
# define NIX_HAS_FORK           1
# define NIX_HAS_MMAP           1
# define NIX_HAS_PTHREADS       1
# define NIX_HAS_SOCKETS        1
# define NIX_HAS_SELECT         1
# define NIX_HAS_POLL           1
# define NIX_HAS_EPOLL          1
# define NIX_HAS_KQUEUE         0

#else
/* Generic Unix */
# define NIX_HAS_POSIX_SIGNALS  1
# define NIX_HAS_FORK           1
# define NIX_HAS_MMAP           1
# define NIX_HAS_PTHREADS       1
# define NIX_HAS_SOCKETS        1
# define NIX_HAS_SELECT         1
# define NIX_HAS_POLL           1
# define NIX_HAS_EPOLL          0
# define NIX_HAS_KQUEUE         0

#endif

/*
 * ========================================================================
 * COMPILER DETECTION AND ATTRIBUTES
 * ========================================================================
 */

#if defined(__GNUC__) || defined(__clang__)
# define NIX_LIKELY(x)      __builtin_expect(!!(x), 1)
# define NIX_UNLIKELY(x)    __builtin_expect(!!(x), 0)
# define NIX_UNUSED         __attribute__((unused))
# define NIX_PACKED         __attribute__((packed))
# define NIX_ALIGNED(n)     __attribute__((aligned(n)))
# define NIX_PRINTF_LIKE(f,a) __attribute__((format(printf,f,a)))
#elif defined(_MSC_VER)
# define NIX_LIKELY(x)      (x)
# define NIX_UNLIKELY(x)    (x)
# define NIX_UNUSED
# define NIX_PACKED
# define NIX_ALIGNED(n)     __declspec(align(n))
# define NIX_PRINTF_LIKE(f,a)
#else
# define NIX_LIKELY(x)      (x)
# define NIX_UNLIKELY(x)    (x)
# define NIX_UNUSED
# define NIX_PACKED
# define NIX_ALIGNED(n)
# define NIX_PRINTF_LIKE(f,a)
#endif

/*
 * ========================================================================
 * DEBUGGING AND LOGGING
 * ========================================================================
 */

#ifdef NIX_DEBUG
# define NIX_DPRINTF(fmt, ...) \
    fprintf(stderr, "[NIX:%s:%d] " fmt "\n", __FILE__, __LINE__, ##__VA_ARGS__)
#else
# define NIX_DPRINTF(fmt, ...) ((void)0)
#endif

#define NIX_ERROR(fmt, ...) \
    fprintf(stderr, "[NIX ERROR:%s:%d] " fmt "\n", __FILE__, __LINE__, ##__VA_ARGS__)

#define NIX_WARNING(fmt, ...) \
    fprintf(stderr, "[NIX WARNING:%s:%d] " fmt "\n", __FILE__, __LINE__, ##__VA_ARGS__)

/*
 * ========================================================================
 * VERSION INFORMATION
 * ========================================================================
 */

#define NIX_VERSION_MAJOR 2
#define NIX_VERSION_MINOR 0
#define NIX_VERSION_PATCH 0
#define NIX_VERSION_STRING "2.0.0"

#endif /* __nix_platform_h */
