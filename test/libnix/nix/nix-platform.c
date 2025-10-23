/*
 * nix-platform.c
 *
 * Platform-specific implementations for libnix
 *
 * This file provides comprehensive platform abstraction for libnix,
 * enabling it to run on Win32, Haiku, OS/2, OpenVMS, and Unix/POSIX systems.
 */

#include "nix-platform.h"
#include <errno.h>
#include <stdarg.h>

#if defined(NIX_HOST_WIN32)
# include <sys/types.h>
# include <sys/stat.h>
#endif

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
    NIX_DPRINTF("Win32 platform initialized (Winsock 2.2)");
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
    case ERROR_FILE_EXISTS:
    case ERROR_ALREADY_EXISTS:      return EEXIST;
    case ERROR_DISK_FULL:           return ENOSPC;
    case ERROR_BROKEN_PIPE:         return EPIPE;
    case ERROR_DIR_NOT_EMPTY:       return ENOTEMPTY;
    case ERROR_BAD_PATHNAME:        return ENOENT;
    case ERROR_INVALID_NAME:        return EINVAL;
    case ERROR_NOT_READY:           return EBUSY;
    case ERROR_WRITE_PROTECT:       return EROFS;
    case ERROR_SHARING_VIOLATION:   return EACCES;
    case ERROR_LOCK_VIOLATION:      return EACCES;
    default:                        return EIO;
    }

#elif defined(NIX_HOST_HAIKU)
    /* Haiku uses standard errno */
    return errno;

#elif defined(NIX_HOST_OS2)
    /* Map OS/2 error codes to errno */
    return errno;

#elif defined(NIX_HOST_OPENVMS)
    /* Map VMS status codes to errno */
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
    /* Map errno to Windows error codes */
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
    case EROFS:     SetLastError(ERROR_WRITE_PROTECT); break;
    case EBUSY:     SetLastError(ERROR_NOT_READY); break;
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
 * PLATFORM VERSION DETECTION (Win32)
 * ========================================================================
 */

#if defined(NIX_HOST_WIN32)

/*
 * Windows version information cache
 * Populated on first call to avoid repeated system calls
 */
static int win32_version_major = -1;
static int win32_version_minor = -1;

/*
 * Initialize version information using the most appropriate API
 * - RtlGetVersion: Windows Vista+ (not subject to manifest compatibility shims)
 * - GetVersionEx: Windows NT 3.1+ (deprecated on Windows 10+, but works)
 */
static void
win32_init_version(void)
{
    if (win32_version_major >= 0) {
        return;  /* Already initialized */
    }

    /*
     * Try RtlGetVersion first (Vista+)
     * This is the recommended method on modern Windows as it bypasses
     * compatibility shims that can cause GetVersionEx to lie about the version.
     */
    typedef LONG (WINAPI *RtlGetVersion_t)(OSVERSIONINFOEXW *);
    HMODULE ntdll = GetModuleHandleA("ntdll.dll");

    if (ntdll) {
        RtlGetVersion_t pRtlGetVersion =
            (RtlGetVersion_t)GetProcAddress(ntdll, "RtlGetVersion");

        if (pRtlGetVersion) {
            OSVERSIONINFOEXW osvi;
            ZeroMemory(&osvi, sizeof(OSVERSIONINFOEXW));
            osvi.dwOSVersionInfoSize = sizeof(OSVERSIONINFOEXW);

            if (pRtlGetVersion(&osvi) == 0) {  /* STATUS_SUCCESS */
                win32_version_major = (int)osvi.dwMajorVersion;
                win32_version_minor = (int)osvi.dwMinorVersion;
                NIX_DPRINTF("Win32 version detected via RtlGetVersion: %d.%d",
                           win32_version_major, win32_version_minor);
                return;
            }
        }
    }

    /*
     * Fall back to GetVersionEx (NT 3.1+)
     * This works on all Windows NT versions but may return incorrect
     * results on Windows 10+ if the application doesn't have a manifest
     * declaring compatibility.
     */
    OSVERSIONINFOA osvi;
    ZeroMemory(&osvi, sizeof(OSVERSIONINFOA));
    osvi.dwOSVersionInfoSize = sizeof(OSVERSIONINFOA);

#pragma warning(push)
#pragma warning(disable: 4996)  /* 'GetVersionExA': was declared deprecated */
    if (GetVersionExA(&osvi)) {
        win32_version_major = (int)osvi.dwMajorVersion;
        win32_version_minor = (int)osvi.dwMinorVersion;
        NIX_DPRINTF("Win32 version detected via GetVersionEx: %d.%d",
                   win32_version_major, win32_version_minor);
        return;
    }
#pragma warning(pop)

    /*
     * If both methods fail (shouldn't happen), assume NT 3.1 as a safe default
     */
    win32_version_major = 3;
    win32_version_minor = 1;
    NIX_DPRINTF("Win32 version detection failed, assuming NT 3.1");
}

int
nix_platform_win32_version_major(void)
{
    win32_init_version();
    return win32_version_major;
}

int
nix_platform_win32_version_minor(void)
{
    win32_init_version();
    return win32_version_minor;
}

int
nix_platform_win32_has_api(const char *api_name)
{
    /*
     * Check if a Windows API function is available at runtime.
     * This enables graceful degradation on older Windows versions.
     *
     * Expected format: "module.dll:FunctionName" or just "FunctionName"
     * Examples:
     *   - "kernel32.dll:CreateHardLinkA"
     *   - "ws2_32.dll:WSAPoll"
     *   - "ntdll.dll:RtlGetVersion"
     */

    if (!api_name || !*api_name) {
        return 0;
    }

    /* Parse module and function name */
    char module_name[256];
    const char *function_name;
    const char *colon = strchr(api_name, ':');

    if (colon) {
        /* Explicit module specified */
        size_t module_len = colon - api_name;
        if (module_len >= sizeof(module_name)) {
            return 0;  /* Module name too long */
        }
        memcpy(module_name, api_name, module_len);
        module_name[module_len] = '\0';
        function_name = colon + 1;
    } else {
        /* No module specified, try common system DLLs */
        function_name = api_name;

        /* Try kernel32.dll first (most common) */
        HMODULE hModule = GetModuleHandleA("kernel32.dll");
        if (hModule && GetProcAddress(hModule, function_name)) {
            return 1;
        }

        /* Try ntdll.dll */
        hModule = GetModuleHandleA("ntdll.dll");
        if (hModule && GetProcAddress(hModule, function_name)) {
            return 1;
        }

        /* Try ws2_32.dll (Winsock) */
        hModule = GetModuleHandleA("ws2_32.dll");
        if (hModule && GetProcAddress(hModule, function_name)) {
            return 1;
        }

        return 0;
    }

    /* Load the specified module and check for function */
    HMODULE hModule = GetModuleHandleA(module_name);
    if (!hModule) {
        /* Module not loaded, try to load it */
        hModule = LoadLibraryA(module_name);
        if (!hModule) {
            return 0;
        }
        /* Note: We intentionally leak the handle here as we don't want to
         * unload system DLLs that might be in use */
    }

    return GetProcAddress(hModule, function_name) != NULL;
}

#endif  /* NIX_HOST_WIN32 */

/*
 * ========================================================================
 * PLATFORM SIGNAL OPERATIONS
 * ========================================================================
 */

nix_signal_handler_t
nix_platform_signal(int signum, nix_signal_handler_t handler)
{
#if defined(NIX_HOST_WIN32)
    /*
     * Windows supports a limited subset of POSIX signals through signal().
     * Supported: SIGINT, SIGTERM, SIGABRT, SIGBREAK (Windows-specific)
     * Not supported: SIGHUP, SIGQUIT, SIGKILL, SIGUSR1/2, etc.
     */

    /* Map Linux/POSIX signal numbers to Windows signal numbers */
    int win_signum;
    switch (signum) {
    case 2:   /* SIGINT */
        win_signum = SIGINT;
        break;
    case 15:  /* SIGTERM */
        win_signum = SIGTERM;
        break;
    case 6:   /* SIGABRT */
        win_signum = SIGABRT;
        break;
    case 21:  /* SIGBREAK (Windows-specific, Linux doesn't have this) */
        win_signum = 21;  /* SIGBREAK */
        break;

    /* Unsupported signals - return error */
    case 1:   /* SIGHUP */
    case 3:   /* SIGQUIT */
    case 9:   /* SIGKILL */
    case 10:  /* SIGUSR1 */
    case 12:  /* SIGUSR2 */
    default:
        NIX_DPRINTF("Signal %d not supported on Win32", signum);
        nix_platform_set_errno(EINVAL);
        return NIX_SIG_ERR;
    }

    /* Install the signal handler */
    void (*result)(int) = signal(win_signum, (void (*)(int))handler);
    if (result == SIG_ERR) {
        nix_platform_set_errno(EINVAL);
        return NIX_SIG_ERR;
    }

    return (nix_signal_handler_t)result;

#elif defined(NIX_HOST_HAIKU)
    /* Haiku has standard POSIX signal support */
    void (*result)(int) = signal(signum, (void (*)(int))handler);
    if (result == SIG_ERR) {
        return NIX_SIG_ERR;
    }
    return (nix_signal_handler_t)result;

#elif defined(NIX_HOST_OS2)
    /* OS/2 has limited signal support, similar to Windows */
    void (*result)(int) = signal(signum, (void (*)(int))handler);
    if (result == SIG_ERR) {
        return NIX_SIG_ERR;
    }
    return (nix_signal_handler_t)result;

#elif defined(NIX_HOST_OPENVMS)
    /* OpenVMS has signal support through C RTL */
    void (*result)(int) = signal(signum, (void (*)(int))handler);
    if (result == SIG_ERR) {
        return NIX_SIG_ERR;
    }
    return (nix_signal_handler_t)result;

#else
    /* Standard POSIX signal() */
    void (*result)(int) = signal(signum, (void (*)(int))handler);
    if (result == SIG_ERR) {
        return NIX_SIG_ERR;
    }
    return (nix_signal_handler_t)result;
#endif
}

int
nix_platform_raise(int signum)
{
#if defined(NIX_HOST_WIN32)
    /*
     * Windows raise() supports SIGINT, SIGTERM, SIGABRT, SIGBREAK
     */

    /* Map Linux/POSIX signal numbers to Windows signal numbers */
    int win_signum;
    switch (signum) {
    case 2:   /* SIGINT */
        win_signum = SIGINT;
        break;
    case 15:  /* SIGTERM */
        win_signum = SIGTERM;
        break;
    case 6:   /* SIGABRT */
        win_signum = SIGABRT;
        break;
    case 21:  /* SIGBREAK */
        win_signum = 21;
        break;

    /* Unsupported signals */
    default:
        NIX_DPRINTF("raise: Signal %d not supported on Win32", signum);
        nix_platform_set_errno(EINVAL);
        return -1;
    }

    return raise(win_signum);

#else
    /* Standard POSIX raise() */
    return raise(signum);
#endif
}

