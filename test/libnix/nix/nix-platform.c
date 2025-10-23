/*
 * nix-platform.c
 *
 * Platform-specific implementations for libnix
 *
 * This file provides implementations of platform-specific functions
 * that abstract away differences between operating systems.
 */

#include "nix-platform.h"

/*
 * ========================================================================
 * PLATFORM INITIALIZATION
 * ========================================================================
 */

int
nix_platform_init(void)
{
#if defined(NIX_HOST_WIN32)
    /* Initialize Winsock */
    WSADATA wsaData;
    int result = WSAStartup(MAKEWORD(2, 2), &wsaData);
    if (result != 0) {
        NIX_ERROR("WSAStartup failed: %d", result);
        return -1;
    }
    NIX_DPRINTF("Win32 platform initialized");
    return 0;

#elif defined(NIX_HOST_HAIKU)
    /* Haiku initialization if needed */
    NIX_DPRINTF("Haiku platform initialized");
    return 0;

#elif defined(NIX_HOST_OS2)
    /* OS/2 initialization if needed */
    NIX_DPRINTF("OS/2 platform initialized");
    return 0;

#elif defined(NIX_HOST_OPENVMS)
    /* OpenVMS initialization if needed */
    NIX_DPRINTF("OpenVMS platform initialized");
    return 0;

#else
    /* Unix/POSIX initialization */
    NIX_DPRINTF("Unix/POSIX platform initialized");
    return 0;
#endif
}

void
nix_platform_shutdown(void)
{
#if defined(NIX_HOST_WIN32)
    /* Cleanup Winsock */
    WSACleanup();
    NIX_DPRINTF("Win32 platform shutdown");

#elif defined(NIX_HOST_HAIKU)
    NIX_DPRINTF("Haiku platform shutdown");

#elif defined(NIX_HOST_OS2)
    NIX_DPRINTF("OS/2 platform shutdown");

#elif defined(NIX_HOST_OPENVMS)
    NIX_DPRINTF("OpenVMS platform shutdown");

#else
    NIX_DPRINTF("Unix/POSIX platform shutdown");
#endif
}

/*
 * ========================================================================
 * PLATFORM ERROR HANDLING
 * ========================================================================
 */

int
nix_platform_get_errno(void)
{
#if defined(NIX_HOST_WIN32)
    /* Map Windows error codes to errno */
    DWORD error = GetLastError();

    /* Common mappings */
    switch (error) {
    case ERROR_SUCCESS:             return 0;
    case ERROR_FILE_NOT_FOUND:
    case ERROR_PATH_NOT_FOUND:      return ENOENT;
    case ERROR_TOO_MANY_OPEN_FILES: return EMFILE;
    case ERROR_ACCESS_DENIED:       return EACCES;
    case ERROR_INVALID_HANDLE:      return EBADF;
    case ERROR_NOT_ENOUGH_MEMORY:   return ENOMEM;
    case ERROR_INVALID_PARAMETER:   return EINVAL;
    case ERROR_FILE_EXISTS:         return EEXIST;
    case ERROR_DISK_FULL:           return ENOSPC;
    case ERROR_BROKEN_PIPE:         return EPIPE;
    case ERROR_DIR_NOT_EMPTY:       return ENOTEMPTY;
    default:                        return EIO;
    }

#elif defined(NIX_HOST_HAIKU)
    /* Haiku uses standard errno */
    return errno;

#elif defined(NIX_HOST_OS2)
    /* Map OS/2 error codes to errno */
    /* This is simplified; real implementation would be more comprehensive */
    return errno;

#elif defined(NIX_HOST_OPENVMS)
    /* Map VMS status codes to errno */
    /* This is simplified; real implementation would use sys$errno */
    return errno;

#else
    /* Standard POSIX errno */
    return errno;
#endif
}

