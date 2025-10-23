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
    /* Windows doesn't have fork(), would need to use CreateProcess */
    NIX_ERROR("fork() not supported on Win32 - use CreateProcess instead");
    errno = ENOSYS;
    return -1;

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
#if defined(NIX_HOST_WIN32)
    HANDLE hProcess;

    /* Windows doesn't have signals like Unix */
    /* We can only terminate the process */
    hProcess = OpenProcess(PROCESS_TERMINATE, FALSE, pid);
    if (hProcess == NULL) {
        return -1;
    }

    if (!TerminateProcess(hProcess, 1)) {
        CloseHandle(hProcess);
        return -1;
    }

    CloseHandle(hProcess);
    return 0;

#elif defined(NIX_HOST_OS2)
    /* OS/2 kill implementation using DosKillProcess */
    if (DosKillProcess(DKP_PROCESS, pid) != NO_ERROR) {
        return -1;
    }
    return 0;

#else
    return kill(pid, sig);
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