int
nix_platform_kill_signal(nix_host_pid_t pid, int signum)
{
#if defined(NIX_HOST_WIN32)
    /*
     * Windows doesn't have a real kill() syscall. We can:
     * 1. Use GenerateConsoleCtrlEvent() for SIGINT/SIGBREAK (same console)
     * 2. Use TerminateProcess() for SIGKILL/SIGTERM (forceful termination)
     * 3. Return ENOSYS for other signals
     */

    HANDLE hProcess;
    BOOL result;

    /* Special case: pid 0 means current process */
    if (pid == 0) {
        pid = GetCurrentProcessId();
    }

    switch (signum) {
    case 2:  /* SIGINT - Generate Ctrl+C event */
        /*
         * GenerateConsoleCtrlEvent() only works for processes in the same
         * console process group. Passing 0 for dwProcessGroupId sends to
         * all processes sharing the console.
         */
        result = GenerateConsoleCtrlEvent(CTRL_C_EVENT, (DWORD)pid);
        if (!result) {
            /* If we can't send console event, fail gracefully */
            nix_platform_set_errno(EPERM);
            return -1;
        }
        return 0;

    case 21:  /* SIGBREAK - Generate Ctrl+Break event */
        result = GenerateConsoleCtrlEvent(CTRL_BREAK_EVENT, (DWORD)pid);
        if (!result) {
            nix_platform_set_errno(EPERM);
            return -1;
        }
        return 0;

    case 9:   /* SIGKILL - Forceful termination */
    case 15:  /* SIGTERM - Termination (Windows has no graceful request) */
        /*
         * Open the target process and terminate it.
         * Note: This is forceful and doesn't allow cleanup.
         */
        hProcess = OpenProcess(PROCESS_TERMINATE, FALSE, (DWORD)pid);
        if (!hProcess) {
            /* Process doesn't exist or no permission */
            DWORD error = GetLastError();
            if (error == ERROR_INVALID_PARAMETER) {
                nix_platform_set_errno(ESRCH);  /* No such process */
            } else {
                nix_platform_set_errno(EPERM);  /* Permission denied */
            }
            return -1;
        }

        result = TerminateProcess(hProcess, 1);
        CloseHandle(hProcess);

        if (!result) {
            nix_platform_set_errno(EPERM);
            return -1;
        }
        return 0;

    case 0:  /* Signal 0 - Check if process exists */
        /*
         * Signal 0 is used to check if a process exists without sending
         * a signal. Try to open the process with minimal permissions.
         */
        hProcess = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, (DWORD)pid);
        if (!hProcess) {
            nix_platform_set_errno(ESRCH);
            return -1;
        }
        CloseHandle(hProcess);
        return 0;

    /* Unsupported signals */
    default:
        NIX_DPRINTF("kill: Signal %d not supported on Win32", signum);
        nix_platform_set_errno(EINVAL);
        return -1;
    }

#elif defined(NIX_HOST_HAIKU)
    /* Haiku has standard POSIX kill() */
    return kill((pid_t)pid, signum);

#elif defined(NIX_HOST_OS2)
    /* OS/2 has limited kill() support through DosSendSignalException */
    /* For simplicity, we'll use the C runtime kill() if available */
    return kill((pid_t)pid, signum);

#elif defined(NIX_HOST_OPENVMS)
    /* OpenVMS has signal support through SYS$SIGPRC */
    return kill((pid_t)pid, signum);

#else
    /* Standard POSIX kill() */
    return kill((pid_t)pid, signum);
#endif
}

/*
 * ========================================================================
 * PLATFORM SOCKET OPERATIONS
 * ========================================================================
 */

int
nix_platform_socket(int domain, int type, int protocol)
{
#if defined(NIX_HOST_WIN32)
    /*
     * Winsock socket() - Direct wrapper for AF_INET/AF_INET6
     * AF_LOCAL/AF_UNIX will be handled separately via named pipes
     */
    SOCKET sock = socket(domain, type, protocol);
    if (sock == INVALID_SOCKET) {
        /* Map Winsock error to errno */
        int error = WSAGetLastError();
        switch (error) {
        case WSAEAFNOSUPPORT:
            nix_platform_set_errno(EAFNOSUPPORT);
            break;
        case WSAEPROTONOSUPPORT:
            nix_platform_set_errno(EPROTONOSUPPORT);
            break;
        case WSAEMFILE:
            nix_platform_set_errno(EMFILE);
            break;
        default:
            nix_platform_set_errno(EINVAL);
        }
        return -1;
    }

    /*
     * On Windows, SOCKETs are handles, not file descriptors.
     * We return them as int for compatibility, but they need special handling.
     * TODO: Integrate with nix_fd table for proper fd management
     */
    return (int)sock;

#else
    /* Standard POSIX socket() */
    return socket(domain, type, protocol);
#endif
}

int
nix_platform_bind(int sockfd, const struct sockaddr *addr, socklen_t addrlen)
{
#if defined(NIX_HOST_WIN32)
    if (bind((SOCKET)sockfd, addr, (int)addrlen) == SOCKET_ERROR) {
        int error = WSAGetLastError();
        switch (error) {
        case WSAEADDRINUSE:
            nix_platform_set_errno(EADDRINUSE);
            break;
        case WSAEADDRNOTAVAIL:
            nix_platform_set_errno(EADDRNOTAVAIL);
            break;
        case WSAEACCES:
            nix_platform_set_errno(EACCES);
            break;
        default:
            nix_platform_set_errno(EINVAL);
        }
        return -1;
    }
    return 0;

#else
    /* Standard POSIX bind() */
    return bind(sockfd, addr, addrlen);
#endif
}

int
nix_platform_listen(int sockfd, int backlog)
{
#if defined(NIX_HOST_WIN32)
    if (listen((SOCKET)sockfd, backlog) == SOCKET_ERROR) {
        nix_platform_set_errno(EINVAL);
        return -1;
    }
    return 0;

#else
    /* Standard POSIX listen() */
    return listen(sockfd, backlog);
#endif
}

int
nix_platform_accept(int sockfd, struct sockaddr *addr, socklen_t *addrlen)
{
#if defined(NIX_HOST_WIN32)
    int len = addrlen ? (int)*addrlen : 0;
    SOCKET client = accept((SOCKET)sockfd, addr, addrlen ? &len : NULL);

    if (client == INVALID_SOCKET) {
        int error = WSAGetLastError();
        switch (error) {
        case WSAEWOULDBLOCK:
            nix_platform_set_errno(EWOULDBLOCK);
            break;
        case WSAECONNRESET:
            nix_platform_set_errno(ECONNRESET);
            break;
        default:
            nix_platform_set_errno(EINVAL);
        }
        return -1;
    }

    if (addrlen) {
        *addrlen = (socklen_t)len;
    }

    return (int)client;

#else
    /* Standard POSIX accept() */
    return accept(sockfd, addr, addrlen);
#endif
}

int
nix_platform_connect(int sockfd, const struct sockaddr *addr, socklen_t addrlen)
{
#if defined(NIX_HOST_WIN32)
    if (connect((SOCKET)sockfd, addr, (int)addrlen) == SOCKET_ERROR) {
        int error = WSAGetLastError();
        switch (error) {
        case WSAECONNREFUSED:
            nix_platform_set_errno(ECONNREFUSED);
            break;
        case WSAENETUNREACH:
            nix_platform_set_errno(ENETUNREACH);
            break;
        case WSAEHOSTUNREACH:
            nix_platform_set_errno(EHOSTUNREACH);
            break;
        case WSAETIMEDOUT:
            nix_platform_set_errno(ETIMEDOUT);
            break;
        case WSAEWOULDBLOCK:
        case WSAEINPROGRESS:
            nix_platform_set_errno(EINPROGRESS);
            break;
        case WSAEISCONN:
            nix_platform_set_errno(EISCONN);
            break;
        default:
            nix_platform_set_errno(EINVAL);
        }
        return -1;
    }
    return 0;

#else
    /* Standard POSIX connect() */
    return connect(sockfd, addr, addrlen);
#endif
}

ssize_t
nix_platform_send(int sockfd, const void *buf, size_t len, int flags)
{
#if defined(NIX_HOST_WIN32)
    int result = send((SOCKET)sockfd, (const char *)buf, (int)len, flags);
    if (result == SOCKET_ERROR) {
        int error = WSAGetLastError();
        switch (error) {
        case WSAEWOULDBLOCK:
            nix_platform_set_errno(EWOULDBLOCK);
            break;
        case WSAECONNRESET:
            nix_platform_set_errno(ECONNRESET);
            break;
        case WSAENOTCONN:
            nix_platform_set_errno(ENOTCONN);
            break;
        default:
            nix_platform_set_errno(EIO);
        }
        return -1;
    }
    return (ssize_t)result;

#else
    /* Standard POSIX send() */
    return send(sockfd, buf, len, flags);
#endif
}

ssize_t
nix_platform_recv(int sockfd, void *buf, size_t len, int flags)
{
#if defined(NIX_HOST_WIN32)
    int result = recv((SOCKET)sockfd, (char *)buf, (int)len, flags);
    if (result == SOCKET_ERROR) {
        int error = WSAGetLastError();
        switch (error) {
        case WSAEWOULDBLOCK:
            nix_platform_set_errno(EWOULDBLOCK);
            break;
        case WSAECONNRESET:
            nix_platform_set_errno(ECONNRESET);
            break;
        case WSAENOTCONN:
            nix_platform_set_errno(ENOTCONN);
            break;
        default:
            nix_platform_set_errno(EIO);
        }
        return -1;
    }
    return (ssize_t)result;

#else
    /* Standard POSIX recv() */
    return recv(sockfd, buf, len, flags);
#endif
}

ssize_t
nix_platform_sendto(int sockfd, const void *buf, size_t len, int flags,
                    const struct sockaddr *dest_addr, socklen_t addrlen)
{
#if defined(NIX_HOST_WIN32)
    int result = sendto((SOCKET)sockfd, (const char *)buf, (int)len, flags,
                        dest_addr, (int)addrlen);
    if (result == SOCKET_ERROR) {
        int error = WSAGetLastError();
        switch (error) {
        case WSAEWOULDBLOCK:
            nix_platform_set_errno(EWOULDBLOCK);
            break;
        case WSAENETUNREACH:
            nix_platform_set_errno(ENETUNREACH);
            break;
        case WSAEHOSTUNREACH:
            nix_platform_set_errno(EHOSTUNREACH);
            break;
        default:
            nix_platform_set_errno(EIO);
        }
        return -1;
    }
    return (ssize_t)result;

#else
    /* Standard POSIX sendto() */
    return sendto(sockfd, buf, len, flags, dest_addr, addrlen);
#endif
}

ssize_t
nix_platform_recvfrom(int sockfd, void *buf, size_t len, int flags,
                      struct sockaddr *src_addr, socklen_t *addrlen)
{
#if defined(NIX_HOST_WIN32)
    int fromlen = addrlen ? (int)*addrlen : 0;
    int result = recvfrom((SOCKET)sockfd, (char *)buf, (int)len, flags,
                          src_addr, addrlen ? &fromlen : NULL);

    if (result == SOCKET_ERROR) {
        int error = WSAGetLastError();
        switch (error) {
        case WSAEWOULDBLOCK:
            nix_platform_set_errno(EWOULDBLOCK);
            break;
        case WSAECONNRESET:
            nix_platform_set_errno(ECONNRESET);
            break;
        default:
            nix_platform_set_errno(EIO);
        }
        return -1;
    }

    if (addrlen) {
        *addrlen = (socklen_t)fromlen;
    }

    return (ssize_t)result;

#else
    /* Standard POSIX recvfrom() */
    return recvfrom(sockfd, buf, len, flags, src_addr, addrlen);
#endif
}

int
nix_platform_shutdown_socket(int sockfd, int how)
{
#if defined(NIX_HOST_WIN32)
    /* Map POSIX shutdown constants to Winsock constants */
    int win_how;
    switch (how) {
    case 0:  /* SHUT_RD */
        win_how = SD_RECEIVE;
        break;
    case 1:  /* SHUT_WR */
        win_how = SD_SEND;
        break;
    case 2:  /* SHUT_RDWR */
        win_how = SD_BOTH;
        break;
    default:
        nix_platform_set_errno(EINVAL);
        return -1;
    }

    if (shutdown((SOCKET)sockfd, win_how) == SOCKET_ERROR) {
        int error = WSAGetLastError();
        switch (error) {
        case WSAENOTCONN:
            nix_platform_set_errno(ENOTCONN);
            break;
        default:
            nix_platform_set_errno(EINVAL);
        }
        return -1;
    }
    return 0;

#else
    /* Standard POSIX shutdown() */
    return shutdown(sockfd, how);
#endif
}