void
nix_platform_set_errno(int error)
{
#if defined(NIX_HOST_WIN32)
    /* Map errno to Windows error codes (simplified) */
    switch (error) {
    case 0:         SetLastError(ERROR_SUCCESS); break;
    case ENOENT:    SetLastError(ERROR_FILE_NOT_FOUND); break;
    case EMFILE:    SetLastError(ERROR_TOO_MANY_OPEN_FILES); break;
    case EACCES:    SetLastError(ERROR_ACCESS_DENIED); break;
    case EBADF:     SetLastError(ERROR_INVALID_HANDLE); break;
    case ENOMEM:    SetLastError(ERROR_NOT_ENOUGH_MEMORY); break;
    case EINVAL:    SetLastError(ERROR_INVALID_PARAMETER); break;
    case EEXIST:    SetLastError(ERROR_FILE_EXISTS); break;
    case ENOSPC:    SetLastError(ERROR_DISK_FULL); break;
    case EPIPE:     SetLastError(ERROR_BROKEN_PIPE); break;
    case ENOTEMPTY: SetLastError(ERROR_DIR_NOT_EMPTY); break;
    default:        SetLastError(ERROR_INVALID_FUNCTION); break;
    }
    errno = error;

#else
    /* Standard errno setting */
    errno = error;
#endif
}

/*
 * ========================================================================
 * PLATFORM FILE OPERATIONS
 * ========================================================================
 */

nix_host_fd_t
nix_platform_open(const char *path, int flags, int mode)
{
#if defined(NIX_HOST_WIN32)
    /* Convert Unix flags to Windows flags */
    DWORD access = 0;
    DWORD creation = 0;
    DWORD share = FILE_SHARE_READ | FILE_SHARE_WRITE;

    /* Access mode */
    if ((flags & 0x03) == 0) {      /* O_RDONLY */
        access = GENERIC_READ;
    } else if ((flags & 0x03) == 1) { /* O_WRONLY */
        access = GENERIC_WRITE;
    } else if ((flags & 0x03) == 2) { /* O_RDWR */
        access = GENERIC_READ | GENERIC_WRITE;
    }

    /* Creation disposition */
    if (flags & 0x0200) {           /* O_CREAT */
        if (flags & 0x0400) {       /* O_EXCL */
            creation = CREATE_NEW;
        } else if (flags & 0x0800) { /* O_TRUNC */
            creation = CREATE_ALWAYS;
        } else {
            creation = OPEN_ALWAYS;
        }
    } else {
        if (flags & 0x0800) {       /* O_TRUNC */
            creation = TRUNCATE_EXISTING;
        } else {
            creation = OPEN_EXISTING;
        }
    }

    return CreateFileA(path, access, share, NULL, creation,
                       FILE_ATTRIBUTE_NORMAL, NULL);

#elif defined(NIX_HOST_HAIKU)
    return open(path, flags, mode);

#elif defined(NIX_HOST_OS2)
    HFILE hf = 0;
    ULONG action = 0;
    ULONG openFlags = 0;
    ULONG openMode = 0;

    /* Convert Unix flags to OS/2 flags */
    if (flags & 0x0200) { /* O_CREAT */
        openFlags = OPEN_ACTION_CREATE_IF_NEW | OPEN_ACTION_OPEN_IF_EXISTS;
    } else {
        openFlags = OPEN_ACTION_FAIL_IF_NEW | OPEN_ACTION_OPEN_IF_EXISTS;
    }

    if ((flags & 0x03) == 0) {      /* O_RDONLY */
        openMode = OPEN_ACCESS_READONLY | OPEN_SHARE_DENYNONE;
    } else if ((flags & 0x03) == 1) { /* O_WRONLY */
        openMode = OPEN_ACCESS_WRITEONLY | OPEN_SHARE_DENYNONE;
    } else {
        openMode = OPEN_ACCESS_READWRITE | OPEN_SHARE_DENYNONE;
    }

    if (DosOpen((PSZ)path, &hf, &action, 0, FILE_NORMAL,
                openFlags, openMode, NULL) != NO_ERROR) {
        return (HFILE)-1;
    }
    return hf;

#elif defined(NIX_HOST_OPENVMS)
    /* OpenVMS would use RMS (Record Management Services) */
    /* Simplified to use POSIX emulation for now */
    return open(path, flags, mode);

#else
    return open(path, flags, mode);
#endif
}

int
nix_platform_close(nix_host_fd_t fd)
{
#if defined(NIX_HOST_WIN32)
    return CloseHandle(fd) ? 0 : -1;

#elif defined(NIX_HOST_OS2)
    return (DosClose(fd) == NO_ERROR) ? 0 : -1;

#else
    return close(fd);
#endif
}