int
nix_platform_getsockname(int sockfd, struct sockaddr *addr, socklen_t *addrlen)
{
#if defined(NIX_HOST_WIN32)
    int len = addrlen ? (int)*addrlen : 0;
    if (getsockname((SOCKET)sockfd, addr, &len) == SOCKET_ERROR) {
        nix_platform_set_errno(EINVAL);
        return -1;
    }
    if (addrlen) {
        *addrlen = (socklen_t)len;
    }
    return 0;

#else
    /* Standard POSIX getsockname() */
    return getsockname(sockfd, addr, addrlen);
#endif
}

int
nix_platform_getpeername(int sockfd, struct sockaddr *addr, socklen_t *addrlen)
{
#if defined(NIX_HOST_WIN32)
    int len = addrlen ? (int)*addrlen : 0;
    if (getpeername((SOCKET)sockfd, addr, &len) == SOCKET_ERROR) {
        int error = WSAGetLastError();
        switch (error) {
        case WSAENOTCONN:
            nix_platform_set_errno(ENOTCONN);
            break;
        default:
            nix_platform_set_errno(EINVAL);
        }
        return -1;
    }
    if (addrlen) {
        *addrlen = (socklen_t)len;
    }
    return 0;

#else
    /* Standard POSIX getpeername() */
    return getpeername(sockfd, addr, addrlen);
#endif
}

int
nix_platform_setsockopt(int sockfd, int level, int optname,
                        const void *optval, socklen_t optlen)
{
#if defined(NIX_HOST_WIN32)
    if (setsockopt((SOCKET)sockfd, level, optname,
                   (const char *)optval, (int)optlen) == SOCKET_ERROR) {
        nix_platform_set_errno(EINVAL);
        return -1;
    }
    return 0;

#else
    /* Standard POSIX setsockopt() */
    return setsockopt(sockfd, level, optname, optval, optlen);
#endif
}

int
nix_platform_getsockopt(int sockfd, int level, int optname,
                        void *optval, socklen_t *optlen)
{
#if defined(NIX_HOST_WIN32)
    int len = optlen ? (int)*optlen : 0;
    if (getsockopt((SOCKET)sockfd, level, optname,
                   (char *)optval, &len) == SOCKET_ERROR) {
        nix_platform_set_errno(EINVAL);
        return -1;
    }
    if (optlen) {
        *optlen = (socklen_t)len;
    }
    return 0;

#else
    /* Standard POSIX getsockopt() */
    return getsockopt(sockfd, level, optname, optval, optlen);
#endif
}

int
nix_platform_socketpair(int domain, int type, int protocol, int sv[2])
{
#if defined(NIX_HOST_WIN32)
    /*
     * Windows doesn't have socketpair().
     * We'll implement it later using:
     * - Named pipes for AF_UNIX/AF_LOCAL
     * - Loopback TCP connection for AF_INET (less efficient)
     *
     * For now, return ENOSYS (not implemented).
     */
    (void)domain;
    (void)type;
    (void)protocol;
    (void)sv;

    NIX_DPRINTF("socketpair: Not yet implemented on Win32");
    nix_platform_set_errno(ENOSYS);
    return -1;

#else
    /* Standard POSIX socketpair() */
    return socketpair(domain, type, protocol, sv);
#endif
}

/*
 * ========================================================================
 * PLATFORM EVENT NOTIFICATION (poll/select)
 * ========================================================================
 */

int
nix_platform_poll(struct pollfd *fds, nfds_t nfds, int timeout)
{
#if defined(NIX_HOST_WIN32)
    /*
     * Windows poll() implementation with version detection:
     * - Vista+: Use WSAPoll() (most compatible)
     * - NT 3.1-XP: Emulate via select()
     */

    /* Check if WSAPoll is available (Vista+) */
    if (nix_platform_win32_has_api("ws2_32.dll:WSAPoll")) {
        /* Use WSAPoll directly - same signature as POSIX poll() */
        typedef int (WSAAPI *WSAPoll_t)(struct pollfd *, ULONG, INT);
        HMODULE ws2_32 = GetModuleHandleA("ws2_32.dll");
        if (ws2_32) {
            WSAPoll_t pWSAPoll = (WSAPoll_t)GetProcAddress(ws2_32, "WSAPoll");
            if (pWSAPoll) {
                return pWSAPoll(fds, (ULONG)nfds, timeout);
            }
        }
    }

    /*
     * Fall back to select() emulation for NT 3.1-XP
     * Convert poll() semantics to select() semantics
     */

    if (nfds == 0) {
        /* Just sleep for timeout milliseconds */
        if (timeout > 0) {
            Sleep((DWORD)timeout);
            return 0;
        } else if (timeout == 0) {
            return 0;
        } else {
            /* Infinite timeout with no fds - not useful, but valid */
            Sleep(INFINITE);
            return 0;
        }
    }

    fd_set readfds, writefds, exceptfds;
    FD_ZERO(&readfds);
    FD_ZERO(&writefds);
    FD_ZERO(&exceptfds);

    int max_fd = -1;
    nfds_t i;

    /* Build fd_sets from pollfd array */
    for (i = 0; i < nfds; i++) {
        if (fds[i].fd < 0) {
            continue;  /* Negative fd means ignore this entry */
        }

        if (fds[i].events & POLLIN) {
            FD_SET((SOCKET)fds[i].fd, &readfds);
        }
        if (fds[i].events & POLLOUT) {
            FD_SET((SOCKET)fds[i].fd, &writefds);
        }
        /* Always check for exceptions */
        FD_SET((SOCKET)fds[i].fd, &exceptfds);

        if (fds[i].fd > max_fd) {
            max_fd = fds[i].fd;
        }
    }

    /* Convert timeout from milliseconds to struct timeval */
    struct timeval tv;
    struct timeval *ptv = NULL;
    if (timeout >= 0) {
        tv.tv_sec = timeout / 1000;
        tv.tv_usec = (timeout % 1000) * 1000;
        ptv = &tv;
    }
    /* timeout < 0 means infinite - ptv remains NULL */

    /* Call select() */
    int result = select(max_fd + 1, &readfds, &writefds, &exceptfds, ptv);
    if (result == SOCKET_ERROR) {
        nix_platform_set_errno(EINVAL);
        return -1;
    }

    /* Convert fd_set results back to pollfd revents */
    int count = 0;
    for (i = 0; i < nfds; i++) {
        fds[i].revents = 0;

        if (fds[i].fd < 0) {
            continue;
        }

        if (FD_ISSET((SOCKET)fds[i].fd, &readfds)) {
            fds[i].revents |= POLLIN;
        }
        if (FD_ISSET((SOCKET)fds[i].fd, &writefds)) {
            fds[i].revents |= POLLOUT;
        }
        if (FD_ISSET((SOCKET)fds[i].fd, &exceptfds)) {
            fds[i].revents |= POLLERR;
        }

        if (fds[i].revents != 0) {
            count++;
        }
    }

    return count;

#else
    /* Standard POSIX poll() */
    return poll(fds, nfds, timeout);
#endif
}

int
nix_platform_ppoll(struct pollfd *fds, nfds_t nfds,
                   const struct timespec *timeout, const void *sigmask)
{
#if defined(NIX_HOST_WIN32)
    /*
     * Windows doesn't have signal masks, so we ignore the sigmask parameter.
     * Convert timeout from timespec to milliseconds and call poll().
     */
    (void)sigmask;  /* Unused on Windows */

    int timeout_ms;
    if (!timeout) {
        timeout_ms = -1;  /* Infinite timeout */
    } else {
        /* Convert timespec to milliseconds */
        timeout_ms = (int)(timeout->tv_sec * 1000 + timeout->tv_nsec / 1000000);
    }

    return nix_platform_poll(fds, nfds, timeout_ms);

#elif defined(HAVE_PPOLL)
    /* Use native ppoll() if available */
    return ppoll(fds, nfds, timeout, (const sigset_t *)sigmask);

#else
    /* Emulate ppoll() via poll() - ignore signal mask */
    (void)sigmask;

    int timeout_ms;
    if (!timeout) {
        timeout_ms = -1;
    } else {
        timeout_ms = (int)(timeout->tv_sec * 1000 + timeout->tv_nsec / 1000000);
    }

    return poll(fds, nfds, timeout_ms);
#endif
}