ssize_t
nix_platform_read(nix_host_fd_t fd, void *buf, size_t count)
{
#if defined(NIX_HOST_WIN32)
    DWORD bytesRead = 0;
    if (!ReadFile(fd, buf, (DWORD)count, &bytesRead, NULL)) {
        return -1;
    }
    return (ssize_t)bytesRead;

#elif defined(NIX_HOST_OS2)
    ULONG bytesRead = 0;
    if (DosRead(fd, buf, (ULONG)count, &bytesRead) != NO_ERROR) {
        return -1;
    }
    return (ssize_t)bytesRead;

#else
    return read(fd, buf, count);
#endif
}

ssize_t
nix_platform_write(nix_host_fd_t fd, const void *buf, size_t count)
{
#if defined(NIX_HOST_WIN32)
    DWORD bytesWritten = 0;
    if (!WriteFile(fd, buf, (DWORD)count, &bytesWritten, NULL)) {
        return -1;
    }
    return (ssize_t)bytesWritten;

#elif defined(NIX_HOST_OS2)
    ULONG bytesWritten = 0;
    if (DosWrite(fd, (PVOID)buf, (ULONG)count, &bytesWritten) != NO_ERROR) {
        return -1;
    }
    return (ssize_t)bytesWritten;

#else
    return write(fd, buf, count);
#endif
}

/*
 * ========================================================================
 * PLATFORM PROCESS OPERATIONS
 * ========================================================================
 */

nix_host_pid_t
nix_platform_getpid(void)
{
#if defined(NIX_HOST_WIN32)
    return GetCurrentProcessId();

#elif defined(NIX_HOST_HAIKU)
    return find_thread(NULL);  /* Returns thread ID, can use getpid() too */

#elif defined(NIX_HOST_OS2)
    PTIB ptib;
    PPIB ppib;
    DosGetInfoBlocks(&ptib, &ppib);
    return ppib->pib_ulpid;

#elif defined(NIX_HOST_OPENVMS)
    /* OpenVMS would use SYS$GETJPI */
    return getpid();

#else
    return getpid();
#endif
}

nix_host_pid_t
nix_platform_fork(void)
{
#if defined(NIX_HOST_WIN32)
    /* Windows doesn't have fork(), would need to use CreateProcess */
    NIX_ERROR("fork() not supported on Win32");
    errno = ENOSYS;
    return -1;

#elif defined(NIX_HOST_HAIKU)
    return fork();

#elif defined(NIX_HOST_OS2)
    /* OS/2 uses DosExecPgm instead of fork */
    NIX_ERROR("fork() not directly supported on OS/2, use DosExecPgm");
    errno = ENOSYS;
    return -1;

#elif defined(NIX_HOST_OPENVMS)
    /* OpenVMS doesn't have fork(), would use LIB$SPAWN */
    NIX_ERROR("fork() not supported on OpenVMS");
    errno = ENOSYS;
    return -1;

#else
    return fork();
#endif
}

/*
 * ========================================================================
 * PLATFORM UTILITIES
 * ========================================================================
 */

const char *
nix_platform_get_name(void)
{
#if defined(NIX_HOST_WIN32)
# ifdef NIX_HOST_WIN64
    return "Windows 64-bit";
# else
    return "Windows 32-bit";
# endif
#elif defined(NIX_HOST_HAIKU)
    return "Haiku";
#elif defined(NIX_HOST_OS2)
    return "OS/2";
#elif defined(NIX_HOST_OPENVMS)
    return "OpenVMS";
#elif defined(NIX_HOST_LINUX)
    return "Linux";
#elif defined(NIX_HOST_FREEBSD)
    return "FreeBSD";
#elif defined(NIX_HOST_OPENBSD)
    return "OpenBSD";
#elif defined(NIX_HOST_NETBSD)
    return "NetBSD";
#elif defined(NIX_HOST_DARWIN)
    return "macOS/Darwin";
#elif defined(NIX_HOST_SOLARIS)
    return "Solaris";
#else
    return "Generic Unix";
#endif
}

const char *
nix_platform_get_arch(void)
{
    return NIX_HOST_ARCH;
}

int
nix_platform_is_case_sensitive_fs(void)
{
#if defined(NIX_HOST_WIN32) || defined(NIX_HOST_OS2) || defined(NIX_HOST_OPENVMS)
    return 0;  /* Case-insensitive */
#else
    return 1;  /* Case-sensitive */
#endif
}