int
nix_platform_pselect(int nfds, fd_set *readfds, fd_set *writefds,
                     fd_set *exceptfds, const struct timespec *timeout,
                     const void *sigmask)
{
#if defined(NIX_HOST_WIN32)
    /*
     * Windows doesn't have signal masks, so we ignore the sigmask parameter.
     * Convert timeout from timespec to timeval and call select().
     */
    (void)sigmask;  /* Unused on Windows */

    struct timeval tv;
    struct timeval *ptv = NULL;

    if (timeout) {
        tv.tv_sec = (long)timeout->tv_sec;
        tv.tv_usec = (long)(timeout->tv_nsec / 1000);
        ptv = &tv;
    }

    int result = select(nfds, readfds, writefds, exceptfds, ptv);
    if (result == SOCKET_ERROR) {
        nix_platform_set_errno(EINVAL);
        return -1;
    }

    return result;

#elif defined(HAVE_PSELECT)
    /* Use native pselect() if available */
    return pselect(nfds, readfds, writefds, exceptfds, timeout,
                   (const sigset_t *)sigmask);

#else
    /* Emulate pselect() via select() - ignore signal mask */
    (void)sigmask;

    struct timeval tv;
    struct timeval *ptv = NULL;

    if (timeout) {
        tv.tv_sec = (long)timeout->tv_sec;
        tv.tv_usec = (long)(timeout->tv_nsec / 1000);
        ptv = &tv;
    }

    return select(nfds, readfds, writefds, exceptfds, ptv);
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
    DWORD attrs = FILE_ATTRIBUTE_NORMAL;

    /* Access mode (O_RDONLY=0, O_WRONLY=1, O_RDWR=2) */
    int acc_mode = flags & 0x03;
    if (acc_mode == 0) {      /* O_RDONLY */
        access = GENERIC_READ;
    } else if (acc_mode == 1) { /* O_WRONLY */
        access = GENERIC_WRITE;
    } else if (acc_mode == 2) { /* O_RDWR */
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

    /* O_APPEND: FILE_APPEND_DATA */
    if (flags & 0x0008) {
        access = (access & ~GENERIC_WRITE) | FILE_APPEND_DATA;
    }

    return CreateFileA(path, access, share, NULL, creation, attrs, NULL);

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

nix_host_off_t
nix_platform_lseek(nix_host_fd_t fd, nix_host_off_t offset, int whence)
{
#if defined(NIX_HOST_WIN32)
    DWORD moveMethod;

    switch (whence) {
    case 0:  /* SEEK_SET */ moveMethod = FILE_BEGIN; break;
    case 1:  /* SEEK_CUR */ moveMethod = FILE_CURRENT; break;
    case 2:  /* SEEK_END */ moveMethod = FILE_END; break;
    default: SetLastError(ERROR_INVALID_PARAMETER); return -1;
    }

    LONG result = SetFilePointer(fd, offset, NULL, moveMethod);
    if (result == INVALID_SET_FILE_POINTER) {
        if (GetLastError() != NO_ERROR) {
            return -1;
        }
    }
    return result;

#elif defined(NIX_HOST_OS2)
    ULONG newPos = 0;
    ULONG moveType;

    switch (whence) {
    case 0: moveType = FILE_BEGIN; break;
    case 1: moveType = FILE_CURRENT; break;
    case 2: moveType = FILE_END; break;
    default: return -1;
    }

    if (DosSetFilePtr(fd, offset, moveType, &newPos) != NO_ERROR) {
        return -1;
    }
    return newPos;

#else
    return lseek(fd, offset, whence);
#endif
}

int
nix_platform_dup(nix_host_fd_t fd)
{
#if defined(NIX_HOST_WIN32)
    HANDLE hCurrent = GetCurrentProcess();
    HANDLE hDup;

    if (!DuplicateHandle(hCurrent, fd, hCurrent, &hDup,
                        0, TRUE, DUPLICATE_SAME_ACCESS)) {
        return -1;
    }
    return (int)(intptr_t)hDup;

#elif defined(NIX_HOST_OS2)
    HFILE hDup = 0;
    if (DosDupHandle(fd, &hDup) != NO_ERROR) {
        return -1;
    }
    return hDup;

#else
    return dup(fd);
#endif
}

int
nix_platform_dup2(nix_host_fd_t oldfd, nix_host_fd_t newfd)
{
#if defined(NIX_HOST_WIN32)
    /* Windows doesn't have dup2 directly, need to close newfd first if valid */
    if (newfd != INVALID_HANDLE_VALUE && newfd != oldfd) {
        CloseHandle(newfd);
    }

    HANDLE hCurrent = GetCurrentProcess();
    HANDLE hDup;

    if (!DuplicateHandle(hCurrent, oldfd, hCurrent, &hDup,
                        0, TRUE, DUPLICATE_SAME_ACCESS)) {
        return -1;
    }
    return (int)(intptr_t)hDup;

#elif defined(NIX_HOST_OS2)
    HFILE hNew = newfd;
    if (DosDupHandle(oldfd, &hNew) != NO_ERROR) {
        return -1;
    }
    return hNew;

#else
    return dup2(oldfd, newfd);
#endif
}

int
nix_platform_access(const char *path, int mode)
{
#if defined(NIX_HOST_WIN32)
    /* Use _access from io.h */
    int win_mode = 0;

    /* R_OK=4, W_OK=2, X_OK=1, F_OK=0 */
    if (mode == 0) {  /* F_OK */
        win_mode = 0;
    } else {
        if (mode & 4) win_mode |= 4;  /* Read */
        if (mode & 2) win_mode |= 2;  /* Write */
        /* Windows doesn't check execute permission via _access */
    }

    return _access(path, win_mode);

#else
    return access(path, mode);
#endif
}

int
nix_platform_unlink(const char *path)
{
#if defined(NIX_HOST_WIN32)
    return DeleteFileA(path) ? 0 : -1;

#elif defined(NIX_HOST_OS2)
    return (DosDelete((PSZ)path) == NO_ERROR) ? 0 : -1;

#else
    return unlink(path);
#endif
}

int
nix_platform_rename(const char *oldpath, const char *newpath)
{
#if defined(NIX_HOST_WIN32)
    return MoveFileA(oldpath, newpath) ? 0 : -1;

#elif defined(NIX_HOST_OS2)
    return (DosMove((PSZ)oldpath, (PSZ)newpath) == NO_ERROR) ? 0 : -1;

#else
    return rename(oldpath, newpath);
#endif
}

int
nix_platform_fsync(nix_host_fd_t fd)
{
#if defined(NIX_HOST_WIN32)
    return FlushFileBuffers(fd) ? 0 : -1;

#elif defined(NIX_HOST_OS2)
    return (DosResetBuffer(fd) == NO_ERROR) ? 0 : -1;

#else
    return fsync(fd);
#endif
}

/*
 * ========================================================================
 * PLATFORM DIRECTORY OPERATIONS
 * ========================================================================
 */

int
nix_platform_mkdir(const char *path, nix_host_mode_t mode)
{
#if defined(NIX_HOST_WIN32)
    (void)mode;  /* Windows doesn't use mode parameter */
    return CreateDirectoryA(path, NULL) ? 0 : -1;

#elif defined(NIX_HOST_OS2)
    (void)mode;
    return (DosCreateDir((PSZ)path, NULL) == NO_ERROR) ? 0 : -1;

#else
    return mkdir(path, mode);
#endif
}

int
nix_platform_rmdir(const char *path)
{
#if defined(NIX_HOST_WIN32)
    return RemoveDirectoryA(path) ? 0 : -1;

#elif defined(NIX_HOST_OS2)
    return (DosDeleteDir((PSZ)path) == NO_ERROR) ? 0 : -1;

#else
    return rmdir(path);
#endif
}

int
nix_platform_chdir(const char *path)
{
#if defined(NIX_HOST_WIN32)
    return SetCurrentDirectoryA(path) ? 0 : -1;

#elif defined(NIX_HOST_OS2)
    return (DosSetCurrentDir((PSZ)path) == NO_ERROR) ? 0 : -1;

#else
    return chdir(path);
#endif
}

char *
nix_platform_getcwd(char *buf, size_t size)
{
#if defined(NIX_HOST_WIN32)
    DWORD result = GetCurrentDirectoryA((DWORD)size, buf);
    if (result == 0 || result > size) {
        return NULL;
    }
    return buf;

#elif defined(NIX_HOST_OS2)
    ULONG len = size;
    ULONG drive;

    if (DosQueryCurrentDir(0, (PBYTE)buf, &len) != NO_ERROR) {
        return NULL;
    }
    return buf;

#else
    return getcwd(buf, size);
#endif
}

/*
 * ========================================================================
 * PLATFORM STAT OPERATIONS
 * ========================================================================
 */

#if defined(NIX_HOST_WIN32)
/* Helper function to convert Windows file time to Unix time_t */
static time_t
filetime_to_time_t(const FILETIME *ft)
{
    ULARGE_INTEGER ull;
    ull.LowPart = ft->dwLowDateTime;
    ull.HighPart = ft->dwHighDateTime;

    /* Convert from 100-nanosecond intervals since 1601 to seconds since 1970 */
    return (time_t)((ull.QuadPart / 10000000ULL) - 11644473600ULL);
}

/* Helper function to convert Windows attributes to Unix mode */
static nix_host_mode_t
win_attrs_to_mode(DWORD attrs)
{
    nix_host_mode_t mode = 0;

    if (attrs & FILE_ATTRIBUTE_DIRECTORY) {
        mode |= 0040000;  /* S_IFDIR */
        mode |= 0000755;  /* rwxr-xr-x */
    } else {
        mode |= 0100000;  /* S_IFREG */
        mode |= 0000644;  /* rw-r--r-- */
    }

    if (attrs & FILE_ATTRIBUTE_READONLY) {
        mode &= ~0000222;  /* Remove write permissions */
    }

    return mode;
}
#endif

int
nix_platform_stat(const char *path, struct nix_platform_stat *buf)
{
#if defined(NIX_HOST_WIN32)
    WIN32_FILE_ATTRIBUTE_DATA fad;

    if (!GetFileAttributesExA(path, GetFileExInfoStandard, &fad)) {
        return -1;
    }

    memset(buf, 0, sizeof(*buf));

    buf->st_mode = win_attrs_to_mode(fad.dwFileAttributes);
    buf->st_size = ((nix_host_off_t)fad.nFileSizeHigh << 32) | fad.nFileSizeLow;
    buf->st_atime = filetime_to_time_t(&fad.ftLastAccessTime);
    buf->st_mtime = filetime_to_time_t(&fad.ftLastWriteTime);
    buf->st_ctime = filetime_to_time_t(&fad.ftCreationTime);
    buf->st_nlink = 1;
    buf->st_blksize = 4096;
    buf->st_blocks = (buf->st_size + 511) / 512;

    return 0;

#elif defined(NIX_HOST_OS2)
    /* OS/2 stat implementation */
    struct stat st;
    if (stat(path, &st) != 0) {
        return -1;
    }

    buf->st_dev = st.st_dev;
    buf->st_ino = st.st_ino;
    buf->st_mode = st.st_mode;
    buf->st_nlink = st.st_nlink;
    buf->st_uid = st.st_uid;
    buf->st_gid = st.st_gid;
    buf->st_rdev = st.st_rdev;
    buf->st_size = st.st_size;
    buf->st_atime = st.st_atime;
    buf->st_mtime = st.st_mtime;
    buf->st_ctime = st.st_ctime;
    buf->st_blksize = 4096;
    buf->st_blocks = (st.st_size + 511) / 512;

    return 0;

#else
    struct stat st;
    if (stat(path, &st) != 0) {
        return -1;
    }

    buf->st_dev = st.st_dev;
    buf->st_ino = st.st_ino;
    buf->st_mode = st.st_mode;
    buf->st_nlink = st.st_nlink;
    buf->st_uid = st.st_uid;
    buf->st_gid = st.st_gid;
    buf->st_rdev = st.st_rdev;
    buf->st_size = st.st_size;
    buf->st_atime = st.st_atime;
    buf->st_mtime = st.st_mtime;
    buf->st_ctime = st.st_ctime;

#ifdef __linux__
    buf->st_blksize = st.st_blksize;
    buf->st_blocks = st.st_blocks;
#else
    buf->st_blksize = 4096;
    buf->st_blocks = (st.st_size + 511) / 512;
#endif

    return 0;
#endif
}

int
nix_platform_fstat(nix_host_fd_t fd, struct nix_platform_stat *buf)
{
#if defined(NIX_HOST_WIN32)
    BY_HANDLE_FILE_INFORMATION fi;

    if (!GetFileInformationByHandle(fd, &fi)) {
        return -1;
    }

    memset(buf, 0, sizeof(*buf));

    buf->st_dev = fi.dwVolumeSerialNumber;
    buf->st_ino = ((nix_host_ino_t)fi.nFileIndexHigh << 32) | fi.nFileIndexLow;
    buf->st_mode = win_attrs_to_mode(fi.dwFileAttributes);
    buf->st_nlink = fi.nNumberOfLinks;
    buf->st_size = ((nix_host_off_t)fi.nFileSizeHigh << 32) | fi.nFileSizeLow;
    buf->st_atime = filetime_to_time_t(&fi.ftLastAccessTime);
    buf->st_mtime = filetime_to_time_t(&fi.ftLastWriteTime);
    buf->st_ctime = filetime_to_time_t(&fi.ftCreationTime);
    buf->st_blksize = 4096;
    buf->st_blocks = (buf->st_size + 511) / 512;

    return 0;

#elif defined(NIX_HOST_OS2)
    struct stat st;
    if (fstat(fd, &st) != 0) {
        return -1;
    }

    buf->st_dev = st.st_dev;
    buf->st_ino = st.st_ino;
    buf->st_mode = st.st_mode;
    buf->st_nlink = st.st_nlink;
    buf->st_uid = st.st_uid;
    buf->st_gid = st.st_gid;
    buf->st_rdev = st.st_rdev;
    buf->st_size = st.st_size;
    buf->st_atime = st.st_atime;
    buf->st_mtime = st.st_mtime;
    buf->st_ctime = st.st_ctime;
    buf->st_blksize = 4096;
    buf->st_blocks = (st.st_size + 511) / 512;

    return 0;

#else
    struct stat st;
    if (fstat(fd, &st) != 0) {
        return -1;
    }

    buf->st_dev = st.st_dev;
    buf->st_ino = st.st_ino;
    buf->st_mode = st.st_mode;
    buf->st_nlink = st.st_nlink;
    buf->st_uid = st.st_uid;
    buf->st_gid = st.st_gid;
    buf->st_rdev = st.st_rdev;
    buf->st_size = st.st_size;
    buf->st_atime = st.st_atime;
    buf->st_mtime = st.st_mtime;
    buf->st_ctime = st.st_ctime;

#ifdef __linux__
    buf->st_blksize = st.st_blksize;
    buf->st_blocks = st.st_blocks;
#else
    buf->st_blksize = 4096;
    buf->st_blocks = (st.st_size + 511) / 512;
#endif

    return 0;
#endif
}

int
nix_platform_lstat(const char *path, struct nix_platform_stat *buf)
{
#if defined(NIX_HOST_WIN32)
    /* Windows doesn't have symlinks (or has them only in recent versions)
     * Just use stat() for compatibility */
    return nix_platform_stat(path, buf);

#elif defined(NIX_HOST_OS2)
    /* OS/2 doesn't have symlinks, use stat() */
    return nix_platform_stat(path, buf);

#else
    struct stat st;
    if (lstat(path, &st) != 0) {
        return -1;
    }

    buf->st_dev = st.st_dev;
    buf->st_ino = st.st_ino;
    buf->st_mode = st.st_mode;
    buf->st_nlink = st.st_nlink;
    buf->st_uid = st.st_uid;
    buf->st_gid = st.st_gid;
    buf->st_rdev = st.st_rdev;
    buf->st_size = st.st_size;
    buf->st_atime = st.st_atime;
    buf->st_mtime = st.st_mtime;
    buf->st_ctime = st.st_ctime;

#ifdef __linux__
    buf->st_blksize = st.st_blksize;
    buf->st_blocks = st.st_blocks;
#else
    buf->st_blksize = 4096;
    buf->st_blocks = (st.st_size + 511) / 512;
#endif

    return 0;
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
    return find_thread(NULL);  /* Returns thread ID */

#elif defined(NIX_HOST_OS2)
    PTIB ptib;
    PPIB ppib;
    DosGetInfoBlocks(&ptib, &ppib);
    return ppib->pib_ulpid;

#elif defined(NIX_HOST_OPENVMS)
    return getpid();

#else
    return getpid();
#endif
}

nix_host_pid_t
nix_platform_fork(void)
{
#if defined(NIX_HOST_WIN32)
    /*
     * Windows fork() emulation using NT API for better process control.
     *
     * This implementation uses:
     * - NtCreateProcess/NtCreateProcessEx for process creation
     * - RtlCloneUserProcess for copy-on-write semantics (Vista+)
     * - Handle and memory duplication
     *
     * Strategy:
     * 1. Try RtlCloneUserProcess (Vista+) - closest to real fork
     * 2. Fall back to NtCreateProcess with section handle
     * 3. Duplicate critical handles and setup child context
     *
     * Even with NT API, Windows fork limitations:
     * - Thread-local storage differs
     * - Some kernel objects can't be duplicated
     * - DLL state may not transfer correctly
     * - Best effort approximation of Unix fork
     */

    HMODULE ntdll = GetModuleHandleA("ntdll.dll");
    if (!ntdll) {
        nix_platform_set_errno(ENOSYS);
        return -1;
    }

    /* Try RtlCloneUserProcess first (Vista+) - most fork-like */
    typedef NTSTATUS (NTAPI *RtlCloneUserProcess_t)(
        ULONG ProcessFlags,
        PSECURITY_DESCRIPTOR ProcessSecurityDescriptor,
        PSECURITY_DESCRIPTOR ThreadSecurityDescriptor,
        HANDLE DebugPort,
        PVOID *ProcessInfo
    );

    typedef struct _RTL_USER_PROCESS_INFORMATION {
        ULONG Length;
        HANDLE ProcessHandle;
        HANDLE ThreadHandle;
        CLIENT_ID ClientId;
        PVOID ImageInformation;
    } RTL_USER_PROCESS_INFORMATION, *PRTL_USER_PROCESS_INFORMATION;

    RtlCloneUserProcess_t pRtlCloneUserProcess =
        (RtlCloneUserProcess_t)GetProcAddress(ntdll, "RtlCloneUserProcess");

    if (pRtlCloneUserProcess) {
        RTL_USER_PROCESS_INFORMATION processInfo;
        NTSTATUS status;

        processInfo.Length = sizeof(processInfo);

        /* Clone the current process */
        status = pRtlCloneUserProcess(
            0,      /* ProcessFlags */
            NULL,   /* ProcessSecurityDescriptor */
            NULL,   /* ThreadSecurityDescriptor */
            NULL,   /* DebugPort */
            &processInfo
        );

        if (NT_SUCCESS(status)) {
            /* Check if we're parent or child */
            if (processInfo.ProcessHandle == NULL &&
                processInfo.ThreadHandle == NULL) {
                /* We are the child process */
                return 0;
            }

            /* We are the parent process */
            nix_host_pid_t child_pid = (nix_host_pid_t)processInfo.ClientId.UniqueProcess;

            /* Resume child thread */
            ResumeThread(processInfo.ThreadHandle);

            /* Close handles */
            CloseHandle(processInfo.ThreadHandle);
            CloseHandle(processInfo.ProcessHandle);

            return child_pid;
        }
        /* If RtlCloneUserProcess failed, fall through to legacy method */
    }

    /*
     * Fallback: CreateProcess-based fork emulation (NT 3.1+)
     * Less fork-like but works on all Windows versions
     */
    STARTUPINFOA si;
    PROCESS_INFORMATION pi;
    char cmdline[4096];
    char env_buf[8192];
    char *env_ptr = env_buf;
    DWORD env_size = sizeof(env_buf);
    DWORD written = 0;

    /* Get current executable path */
    char exe_path[MAX_PATH];
    if (!GetModuleFileNameA(NULL, exe_path, sizeof(exe_path))) {
        nix_platform_set_errno(ENOMEM);
        return -1;
    }

    /* Build command line */
    _snprintf(cmdline, sizeof(cmdline), "\"%s\"", exe_path);

    /* Build environment with __NIX_FORK_CHILD__ marker */
    _snprintf(env_ptr, env_size - written, "__NIX_FORK_CHILD__=%lu", GetCurrentProcessId());
    written = (DWORD)strlen(env_ptr) + 1;
    env_ptr += written;

    /* Copy existing environment */
    char *parent_env = GetEnvironmentStrings();
    if (parent_env) {
        char *p = parent_env;
        while (*p && (written < env_size - 2)) {
            size_t len = strlen(p);
            if (strncmp(p, "__NIX_FORK_", 11) != 0) {
                if (written + len + 1 < env_size) {
                    memcpy(env_ptr, p, len + 1);
                    env_ptr += len + 1;
                    written += (DWORD)(len + 1);
                }
            }
            p += len + 1;
        }
        FreeEnvironmentStrings(parent_env);
    }
    *env_ptr = '\0';

    /* Setup startup info */
    ZeroMemory(&si, sizeof(si));
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
    si.hStdOutput = GetStdHandle(STD_OUTPUT_HANDLE);
    si.hStdError = GetStdHandle(STD_ERROR_HANDLE);

    /* Create child process */
    if (!CreateProcessA(exe_path, cmdline, NULL, NULL, TRUE,
                        0, env_buf, NULL, &si, &pi)) {
        DWORD error = GetLastError();
        if (error == ERROR_NOT_ENOUGH_MEMORY || error == ERROR_OUTOFMEMORY) {
            nix_platform_set_errno(ENOMEM);
        } else {
            nix_platform_set_errno(EAGAIN);
        }
        return -1;
    }

    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);

    return (nix_host_pid_t)pi.dwProcessId;

#elif defined(NIX_HOST_HAIKU)
    return fork();

#elif defined(NIX_HOST_OS2)
    /* OS/2 uses DosExecPgm instead of fork */
    NIX_ERROR("fork() not directly supported on OS/2 - use DosExecPgm instead");
    errno = ENOSYS;
    return -1;

#elif defined(NIX_HOST_OPENVMS)
    /* OpenVMS doesn't have fork(), would use LIB$SPAWN */
    NIX_ERROR("fork() not supported on OpenVMS - use LIB$SPAWN instead");
    errno = ENOSYS;
    return -1;

#else
    return fork();
#endif
}

nix_host_pid_t
nix_platform_waitpid(nix_host_pid_t pid, int *status, int options)
{
#if defined(NIX_HOST_WIN32)
    HANDLE hProcess;
    DWORD exitCode;
    DWORD waitResult;

    hProcess = OpenProcess(SYNCHRONIZE | PROCESS_QUERY_INFORMATION, FALSE, pid);
    if (hProcess == NULL) {
        return -1;
    }

    /* Convert options - WNOHANG = don't wait */
    DWORD timeout = (options & 1) ? 0 : INFINITE;

    waitResult = WaitForSingleObject(hProcess, timeout);

    if (waitResult == WAIT_TIMEOUT) {
        CloseHandle(hProcess);
        return 0;  /* WNOHANG and child still running */
    }

    if (waitResult != WAIT_OBJECT_0) {
        CloseHandle(hProcess);
        return -1;
    }

    if (GetExitCodeProcess(hProcess, &exitCode)) {
        if (status != NULL) {
            *status = (int)exitCode;
        }
    }

    CloseHandle(hProcess);
    return pid;

#elif defined(NIX_HOST_OS2)
    /* OS/2 waitpid implementation using DosWaitChild */
    errno = ENOSYS;
    return -1;

#else
    return waitpid(pid, status, options);
#endif
}

int
nix_platform_kill(nix_host_pid_t pid, int sig)
{
    /* Use the signal-aware implementation */
    return nix_platform_kill_signal(pid, sig);
}

nix_host_pid_t
nix_platform_getppid(void)
{
#if defined(NIX_HOST_WIN32)
    /*
     * Windows doesn't have a direct getppid(), but we can use NtQueryInformationProcess
     * or traverse the process tree via CreateToolhelp32Snapshot.
     * For simplicity, return 1 (system process) or implement snapshot approach.
     */
    HANDLE hSnapshot;
    PROCESSENTRY32 pe32;
    DWORD currentPid = GetCurrentProcessId();
    DWORD parentPid = 0;

    hSnapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (hSnapshot == INVALID_HANDLE_VALUE) {
        return 1;  /* Fallback to init-like PID */
    }

    pe32.dwSize = sizeof(PROCESSENTRY32);
    if (!Process32First(hSnapshot, &pe32)) {
        CloseHandle(hSnapshot);
        return 1;
    }

    do {
        if (pe32.th32ProcessID == currentPid) {
            parentPid = pe32.th32ParentProcessID;
            break;
        }
    } while (Process32Next(hSnapshot, &pe32));

    CloseHandle(hSnapshot);
    return (nix_host_pid_t)parentPid;

#elif defined(NIX_HOST_HAIKU)
    /* Haiku has getppid for processes */
    return getppid();

#elif defined(NIX_HOST_OS2)
    PTIB ptib;
    PPIB ppib;
    DosGetInfoBlocks(&ptib, &ppib);
    return ppib->pib_ulppid;

#else
    return getppid();
#endif
}

nix_host_pid_t
nix_platform_vfork(void)
{
    /*
     * vfork() is an optimization of fork() that shares memory until exec.
     * On platforms without vfork, we can just call fork().
     * Windows doesn't have either, so same as fork().
     */
    return nix_platform_fork();
}

void
nix_platform_exit(int status)
{
#if defined(NIX_HOST_WIN32)
    ExitProcess((UINT)status);

#elif defined(NIX_HOST_OS2)
    DosExit(EXIT_PROCESS, status);

#else
    exit(status);
#endif
}

void
nix_platform__exit(int status)
{
#if defined(NIX_HOST_WIN32)
    /* _exit is immediate, no cleanup */
    TerminateProcess(GetCurrentProcess(), (UINT)status);

#elif defined(NIX_HOST_OS2)
    DosExit(EXIT_PROCESS, status);

#else
    _exit(status);
#endif
}

nix_host_pid_t
nix_platform_wait(int *status)
{
    /* wait() is equivalent to waitpid(-1, status, 0) */
    return nix_platform_waitpid((nix_host_pid_t)-1, status, 0);
}

nix_host_pid_t
nix_platform_wait3(int *status, int options, void *rusage)
{
    /*
     * wait3() is like waitpid(-1, ...) but also returns resource usage.
     * Windows: We'll implement rusage collection if requested.
     */
#if defined(NIX_HOST_WIN32)
    /* For now, ignore rusage and just call waitpid */
    (void)rusage;  /* TODO: Implement rusage collection */
    return nix_platform_waitpid((nix_host_pid_t)-1, status, options);

#elif defined(HAVE_WAIT3)
    return wait3(status, options, (struct rusage *)rusage);

#else
    /* Fallback to waitpid, ignore rusage */
    (void)rusage;
    return waitpid(-1, status, options);
#endif
}

nix_host_pid_t
nix_platform_wait4(nix_host_pid_t pid, int *status, int options, void *rusage)
{
    /*
     * wait4() is like waitpid() but also returns resource usage.
     */
#if defined(NIX_HOST_WIN32)
    /* For now, ignore rusage and just call waitpid */
    (void)rusage;  /* TODO: Implement rusage collection */
    return nix_platform_waitpid(pid, status, options);

#elif defined(HAVE_WAIT4)
    return wait4(pid, status, options, (struct rusage *)rusage);

#else
    /* Fallback to waitpid, ignore rusage */
    (void)rusage;
    return waitpid(pid, status, options);
#endif
}

/*
 * ========================================================================
 * PROCESS EXECUTION (exec family)
 * ========================================================================
 */

int
nix_platform_execv(const char *path, char *const argv[])
{
#if defined(NIX_HOST_WIN32)
    /* Use _execv from MSVCRT */
    return _execv(path, argv);

#else
    return execv(path, argv);
#endif
}

int
nix_platform_execvp(const char *file, char *const argv[])
{
#if defined(NIX_HOST_WIN32)
    /* Use _execvp from MSVCRT - searches PATH */
    return _execvp(file, argv);

#else
    return execvp(file, argv);
#endif
}

int
nix_platform_execvpe(const char *file, char *const argv[], char *const envp[])
{
#if defined(NIX_HOST_WIN32)
    /* Use _execvpe from MSVCRT */
    return _execvpe(file, argv, envp);

#elif defined(HAVE_EXECVPE)
    return execvpe(file, argv, envp);

#else
    /* Emulate execvpe using execve and PATH search */
    nix_platform_set_errno(ENOSYS);
    return -1;
#endif
}

int
nix_platform_execl(const char *path, const char *arg, ...)
{
    /* Build argv array from variadic arguments */
    va_list ap;
    int argc = 1;  /* Start with 1 for arg0 */
    const char *a;

    /* Count arguments */
    va_start(ap, arg);
    while ((a = va_arg(ap, const char *)) != NULL) {
        argc++;
    }
    va_end(ap);

    /* Allocate argv array */
    const char **argv = (const char **)malloc((argc + 1) * sizeof(char *));
    if (!argv) {
        nix_platform_set_errno(ENOMEM);
        return -1;
    }

    /* Populate argv */
    argv[0] = arg;
    va_start(ap, arg);
    for (int i = 1; i < argc; i++) {
        argv[i] = va_arg(ap, const char *);
    }
    argv[argc] = NULL;
    va_end(ap);

    /* Call execv */
    int result = nix_platform_execv(path, (char *const *)argv);
    free(argv);
    return result;
}

int
nix_platform_execlp(const char *file, const char *arg, ...)
{
    /* Build argv array from variadic arguments */
    va_list ap;
    int argc = 1;
    const char *a;

    va_start(ap, arg);
    while ((a = va_arg(ap, const char *)) != NULL) {
        argc++;
    }
    va_end(ap);

    const char **argv = (const char **)malloc((argc + 1) * sizeof(char *));
    if (!argv) {
        nix_platform_set_errno(ENOMEM);
        return -1;
    }

    argv[0] = arg;
    va_start(ap, arg);
    for (int i = 1; i < argc; i++) {
        argv[i] = va_arg(ap, const char *);
    }
    argv[argc] = NULL;
    va_end(ap);

    int result = nix_platform_execvp(file, (char *const *)argv);
    free(argv);
    return result;
}

int
nix_platform_execle(const char *path, const char *arg, ...)
{
    /* Build argv array, environment is after NULL terminator */
    va_list ap;
    int argc = 1;
    const char *a;

    va_start(ap, arg);
    while ((a = va_arg(ap, const char *)) != NULL) {
        argc++;
    }
    char *const *envp = va_arg(ap, char *const *);
    va_end(ap);

    const char **argv = (const char **)malloc((argc + 1) * sizeof(char *));
    if (!argv) {
        nix_platform_set_errno(ENOMEM);
        return -1;
    }

    argv[0] = arg;
    va_start(ap, arg);
    for (int i = 1; i < argc; i++) {
        argv[i] = va_arg(ap, const char *);
    }
    argv[argc] = NULL;
    va_end(ap);

    int result = nix_platform_execve(path, (char *const *)argv, envp);
    free(argv);
    return result;
}

/*
 * ========================================================================
 * PROCESS GROUPS AND SESSIONS
 * ========================================================================
 */

nix_host_pid_t
nix_platform_getpgid(nix_host_pid_t pid)
{
#if defined(NIX_HOST_WIN32)
    /*
     * Windows doesn't have process groups in the Unix sense.
     * We could emulate this, but for now return the PID itself.
     */
    (void)pid;
    return pid ? pid : GetCurrentProcessId();

#elif defined(HAVE_GETPGID)
    return getpgid(pid);

#else
    nix_platform_set_errno(ENOSYS);
    return -1;
#endif
}

nix_host_pid_t
nix_platform_getpgrp(void)
{
#if defined(NIX_HOST_WIN32)
    /* Return own PID as process group */
    return GetCurrentProcessId();

#else
    return getpgrp();
#endif
}

int
nix_platform_setpgid(nix_host_pid_t pid, nix_host_pid_t pgid)
{
#if defined(NIX_HOST_WIN32)
    /*
     * Windows doesn't support setpgid.
     * We could create process groups via CreateProcess with CREATE_NEW_PROCESS_GROUP,
     * but that's at creation time only.
     */
    (void)pid;
    (void)pgid;
    nix_platform_set_errno(ENOSYS);
    return -1;

#else
    return setpgid(pid, pgid);
#endif
}

int
nix_platform_setpgrp(void)
{
#if defined(NIX_HOST_WIN32)
    nix_platform_set_errno(ENOSYS);
    return -1;

#elif defined(HAVE_SETPGRP)
    return setpgrp();

#else
    return setpgid(0, 0);
#endif
}

nix_host_pid_t
nix_platform_getsid(nix_host_pid_t pid)
{
#if defined(NIX_HOST_WIN32)
    /*
     * Windows has sessions but they're different from Unix.
     * Return process group (which is PID for us).
     */
    return nix_platform_getpgid(pid);

#elif defined(HAVE_GETSID)
    return getsid(pid);

#else
    nix_platform_set_errno(ENOSYS);
    return -1;
#endif
}

nix_host_pid_t
nix_platform_setsid(void)
{
#if defined(NIX_HOST_WIN32)
    /*
     * We could emulate this by creating a new console or detaching,
     * but for now just return error.
     */
    nix_platform_set_errno(ENOSYS);
    return -1;

#else
    return setsid();
#endif
}

/*
 * ========================================================================
 * USER AND GROUP IDS
 * ========================================================================
 */

unsigned int
nix_platform_getuid(void)
{
#if defined(NIX_HOST_WIN32)
    /*
     * Windows doesn't have UIDs. We could return a hash of the username
     * or SID, but for now just return 0 (root equivalent).
     */
    return 0;

#else
    return getuid();
#endif
}

unsigned int
nix_platform_geteuid(void)
{
#if defined(NIX_HOST_WIN32)
    return 0;

#else
    return geteuid();
#endif
}

unsigned int
nix_platform_getgid(void)
{
#if defined(NIX_HOST_WIN32)
    return 0;

#else
    return getgid();
#endif
}

unsigned int
nix_platform_getegid(void)
{
#if defined(NIX_HOST_WIN32)
    return 0;

#else
    return getegid();
#endif
}

int
nix_platform_setuid(unsigned int uid)
{
#if defined(NIX_HOST_WIN32)
    /*
     * Windows doesn't support setuid. To truly change user,
     * we'd need to use CreateProcessAsUser or ImpersonateLoggedOnUser.
     */
    (void)uid;
    nix_platform_set_errno(EPERM);
    return -1;

#else
    return setuid(uid);
#endif
}

int
nix_platform_seteuid(unsigned int euid)
{
#if defined(NIX_HOST_WIN32)
    (void)euid;
    nix_platform_set_errno(EPERM);
    return -1;

#else
    return seteuid(euid);
#endif
}

int
nix_platform_setgid(unsigned int gid)
{
#if defined(NIX_HOST_WIN32)
    (void)gid;
    nix_platform_set_errno(EPERM);
    return -1;

#else
    return setgid(gid);
#endif
}

int
nix_platform_setegid(unsigned int egid)
{
#if defined(NIX_HOST_WIN32)
    (void)egid;
    nix_platform_set_errno(EPERM);
    return -1;

#else
    return setegid(egid);
#endif
}

int
nix_platform_setreuid(unsigned int ruid, unsigned int euid)
{
#if defined(NIX_HOST_WIN32)
    (void)ruid;
    (void)euid;
    nix_platform_set_errno(EPERM);
    return -1;

#else
    return setreuid(ruid, euid);
#endif
}

int
nix_platform_setregid(unsigned int rgid, unsigned int egid)
{
#if defined(NIX_HOST_WIN32)
    (void)rgid;
    (void)egid;
    nix_platform_set_errno(EPERM);
    return -1;

#else
    return setregid(rgid, egid);
#endif
}

/*
 * ========================================================================
 * PROCESS PRIORITY
 * ========================================================================
 */

int
nix_platform_nice(int inc)
{
#if defined(NIX_HOST_WIN32)
    /*
     * Windows uses priority classes, not nice values.
     * Map nice values to Windows priority classes:
     * nice < -10  → HIGH_PRIORITY_CLASS
     * nice < 0    → ABOVE_NORMAL_PRIORITY_CLASS
     * nice == 0   → NORMAL_PRIORITY_CLASS
     * nice < 10   → BELOW_NORMAL_PRIORITY_CLASS
     * nice >= 10  → IDLE_PRIORITY_CLASS
     */
    HANDLE hProcess = GetCurrentProcess();
    DWORD currentPriority = GetPriorityClass(hProcess);
    DWORD newPriority;

    /* Estimate current nice value from priority class */
    int currentNice = 0;
    if (currentPriority == HIGH_PRIORITY_CLASS) {
        currentNice = -15;
    } else if (currentPriority == ABOVE_NORMAL_PRIORITY_CLASS) {
        currentNice = -5;
    } else if (currentPriority == NORMAL_PRIORITY_CLASS) {
        currentNice = 0;
    } else if (currentPriority == BELOW_NORMAL_PRIORITY_CLASS) {
        currentNice = 5;
    } else if (currentPriority == IDLE_PRIORITY_CLASS) {
        currentNice = 19;
    }

    int newNice = currentNice + inc;

    /* Map to priority class */
    if (newNice < -10) {
        newPriority = HIGH_PRIORITY_CLASS;
    } else if (newNice < 0) {
        newPriority = ABOVE_NORMAL_PRIORITY_CLASS;
    } else if (newNice < 10) {
        newPriority = BELOW_NORMAL_PRIORITY_CLASS;
    } else {
        newPriority = IDLE_PRIORITY_CLASS;
    }

    if (!SetPriorityClass(hProcess, newPriority)) {
        nix_platform_set_errno(EPERM);
        return -1;
    }

    return newNice;

#else
    return nice(inc);
#endif
}

int
nix_platform_getpriority(int which, int who)
{
#if defined(NIX_HOST_WIN32)
    /*
     * Map Windows priority class to Unix nice value.
     */
    HANDLE hProcess;
    DWORD priority;

    if (which == PRIO_PROCESS) {
        if (who == 0) {
            hProcess = GetCurrentProcess();
        } else {
            hProcess = OpenProcess(PROCESS_QUERY_INFORMATION, FALSE, (DWORD)who);
            if (!hProcess) {
                nix_platform_set_errno(ESRCH);
                return -1;
            }
        }

        priority = GetPriorityClass(hProcess);

        if (who != 0) {
            CloseHandle(hProcess);
        }

        /* Map priority class to nice value */
        switch (priority) {
        case HIGH_PRIORITY_CLASS:           return -15;
        case ABOVE_NORMAL_PRIORITY_CLASS:   return -5;
        case NORMAL_PRIORITY_CLASS:         return 0;
        case BELOW_NORMAL_PRIORITY_CLASS:   return 5;
        case IDLE_PRIORITY_CLASS:           return 19;
        default:                            return 0;
        }
    }

    /* Process groups and users not supported on Windows */
    nix_platform_set_errno(EINVAL);
    return -1;

#else
    return getpriority(which, who);
#endif
}

int
nix_platform_setpriority(int which, int who, int prio)
{
#if defined(NIX_HOST_WIN32)
    /*
     * Map Unix nice value to Windows priority class.
     */
    HANDLE hProcess;
    DWORD priority;

    if (which == PRIO_PROCESS) {
        /* Map nice value to priority class */
        if (prio < -10) {
            priority = HIGH_PRIORITY_CLASS;
        } else if (prio < 0) {
            priority = ABOVE_NORMAL_PRIORITY_CLASS;
        } else if (prio == 0) {
            priority = NORMAL_PRIORITY_CLASS;
        } else if (prio < 10) {
            priority = BELOW_NORMAL_PRIORITY_CLASS;
        } else {
            priority = IDLE_PRIORITY_CLASS;
        }

        if (who == 0) {
            hProcess = GetCurrentProcess();
        } else {
            hProcess = OpenProcess(PROCESS_SET_INFORMATION, FALSE, (DWORD)who);
            if (!hProcess) {
                nix_platform_set_errno(ESRCH);
                return -1;
            }
        }

        if (!SetPriorityClass(hProcess, priority)) {
            if (who != 0) {
                CloseHandle(hProcess);
            }
            nix_platform_set_errno(EPERM);
            return -1;
        }

        if (who != 0) {
            CloseHandle(hProcess);
        }

        return 0;
    }

    /* Process groups and users not supported on Windows */
    nix_platform_set_errno(EINVAL);
    return -1;

#else
    return setpriority(which, who, prio);
#endif
}

/*
 * ========================================================================
 * RESOURCE USAGE
 * ========================================================================
 */

int
nix_platform_getrusage(int who, void *usage)
{
#if defined(NIX_HOST_WIN32)
    /*
     * Windows has GetProcessTimes and GetProcessMemoryInfo for resource usage.
     * Map to struct rusage format.
     */
    struct rusage {
        struct timeval ru_utime;  /* user CPU time */
        struct timeval ru_stime;  /* system CPU time */
        long ru_maxrss;           /* maximum resident set size */
        long ru_ixrss;            /* integral shared memory size */
        long ru_idrss;            /* integral unshared data size */
        long ru_isrss;            /* integral unshared stack size */
        long ru_minflt;           /* page reclaims (soft page faults) */
        long ru_majflt;           /* page faults (hard page faults) */
        long ru_nswap;            /* swaps */
        long ru_inblock;          /* block input operations */
        long ru_oublock;          /* block output operations */
        long ru_msgsnd;           /* IPC messages sent */
        long ru_msgrcv;           /* IPC messages received */
        long ru_nsignals;         /* signals received */
        long ru_nvcsw;            /* voluntary context switches */
        long ru_nivcsw;           /* involuntary context switches */
    };
    struct rusage *ru = (struct rusage *)usage;

    if (who == RUSAGE_SELF) {
        HANDLE hProcess = GetCurrentProcess();
        FILETIME createTime, exitTime, kernelTime, userTime;
        PROCESS_MEMORY_COUNTERS_EX memInfo;

        /* Get CPU times */
        if (GetProcessTimes(hProcess, &createTime, &exitTime, &kernelTime, &userTime)) {
            ULARGE_INTEGER uli;

            /* User time */
            uli.LowPart = userTime.dwLowDateTime;
            uli.HighPart = userTime.dwHighDateTime;
            ru->ru_utime.tv_sec = (long)(uli.QuadPart / 10000000ULL);
            ru->ru_utime.tv_usec = (long)((uli.QuadPart % 10000000ULL) / 10);

            /* Kernel time */
            uli.LowPart = kernelTime.dwLowDateTime;
            uli.HighPart = kernelTime.dwHighDateTime;
            ru->ru_stime.tv_sec = (long)(uli.QuadPart / 10000000ULL);
            ru->ru_stime.tv_usec = (long)((uli.QuadPart % 10000000ULL) / 10);
        }

        /* Get memory usage */
        memInfo.cb = sizeof(memInfo);
        if (GetProcessMemoryInfo(hProcess, (PROCESS_MEMORY_COUNTERS*)&memInfo, sizeof(memInfo))) {
            ru->ru_maxrss = (long)(memInfo.PeakWorkingSetSize / 1024);  /* KB */
            ru->ru_majflt = (long)memInfo.PageFaultCount;
        }

        /* Zero out unsupported fields */
        ru->ru_ixrss = 0;
        ru->ru_idrss = 0;
        ru->ru_isrss = 0;
        ru->ru_minflt = 0;
        ru->ru_nswap = 0;
        ru->ru_inblock = 0;
        ru->ru_oublock = 0;
        ru->ru_msgsnd = 0;
        ru->ru_msgrcv = 0;
        ru->ru_nsignals = 0;
        ru->ru_nvcsw = 0;
        ru->ru_nivcsw = 0;

        return 0;
    }

    /* RUSAGE_CHILDREN not easily supported on Windows */
    nix_platform_set_errno(EINVAL);
    return -1;

#else
    return getrusage(who, (struct rusage *)usage);
#endif
}

/*
 * ========================================================================
 * SESSION AND TERMINAL CONTROL
 * ========================================================================
 */

nix_host_pid_t
nix_platform_tcgetpgrp(int fd)
{
#if defined(NIX_HOST_WIN32)
    /*
     * Windows consoles don't have process groups like Unix.
     * Check if fd is a console and return current process PID.
     */
    HANDLE h = (HANDLE)_get_osfhandle(fd);
    if (h == INVALID_HANDLE_VALUE) {
        nix_platform_set_errno(EBADF);
        return -1;
    }

    DWORD mode;
    if (GetConsoleMode(h, &mode)) {
        /* It's a console, return our PID as the foreground group */
        return GetCurrentProcessId();
    }

    /* Not a terminal */
    nix_platform_set_errno(ENOTTY);
    return -1;

#else
    return tcgetpgrp(fd);
#endif
}

int
nix_platform_tcsetpgrp(int fd, nix_host_pid_t pgrp)
{
#if defined(NIX_HOST_WIN32)
    /*
     * Windows doesn't support setting foreground process group.
     * We could use SetConsoleCtrlHandler for some emulation,
     * but it's not equivalent to Unix tcsetpgrp.
     */
    (void)fd;
    (void)pgrp;
    nix_platform_set_errno(ENOSYS);
    return -1;

#else
    return tcsetpgrp(fd, pgrp);
#endif
}

char *
nix_platform_ttyname(int fd)
{
#if defined(NIX_HOST_WIN32)
    /*
     * Windows doesn't have TTY names like Unix (/dev/tty, /dev/pts/0).
     * Return "CON" for console, NULL otherwise.
     */
    static char tty_name[32];
    HANDLE h = (HANDLE)_get_osfhandle(fd);

    if (h == INVALID_HANDLE_VALUE) {
        nix_platform_set_errno(EBADF);
        return NULL;
    }

    DWORD mode;
    if (GetConsoleMode(h, &mode)) {
        /* Console handle */
        strcpy(tty_name, "CON");
        return tty_name;
    }

    /* Check if it's a named pipe (could be redirected) */
    DWORD type = GetFileType(h);
    if (type == FILE_TYPE_PIPE) {
        strcpy(tty_name, "PIPE");
        return tty_name;
    }

    nix_platform_set_errno(ENOTTY);
    return NULL;

#else
    return ttyname(fd);
#endif
}

int
nix_platform_ttyname_r(int fd, char *buf, size_t buflen)
{
#if defined(NIX_HOST_WIN32)
    char *name = nix_platform_ttyname(fd);
    if (!name) {
        return nix_platform_get_errno();
    }

    if (buflen < strlen(name) + 1) {
        nix_platform_set_errno(ERANGE);
        return ERANGE;
    }

    strcpy(buf, name);
    return 0;

#elif defined(HAVE_TTYNAME_R)
    return ttyname_r(fd, buf, buflen);

#else
    char *name = ttyname(fd);
    if (!name) {
        return errno;
    }

    if (buflen < strlen(name) + 1) {
        return ERANGE;
    }

    strcpy(buf, name);
    return 0;
#endif
}

int
nix_platform_isatty_ex(int fd)
{
    /*
     * Extended isatty that works better on Windows.
     * Returns 1 if terminal, 0 if not, -1 on error.
     */
#if defined(NIX_HOST_WIN32)
    HANDLE h = (HANDLE)_get_osfhandle(fd);
    if (h == INVALID_HANDLE_VALUE) {
        nix_platform_set_errno(EBADF);
        return -1;
    }

    DWORD mode;
    if (GetConsoleMode(h, &mode)) {
        return 1;  /* Is a console */
    }

    /* Not a console - check if it's a character device */
    DWORD type = GetFileType(h);
    if (type == FILE_TYPE_CHAR) {
        return 1;
    }

    return 0;  /* Not a TTY */

#else
    return isatty(fd);
#endif
}

char *
nix_platform_ctermid(char *s)
{
#if defined(NIX_HOST_WIN32)
    /*
     * Return the controlling terminal name.
     * On Windows, always "CON" for console.
     */
    static char ctermid_buf[32];
    char *buf = s ? s : ctermid_buf;

    strcpy(buf, "CON");
    return buf;

#else
    return ctermid(s);
#endif
}

int
nix_platform_vhangup(void)
{
#if defined(NIX_HOST_WIN32)
    /*
     * vhangup() revokes access to the controlling terminal.
     * Windows doesn't have an equivalent.
     * We could close and reopen console, but not the same.
     */
    nix_platform_set_errno(ENOSYS);
    return -1;

#elif defined(HAVE_VHANGUP)
    return vhangup();

#else
    nix_platform_set_errno(ENOSYS);
    return -1;
#endif
}

int
nix_platform_revoke(const char *file)
{
#if defined(NIX_HOST_WIN32)
    /*
     * revoke() revokes access to a device file.
     * Windows doesn't have this concept for devices.
     */
    (void)file;
    nix_platform_set_errno(ENOSYS);
    return -1;

#elif defined(HAVE_REVOKE)
    return revoke(file);

#else
    (void)file;
    nix_platform_set_errno(ENOSYS);
    return -1;
#endif
}

/*
 * ========================================================================
 * PLATFORM TIME OPERATIONS
 * ========================================================================
 */

int
nix_platform_gettimeofday(struct timeval *tv, void *tz)
{
#if defined(NIX_HOST_WIN32)
    FILETIME ft;
    ULARGE_INTEGER ull;

    (void)tz;  /* Timezone not supported */

    GetSystemTimeAsFileTime(&ft);

    ull.LowPart = ft.dwLowDateTime;
    ull.HighPart = ft.dwHighDateTime;

    /* Convert from 100-nanosecond intervals since 1601 to microseconds since 1970 */
    uint64_t usec = (ull.QuadPart / 10ULL) - 11644473600000000ULL;

    tv->tv_sec = (long)(usec / 1000000ULL);
    tv->tv_usec = (long)(usec % 1000000ULL);

    return 0;

#elif defined(NIX_HOST_OS2)
    DATETIME dt;
    if (DosGetDateTime(&dt) != NO_ERROR) {
        return -1;
    }

    /* Convert DATETIME to time_t (simplified) */
    struct tm tm;
    tm.tm_year = dt.year - 1900;
    tm.tm_mon = dt.month - 1;
    tm.tm_mday = dt.day;
    tm.tm_hour = dt.hours;
    tm.tm_min = dt.minutes;
    tm.tm_sec = dt.seconds;
    tm.tm_isdst = -1;

    tv->tv_sec = mktime(&tm);
    tv->tv_usec = dt.hundredths * 10000;

    return 0;

#else
    return gettimeofday(tv, tz);
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

/*
 * ========================================================================
 * ADDITIONAL FILE OPERATIONS
 * ========================================================================
 */

int
nix_platform_truncate(const char *path, nix_host_off_t length)
{
#if defined(NIX_HOST_WIN32)
    HANDLE hFile;
    LARGE_INTEGER li;
    BOOL result;

    hFile = CreateFileA(path, GENERIC_WRITE, 0, NULL, OPEN_EXISTING,
                        FILE_ATTRIBUTE_NORMAL, NULL);
    if (hFile == INVALID_HANDLE_VALUE) {
        return -1;
    }

    li.QuadPart = length;
    if (!SetFilePointerEx(hFile, li, NULL, FILE_BEGIN)) {
        CloseHandle(hFile);
        return -1;
    }

    result = SetEndOfFile(hFile);
    CloseHandle(hFile);
    return result ? 0 : -1;

#else
    return truncate(path, length);
#endif
}

int
nix_platform_ftruncate(nix_host_fd_t fd, nix_host_off_t length)
{
#if defined(NIX_HOST_WIN32)
    LARGE_INTEGER li;

    li.QuadPart = length;
    if (!SetFilePointerEx(fd, li, NULL, FILE_BEGIN)) {
        return -1;
    }

    return SetEndOfFile(fd) ? 0 : -1;

#elif defined(NIX_HOST_OS2)
    return (DosSetFileSize(fd, (ULONG)length) == NO_ERROR) ? 0 : -1;

#else
    return ftruncate(fd, length);
#endif
}

ssize_t
nix_platform_pread(nix_host_fd_t fd, void *buf, size_t count, nix_host_off_t offset)
{
#if defined(NIX_HOST_WIN32)
    OVERLAPPED ov = {0};
    DWORD bytesRead;

    ov.Offset = (DWORD)(offset & 0xFFFFFFFF);
    ov.OffsetHigh = (DWORD)(offset >> 32);

    if (!ReadFile(fd, buf, (DWORD)count, &bytesRead, &ov)) {
        return -1;
    }

    return (ssize_t)bytesRead;

#else
    return pread(fd, buf, count, offset);
#endif
}

ssize_t
nix_platform_pwrite(nix_host_fd_t fd, const void *buf, size_t count, nix_host_off_t offset)
{
#if defined(NIX_HOST_WIN32)
    OVERLAPPED ov = {0};
    DWORD bytesWritten;

    ov.Offset = (DWORD)(offset & 0xFFFFFFFF);
    ov.OffsetHigh = (DWORD)(offset >> 32);

    if (!WriteFile(fd, buf, (DWORD)count, &bytesWritten, &ov)) {
        return -1;
    }

    return (ssize_t)bytesWritten;

#else
    return pwrite(fd, buf, count, offset);
#endif
}

ssize_t
nix_platform_readv(nix_host_fd_t fd, const struct iovec *iov, int iovcnt)
{
#if defined(NIX_HOST_WIN32)
    ssize_t total = 0;
    int i;

    for (i = 0; i < iovcnt; i++) {
        ssize_t n = nix_platform_read(fd, iov[i].iov_base, iov[i].iov_len);
        if (n < 0) {
            return (total > 0) ? total : -1;
        }
        total += n;
        if ((size_t)n < iov[i].iov_len) {
            break;  /* Short read */
        }
    }

    return total;

#else
    return readv(fd, iov, iovcnt);
#endif
}

ssize_t
nix_platform_writev(nix_host_fd_t fd, const struct iovec *iov, int iovcnt)
{
#if defined(NIX_HOST_WIN32)
    ssize_t total = 0;
    int i;

    for (i = 0; i < iovcnt; i++) {
        ssize_t n = nix_platform_write(fd, iov[i].iov_base, iov[i].iov_len);
        if (n < 0) {
            return (total > 0) ? total : -1;
        }
        total += n;
        if ((size_t)n < iov[i].iov_len) {
            break;  /* Short write */
        }
    }

    return total;

#else
    return writev(fd, iov, iovcnt);
#endif
}

int
nix_platform_chmod(const char *path, nix_host_mode_t mode)
{
#if defined(NIX_HOST_WIN32)
    /* Windows has limited chmod support */
    return _chmod(path, mode);

#else
    return chmod(path, mode);
#endif
}

int
nix_platform_fchmod(nix_host_fd_t fd, nix_host_mode_t mode)
{
#if defined(NIX_HOST_WIN32)
    /* Windows doesn't support fchmod */
    (void)fd;
    (void)mode;
    errno = ENOSYS;
    return -1;

#else
    return fchmod(fd, mode);
#endif
}

int
nix_platform_chown(const char *path, nix_host_uid_t owner, nix_host_gid_t group)
{
#if defined(NIX_HOST_WIN32)
    /* Windows doesn't use UID/GID */
    (void)path;
    (void)owner;
    (void)group;
    errno = ENOSYS;
    return -1;

#else
    return chown(path, owner, group);
#endif
}

int
nix_platform_fchown(nix_host_fd_t fd, nix_host_uid_t owner, nix_host_gid_t group)
{
#if defined(NIX_HOST_WIN32)
    /* Windows doesn't use UID/GID */
    (void)fd;
    (void)owner;
    (void)group;
    errno = ENOSYS;
    return -1;

#else
    return fchown(fd, owner, group);
#endif
}

int
nix_platform_link(const char *path1, const char *path2)
{
#if defined(NIX_HOST_WIN32)
    /* CreateHardLink requires Windows 2000+ */
    if (CreateHardLinkA(path2, path1, NULL)) {
        return 0;
    }
    return -1;

#else
    return link(path1, path2);
#endif
}

int
nix_platform_symlink(const char *path1, const char *path2)
{
#if defined(NIX_HOST_WIN32)
    /* CreateSymbolicLink requires Windows Vista+ and admin privileges */
    /* For compatibility, return ENOSYS */
    (void)path1;
    (void)path2;
    errno = ENOSYS;
    return -1;

#else
    return symlink(path1, path2);
#endif
}

ssize_t
nix_platform_readlink(const char *path, char *buf, size_t bufsiz)
{
#if defined(NIX_HOST_WIN32)
    /* Windows doesn't have symlinks (pre-Vista) */
    (void)path;
    (void)buf;
    (void)bufsiz;
    errno = EINVAL;
    return -1;

#else
    return readlink(path, buf, bufsiz);
#endif
}

int
nix_platform_utime(const char *path, const struct utimbuf *times)
{
#if defined(NIX_HOST_WIN32)
    HANDLE hFile;
    FILETIME ftAccess, ftModify;
    ULARGE_INTEGER ull;

    hFile = CreateFileA(path, FILE_WRITE_ATTRIBUTES, 0, NULL,
                        OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hFile == INVALID_HANDLE_VALUE) {
        return -1;
    }

    if (times != NULL) {
        /* Convert Unix time_t to Windows FILETIME */
        ull.QuadPart = (times->actime + 11644473600ULL) * 10000000ULL;
        ftAccess.dwLowDateTime = ull.LowPart;
        ftAccess.dwHighDateTime = ull.HighPart;

        ull.QuadPart = (times->modtime + 11644473600ULL) * 10000000ULL;
        ftModify.dwLowDateTime = ull.LowPart;
        ftModify.dwHighDateTime = ull.HighPart;

        if (!SetFileTime(hFile, NULL, &ftAccess, &ftModify)) {
            CloseHandle(hFile);
            return -1;
        }
    }

    CloseHandle(hFile);
    return 0;

#else
    return utime(path, times);
#endif
}

int
nix_platform_sync(void)
{
#if defined(NIX_HOST_WIN32)
    /* Windows doesn't have a direct sync equivalent */
    /* FlushFileBuffers works on individual files */
    return 0;

#else
    sync();
    return 0;
#endif
}

int
nix_platform_fchdir(nix_host_fd_t fd)
{
#if defined(NIX_HOST_WIN32)
    /* Windows doesn't support fchdir */
    (void)fd;
    errno = ENOSYS;
    return -1;

#else
    return fchdir(fd);
#endif
}

/*
 * ========================================================================
 * I/O OPERATIONS
 * ========================================================================
 */

int
nix_platform_pipe(int pipefd[2])
{
#if defined(NIX_HOST_WIN32)
    HANDLE hRead, hWrite;
    SECURITY_ATTRIBUTES sa;

    sa.nLength = sizeof(SECURITY_ATTRIBUTES);
    sa.bInheritHandle = TRUE;
    sa.lpSecurityDescriptor = NULL;

    if (!CreatePipe(&hRead, &hWrite, &sa, 0)) {
        return -1;
    }

    pipefd[0] = (int)(intptr_t)hRead;
    pipefd[1] = (int)(intptr_t)hWrite;
    return 0;

#else
    return pipe(pipefd);
#endif
}

int
nix_platform_fcntl(nix_host_fd_t fd, int cmd, long arg)
{
#if defined(NIX_HOST_WIN32)
    /* Windows has very limited fcntl support */
    /* F_GETFL = 3, F_SETFL = 4, O_NONBLOCK = 0x4000 */

    switch (cmd) {
    case 3:  /* F_GETFL */
        /* Can't query flags on Windows, return 0 */
        return 0;

    case 4:  /* F_SETFL */
        /* Limited support - ignore most flags */
        return 0;

    default:
        errno = EINVAL;
        return -1;
    }

#else
    return fcntl(fd, cmd, arg);
#endif
}

int
nix_platform_select(int nfds, fd_set *readfds, fd_set *writefds,
                    fd_set *exceptfds, struct timeval *timeout)
{
#if defined(NIX_HOST_WIN32)
    /* Windows select() works only for sockets */
    /* For files/pipes, this is limited */
    return select(nfds, readfds, writefds, exceptfds, timeout);

#else
    return select(nfds, readfds, writefds, exceptfds, timeout);
#endif
}

int
nix_platform_isatty(nix_host_fd_t fd)
{
#if defined(NIX_HOST_WIN32)
    DWORD mode;
    return GetConsoleMode(fd, &mode) ? 1 : 0;

#else
    return isatty(fd);
#endif
}

/*
 * ========================================================================
 * ADDITIONAL PROCESS OPERATIONS
 * ========================================================================
 */

nix_host_pid_t
nix_platform_getppid(void)
{
#if defined(NIX_HOST_WIN32)
    /* Get parent process ID - requires PSAPI */
    /* For simplicity, return 0 (no parent) */
    return 0;

#else
    return getppid();
#endif
}

int
nix_platform_execve(const char *path, char *const argv[], char *const envp[])
{
#if defined(NIX_HOST_WIN32)
    /* Windows uses CreateProcess instead of exec */
    /* This is a simplified implementation */
    (void)path;
    (void)argv;
    (void)envp;
    errno = ENOSYS;
    return -1;

#else
    return execve(path, argv, envp);
#endif
}

/*
 * ========================================================================
 * HOSTNAME OPERATIONS
 * ========================================================================
 */

int
nix_platform_gethostname(char *name, size_t len)
{
#if defined(NIX_HOST_WIN32)
    DWORD size = (DWORD)len;
    return GetComputerNameA(name, &size) ? 0 : -1;

#else
    return gethostname(name, len);
#endif
}

int
nix_platform_sethostname(const char *name, size_t len)
{
#if defined(NIX_HOST_WIN32)
    /* Setting hostname requires admin privileges on Windows */
    (void)name;
    (void)len;
    errno = ENOSYS;
    return -1;

#else
    return sethostname(name, len);
#endif
}

/*
 * ========================================================================
 * ADDITIONAL TIME OPERATIONS
 * ========================================================================
 */

int
nix_platform_nanosleep(const struct timespec *req, struct timespec *rem)
{
#if defined(NIX_HOST_WIN32)
    DWORD milliseconds;

    (void)rem;  /* Windows Sleep doesn't support remaining time */

    /* Convert nanoseconds to milliseconds */
    milliseconds = (DWORD)(req->tv_sec * 1000 + req->tv_nsec / 1000000);

    Sleep(milliseconds);
    return 0;

#else
    return nanosleep(req, rem);
#endif
}

unsigned int
nix_platform_sleep(unsigned int seconds)
{
#if defined(NIX_HOST_WIN32)
    Sleep(seconds * 1000);
    return 0;

#else
    return sleep(seconds);
#endif
}
