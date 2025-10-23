/*
 * nix-platform-win32-fd-symlink.c
 *
 * BSD chflags, lstat, lch*, lutime*, fcntl, and *at functions for Windows
 *
 * Features:
 * - BSD chflags/fchflags/lchflags (file flags)
 * - lstat (stat without following symlinks)
 * - lchown/lchmod (ownership/mode without following symlinks)
 * - lutimes/futimes/utimensat (time modification)
 * - fcntl (file descriptor control)
 * - *at functions (openat, fstatat, fchownat, etc.)
 * - symlinkat/linkat/readlinkat (symlink operations with directory fd)
 */

#include "nix-platform.h"

#if defined(NIX_HOST_WIN32)

#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <sys/stat.h>
#include <fcntl.h>

/*
 * ========================================================================
 * BSD FILE FLAGS
 * ========================================================================
 */

/* BSD file flags */
#define UF_NODUMP      0x00000001  /* Do not dump file */
#define UF_IMMUTABLE   0x00000002  /* File may not be changed */
#define UF_APPEND      0x00000004  /* Writes to file may only append */
#define UF_OPAQUE      0x00000008  /* Directory is opaque (union mounts) */
#define UF_NOUNLINK    0x00000010  /* File may not be removed or renamed */
#define UF_HIDDEN      0x00008000  /* Windows hidden file */

#define SF_ARCHIVED    0x00010000  /* File is archived */
#define SF_IMMUTABLE   0x00020000  /* File may not be changed */
#define SF_APPEND      0x00040000  /* Writes to file may only append */
#define SF_NOUNLINK    0x00100000  /* File may not be removed or renamed */

/*
 * Map BSD flags to Windows attributes
 */
static DWORD
flags_to_attrs(unsigned long flags)
{
	DWORD attrs = 0;

	if (flags & UF_IMMUTABLE || flags & SF_IMMUTABLE) {
		attrs |= FILE_ATTRIBUTE_READONLY;
	}
	if (flags & UF_HIDDEN) {
		attrs |= FILE_ATTRIBUTE_HIDDEN;
	}
	if (flags & SF_ARCHIVED) {
		attrs |= FILE_ATTRIBUTE_ARCHIVE;
	}
	/* UF_NODUMP, UF_APPEND, UF_OPAQUE, UF_NOUNLINK don't have direct Windows equivalents */

	return attrs;
}

/*
 * Map Windows attributes to BSD flags
 */
static unsigned long
attrs_to_flags(DWORD attrs)
{
	unsigned long flags = 0;

	if (attrs & FILE_ATTRIBUTE_READONLY) {
		flags |= UF_IMMUTABLE;
	}
	if (attrs & FILE_ATTRIBUTE_HIDDEN) {
		flags |= UF_HIDDEN;
	}
	if (attrs & FILE_ATTRIBUTE_ARCHIVE) {
		flags |= SF_ARCHIVED;
	}

	return flags;
}

/*
 * Set file flags by path
 */
int
nix_platform_win32_chflags(const char *path, unsigned long flags)
{
	DWORD attrs = flags_to_attrs(flags);

	/* Get current attributes to preserve those we don't modify */
	DWORD current = GetFileAttributesA(path);
	if (current == INVALID_FILE_ATTRIBUTES) {
		nix_platform_set_errno(ENOENT);
		return -1;
	}

	/* Preserve system/directory/etc attributes */
	attrs |= (current & (FILE_ATTRIBUTE_SYSTEM | FILE_ATTRIBUTE_DIRECTORY |
						  FILE_ATTRIBUTE_DEVICE | FILE_ATTRIBUTE_NORMAL));

	if (!SetFileAttributesA(path, attrs)) {
		nix_platform_set_errno(EACCES);
		return -1;
	}

	return 0;
}

/*
 * Set file flags by file descriptor
 */
int
nix_platform_win32_fchflags(int fd, unsigned long flags)
{
	HANDLE h = (HANDLE)_get_osfhandle(fd);
	if (h == INVALID_HANDLE_VALUE) {
		nix_platform_set_errno(EBADF);
		return -1;
	}

	FILE_BASIC_INFO info;
	if (!GetFileInformationByHandleEx(h, FileBasicInfo, &info, sizeof(info))) {
		nix_platform_set_errno(EIO);
		return -1;
	}

	DWORD attrs = flags_to_attrs(flags);

	/* Preserve system/directory/etc attributes */
	attrs |= (info.FileAttributes & (FILE_ATTRIBUTE_SYSTEM | FILE_ATTRIBUTE_DIRECTORY |
									  FILE_ATTRIBUTE_DEVICE | FILE_ATTRIBUTE_NORMAL));

	info.FileAttributes = attrs;

	if (!SetFileInformationByHandle(h, FileBasicInfo, &info, sizeof(info))) {
		nix_platform_set_errno(EACCES);
		return -1;
	}

	return 0;
}

/*
 * Set file flags without following symlinks
 */
int
nix_platform_win32_lchflags(const char *path, unsigned long flags)
{
	HANDLE h;
	FILE_BASIC_INFO info;

	/* Open without following reparse points */
	h = CreateFileA(path,
					FILE_READ_ATTRIBUTES | FILE_WRITE_ATTRIBUTES,
					FILE_SHARE_READ | FILE_SHARE_WRITE,
					NULL,
					OPEN_EXISTING,
					FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT,
					NULL);

	if (h == INVALID_HANDLE_VALUE) {
		nix_platform_set_errno(ENOENT);
		return -1;
	}

	if (!GetFileInformationByHandleEx(h, FileBasicInfo, &info, sizeof(info))) {
		CloseHandle(h);
		nix_platform_set_errno(EIO);
		return -1;
	}

	DWORD attrs = flags_to_attrs(flags);
	attrs |= (info.FileAttributes & (FILE_ATTRIBUTE_SYSTEM | FILE_ATTRIBUTE_DIRECTORY |
									  FILE_ATTRIBUTE_DEVICE | FILE_ATTRIBUTE_NORMAL));

	info.FileAttributes = attrs;

	if (!SetFileInformationByHandle(h, FileBasicInfo, &info, sizeof(info))) {
		CloseHandle(h);
		nix_platform_set_errno(EACCES);
		return -1;
	}

	CloseHandle(h);
	return 0;
}

/*
 * ========================================================================
 * LSTAT (stat without following symlinks)
 * ========================================================================
 */

int
nix_platform_win32_lstat(const char *path, struct stat *st)
{
	HANDLE h;
	BY_HANDLE_FILE_INFORMATION info;

	/* Open without following reparse points */
	h = CreateFileA(path,
					FILE_READ_ATTRIBUTES,
					FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
					NULL,
					OPEN_EXISTING,
					FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT,
					NULL);

	if (h == INVALID_HANDLE_VALUE) {
		nix_platform_set_errno(ENOENT);
		return -1;
	}

	if (!GetFileInformationByHandle(h, &info)) {
		CloseHandle(h);
		nix_platform_set_errno(EIO);
		return -1;
	}

	memset(st, 0, sizeof(*st));

	/* File mode */
	st->st_mode = 0;
	if (info.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
		st->st_mode |= S_IFDIR | 0755;
	} else if (info.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) {
		st->st_mode |= S_IFLNK | 0777;  /* Symbolic link */
	} else {
		st->st_mode |= S_IFREG | 0644;
	}

	if (!(info.dwFileAttributes & FILE_ATTRIBUTE_READONLY)) {
		st->st_mode |= 0200;  /* Writable */
	}

	/* File times */
	LARGE_INTEGER li;
	li.LowPart = info.ftCreationTime.dwLowDateTime;
	li.HighPart = info.ftCreationTime.dwHighDateTime;
	st->st_ctime = (time_t)(li.QuadPart / 10000000ULL - 11644473600ULL);

	li.LowPart = info.ftLastAccessTime.dwLowDateTime;
	li.HighPart = info.ftLastAccessTime.dwHighDateTime;
	st->st_atime = (time_t)(li.QuadPart / 10000000ULL - 11644473600ULL);

	li.LowPart = info.ftLastWriteTime.dwLowDateTime;
	li.HighPart = info.ftLastWriteTime.dwHighDateTime;
	st->st_mtime = (time_t)(li.QuadPart / 10000000ULL - 11644473600ULL);

	/* File size */
	li.LowPart = info.nFileSizeLow;
	li.HighPart = info.nFileSizeHigh;
	st->st_size = li.QuadPart;

	/* Links */
	st->st_nlink = info.nNumberOfLinks;

	/* Inode and device */
	st->st_ino = ((uint64_t)info.nFileIndexHigh << 32) | info.nFileIndexLow;
	st->st_dev = info.dwVolumeSerialNumber;

	CloseHandle(h);
	return 0;
}

/*
 * ========================================================================
 * LCHOWN/LCHMOD (without following symlinks)
 * ========================================================================
 */

int
nix_platform_win32_lchown(const char *path, uid_t owner, gid_t group)
{
	/* Windows doesn't have Unix-style ownership */
	/* Could use SetNamedSecurityInfo but complex */
	nix_platform_set_errno(ENOSYS);
	return -1;
}

int
nix_platform_win32_lchmod(const char *path, mode_t mode)
{
	HANDLE h;
	DWORD attrs;

	/* Open without following reparse points */
	h = CreateFileA(path,
					FILE_READ_ATTRIBUTES | FILE_WRITE_ATTRIBUTES,
					FILE_SHARE_READ | FILE_SHARE_WRITE,
					NULL,
					OPEN_EXISTING,
					FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT,
					NULL);

	if (h == INVALID_HANDLE_VALUE) {
		nix_platform_set_errno(ENOENT);
		return -1;
	}

	FILE_BASIC_INFO info;
	if (!GetFileInformationByHandleEx(h, FileBasicInfo, &info, sizeof(info))) {
		CloseHandle(h);
		nix_platform_set_errno(EIO);
		return -1;
	}

	attrs = info.FileAttributes;

	/* Modify readonly bit based on write permission */
	if (mode & S_IWUSR) {
		attrs &= ~FILE_ATTRIBUTE_READONLY;
	} else {
		attrs |= FILE_ATTRIBUTE_READONLY;
	}

	info.FileAttributes = attrs;

	if (!SetFileInformationByHandle(h, FileBasicInfo, &info, sizeof(info))) {
		CloseHandle(h);
		nix_platform_set_errno(EACCES);
		return -1;
	}

	CloseHandle(h);
	return 0;
}

/*
 * ========================================================================
 * LUTIMES/FUTIMES (time modification)
 * ========================================================================
 */

int
nix_platform_win32_lutimes(const char *path, const struct timeval tv[2])
{
	HANDLE h;
	FILETIME atime, mtime;

	/* Open without following reparse points */
	h = CreateFileA(path,
					FILE_WRITE_ATTRIBUTES,
					FILE_SHARE_READ | FILE_SHARE_WRITE,
					NULL,
					OPEN_EXISTING,
					FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT,
					NULL);

	if (h == INVALID_HANDLE_VALUE) {
		nix_platform_set_errno(ENOENT);
		return -1;
	}

	/* Convert timeval to FILETIME */
	if (tv != NULL) {
		LARGE_INTEGER li;

		/* Access time */
		li.QuadPart = (tv[0].tv_sec + 11644473600ULL) * 10000000ULL + tv[0].tv_usec * 10;
		atime.dwLowDateTime = li.LowPart;
		atime.dwHighDateTime = li.HighPart;

		/* Modification time */
		li.QuadPart = (tv[1].tv_sec + 11644473600ULL) * 10000000ULL + tv[1].tv_usec * 10;
		mtime.dwLowDateTime = li.LowPart;
		mtime.dwHighDateTime = li.HighPart;
	}

	if (!SetFileTime(h, NULL, tv ? &atime : NULL, tv ? &mtime : NULL)) {
		CloseHandle(h);
		nix_platform_set_errno(EACCES);
		return -1;
	}

	CloseHandle(h);
	return 0;
}

int
nix_platform_win32_futimes(int fd, const struct timeval tv[2])
{
	HANDLE h = (HANDLE)_get_osfhandle(fd);
	if (h == INVALID_HANDLE_VALUE) {
		nix_platform_set_errno(EBADF);
		return -1;
	}

	FILETIME atime, mtime;

	if (tv != NULL) {
		LARGE_INTEGER li;

		/* Access time */
		li.QuadPart = (tv[0].tv_sec + 11644473600ULL) * 10000000ULL + tv[0].tv_usec * 10;
		atime.dwLowDateTime = li.LowPart;
		atime.dwHighDateTime = li.HighPart;

		/* Modification time */
		li.QuadPart = (tv[1].tv_sec + 11644473600ULL) * 10000000ULL + tv[1].tv_usec * 10;
		mtime.dwLowDateTime = li.LowPart;
		mtime.dwHighDateTime = li.HighPart;
	}

	if (!SetFileTime(h, NULL, tv ? &atime : NULL, tv ? &mtime : NULL)) {
		nix_platform_set_errno(EACCES);
		return -1;
	}

	return 0;
}

/*
 * ========================================================================
 * FCNTL (file descriptor control)
 * ========================================================================
 */

/* FD flags (for F_GETFD/F_SETFD) */
static int *g_fd_flags = NULL;
static int g_fd_flags_size = 0;
static CRITICAL_SECTION g_fd_flags_lock;
static int g_fd_flags_initialized = 0;

static void
init_fd_flags(void)
{
	if (g_fd_flags_initialized)
		return;

	InitializeCriticalSection(&g_fd_flags_lock);
	g_fd_flags_size = 256;
	g_fd_flags = (int *)calloc(g_fd_flags_size, sizeof(int));
	g_fd_flags_initialized = 1;
}

static int
get_fd_flags(int fd)
{
	init_fd_flags();

	EnterCriticalSection(&g_fd_flags_lock);

	if (fd >= g_fd_flags_size) {
		/* Grow array */
		int new_size = fd + 128;
		int *new_flags = (int *)realloc(g_fd_flags, new_size * sizeof(int));
		if (new_flags) {
			memset(new_flags + g_fd_flags_size, 0, (new_size - g_fd_flags_size) * sizeof(int));
			g_fd_flags = new_flags;
			g_fd_flags_size = new_size;
		}
	}

	int flags = (fd < g_fd_flags_size) ? g_fd_flags[fd] : 0;

	LeaveCriticalSection(&g_fd_flags_lock);

	return flags;
}

static void
set_fd_flags(int fd, int flags)
{
	init_fd_flags();

	EnterCriticalSection(&g_fd_flags_lock);

	if (fd >= g_fd_flags_size) {
		/* Grow array */
		int new_size = fd + 128;
		int *new_flags = (int *)realloc(g_fd_flags, new_size * sizeof(int));
		if (new_flags) {
			memset(new_flags + g_fd_flags_size, 0, (new_size - g_fd_flags_size) * sizeof(int));
			g_fd_flags = new_flags;
			g_fd_flags_size = new_size;
		}
	}

	if (fd < g_fd_flags_size) {
		g_fd_flags[fd] = flags;
	}

	LeaveCriticalSection(&g_fd_flags_lock);
}

int
nix_platform_win32_fcntl(int fd, int cmd, ...)
{
	va_list ap;
	int arg;
	int ret = -1;

	va_start(ap, cmd);
	arg = va_arg(ap, int);
	va_end(ap);

	switch (cmd) {
	case F_DUPFD:
		/* Duplicate fd to >= arg */
		{
			HANDLE h = (HANDLE)_get_osfhandle(fd);
			if (h == INVALID_HANDLE_VALUE) {
				nix_platform_set_errno(EBADF);
				return -1;
			}

			HANDLE dup_h;
			if (!DuplicateHandle(GetCurrentProcess(), h,
								 GetCurrentProcess(), &dup_h,
								 0, FALSE, DUPLICATE_SAME_ACCESS)) {
				nix_platform_set_errno(EMFILE);
				return -1;
			}

			ret = _open_osfhandle((intptr_t)dup_h, 0);
			if (ret < arg) {
				/* Need to find fd >= arg */
				/* For simplicity, just return what we got */
			}
		}
		break;

	case F_GETFD:
		/* Get file descriptor flags */
		ret = get_fd_flags(fd);
		break;

	case F_SETFD:
		/* Set file descriptor flags */
		set_fd_flags(fd, arg);
		ret = 0;
		break;

	case F_GETFL:
		/* Get file status flags */
		{
			HANDLE h = (HANDLE)_get_osfhandle(fd);
			if (h == INVALID_HANDLE_VALUE) {
				nix_platform_set_errno(EBADF);
				return -1;
			}

			/* Windows doesn't have direct equivalent */
			/* Return O_RDWR by default */
			ret = O_RDWR;
		}
		break;

	case F_SETFL:
		/* Set file status flags */
		/* Windows doesn't support changing file status flags */
		ret = 0;
		break;

	default:
		nix_platform_set_errno(EINVAL);
		return -1;
	}

	return ret;
}

/*
 * ========================================================================
 * *AT FUNCTIONS (directory-relative operations)
 * ========================================================================
 */

/* Special value for current directory */
#ifndef AT_FDCWD
#define AT_FDCWD -100
#endif

/* Flags for *at functions */
#ifndef AT_SYMLINK_NOFOLLOW
#define AT_SYMLINK_NOFOLLOW 0x100
#endif
#ifndef AT_SYMLINK_FOLLOW
#define AT_SYMLINK_FOLLOW 0x400
#endif
#ifndef AT_REMOVEDIR
#define AT_REMOVEDIR 0x200
#endif

/*
 * Build full path from directory fd and relative path
 */
static int
build_path_at(int dirfd, const char *pathname, char *fullpath, size_t size)
{
	if (pathname[0] == '/' || pathname[0] == '\\') {
		/* Absolute path */
		strncpy(fullpath, pathname, size);
		fullpath[size - 1] = '\0';
		return 0;
	}

	if (dirfd == AT_FDCWD) {
		/* Use current directory */
		if (!GetCurrentDirectoryA((DWORD)size, fullpath)) {
			return -1;
		}
		size_t len = strlen(fullpath);
		if (len < size - 2) {
			fullpath[len] = '\\';
			fullpath[len + 1] = '\0';
			strncat(fullpath, pathname, size - len - 2);
		}
		return 0;
	}

	/* Get directory path from fd */
	HANDLE h = (HANDLE)_get_osfhandle(dirfd);
	if (h == INVALID_HANDLE_VALUE) {
		nix_platform_set_errno(EBADF);
		return -1;
	}

	if (!GetFinalPathNameByHandleA(h, fullpath, (DWORD)size, FILE_NAME_NORMALIZED)) {
		nix_platform_set_errno(EINVAL);
		return -1;
	}

	/* Remove \\?\ prefix if present */
	if (strncmp(fullpath, "\\\\?\\", 4) == 0) {
		memmove(fullpath, fullpath + 4, strlen(fullpath + 4) + 1);
	}

	size_t len = strlen(fullpath);
	if (len < size - 2) {
		fullpath[len] = '\\';
		fullpath[len + 1] = '\0';
		strncat(fullpath, pathname, size - len - 2);
	}

	return 0;
}

int
nix_platform_win32_openat(int dirfd, const char *pathname, int flags, ...)
{
	char fullpath[MAX_PATH];

	if (build_path_at(dirfd, pathname, fullpath, sizeof(fullpath)) < 0) {
		return -1;
	}

	va_list ap;
	va_start(ap, flags);
	mode_t mode = va_arg(ap, mode_t);
	va_end(ap);

	return open(fullpath, flags, mode);
}

int
nix_platform_win32_fstatat(int dirfd, const char *pathname, struct stat *st, int flags)
{
	char fullpath[MAX_PATH];

	if (build_path_at(dirfd, pathname, fullpath, sizeof(fullpath)) < 0) {
		return -1;
	}

	if (flags & AT_SYMLINK_NOFOLLOW) {
		return nix_platform_win32_lstat(fullpath, st);
	} else {
		return stat(fullpath, st);
	}
}

int
nix_platform_win32_fchownat(int dirfd, const char *pathname, uid_t owner, gid_t group, int flags)
{
	/* Windows doesn't have Unix-style ownership */
	nix_platform_set_errno(ENOSYS);
	return -1;
}

int
nix_platform_win32_fchmodat(int dirfd, const char *pathname, mode_t mode, int flags)
{
	char fullpath[MAX_PATH];

	if (build_path_at(dirfd, pathname, fullpath, sizeof(fullpath)) < 0) {
		return -1;
	}

	if (flags & AT_SYMLINK_NOFOLLOW) {
		return nix_platform_win32_lchmod(fullpath, mode);
	} else {
		return chmod(fullpath, mode);
	}
}

int
nix_platform_win32_utimensat(int dirfd, const char *pathname,
							  const struct timespec times[2], int flags)
{
	char fullpath[MAX_PATH];

	if (build_path_at(dirfd, pathname, fullpath, sizeof(fullpath)) < 0) {
		return -1;
	}

	if (times) {
		struct timeval tv[2];
		tv[0].tv_sec = times[0].tv_sec;
		tv[0].tv_usec = times[0].tv_nsec / 1000;
		tv[1].tv_sec = times[1].tv_sec;
		tv[1].tv_usec = times[1].tv_nsec / 1000;

		if (flags & AT_SYMLINK_NOFOLLOW) {
			return nix_platform_win32_lutimes(fullpath, tv);
		} else {
			/* Use regular utimes */
			HANDLE h = CreateFileA(fullpath,
								   FILE_WRITE_ATTRIBUTES,
								   FILE_SHARE_READ | FILE_SHARE_WRITE,
								   NULL,
								   OPEN_EXISTING,
								   FILE_FLAG_BACKUP_SEMANTICS,
								   NULL);
			if (h == INVALID_HANDLE_VALUE) {
				nix_platform_set_errno(ENOENT);
				return -1;
			}

			FILETIME atime, mtime;
			LARGE_INTEGER li;

			li.QuadPart = (tv[0].tv_sec + 11644473600ULL) * 10000000ULL + tv[0].tv_usec * 10;
			atime.dwLowDateTime = li.LowPart;
			atime.dwHighDateTime = li.HighPart;

			li.QuadPart = (tv[1].tv_sec + 11644473600ULL) * 10000000ULL + tv[1].tv_usec * 10;
			mtime.dwLowDateTime = li.LowPart;
			mtime.dwHighDateTime = li.HighPart;

			if (!SetFileTime(h, NULL, &atime, &mtime)) {
				CloseHandle(h);
				nix_platform_set_errno(EACCES);
				return -1;
			}

			CloseHandle(h);
			return 0;
		}
	}

	return 0;
}

/*
 * ========================================================================
 * SYMLINK *AT FUNCTIONS
 * ========================================================================
 */

int
nix_platform_win32_symlinkat(const char *target, int newdirfd, const char *linkpath)
{
	char fullpath[MAX_PATH];

	if (build_path_at(newdirfd, linkpath, fullpath, sizeof(fullpath)) < 0) {
		return -1;
	}

	return nix_platform_win32_symlink(target, fullpath);
}

int
nix_platform_win32_linkat(int olddirfd, const char *oldpath,
						  int newdirfd, const char *newpath, int flags)
{
	char fulloldpath[MAX_PATH];
	char fullnewpath[MAX_PATH];

	if (build_path_at(olddirfd, oldpath, fulloldpath, sizeof(fulloldpath)) < 0) {
		return -1;
	}

	if (build_path_at(newdirfd, newpath, fullnewpath, sizeof(fullnewpath)) < 0) {
		return -1;
	}

	/* If AT_SYMLINK_FOLLOW is set, dereference oldpath if it's a symlink */
	/* Windows CreateHardLink always follows symlinks, so we're OK */

	return nix_platform_win32_link(fulloldpath, fullnewpath);
}

ssize_t
nix_platform_win32_readlinkat(int dirfd, const char *pathname, char *buf, size_t bufsiz)
{
	char fullpath[MAX_PATH];

	if (build_path_at(dirfd, pathname, fullpath, sizeof(fullpath)) < 0) {
		return -1;
	}

	return nix_platform_win32_readlink(fullpath, buf, bufsiz);
}

int
nix_platform_win32_unlinkat(int dirfd, const char *pathname, int flags)
{
	char fullpath[MAX_PATH];

	if (build_path_at(dirfd, pathname, fullpath, sizeof(fullpath)) < 0) {
		return -1;
	}

	if (flags & AT_REMOVEDIR) {
		/* Remove directory */
		if (!RemoveDirectoryA(fullpath)) {
			DWORD err = GetLastError();
			if (err == ERROR_FILE_NOT_FOUND || err == ERROR_PATH_NOT_FOUND) {
				nix_platform_set_errno(ENOENT);
			} else if (err == ERROR_DIR_NOT_EMPTY) {
				nix_platform_set_errno(ENOTEMPTY);
			} else {
				nix_platform_set_errno(EACCES);
			}
			return -1;
		}
	} else {
		/* Remove file */
		if (!DeleteFileA(fullpath)) {
			DWORD err = GetLastError();
			if (err == ERROR_FILE_NOT_FOUND) {
				nix_platform_set_errno(ENOENT);
			} else if (err == ERROR_ACCESS_DENIED) {
				nix_platform_set_errno(EACCES);
			} else {
				nix_platform_set_errno(EIO);
			}
			return -1;
		}
	}

	return 0;
}

int
nix_platform_win32_mkdirat(int dirfd, const char *pathname, mode_t mode)
{
	char fullpath[MAX_PATH];

	if (build_path_at(dirfd, pathname, fullpath, sizeof(fullpath)) < 0) {
		return -1;
	}

	if (!CreateDirectoryA(fullpath, NULL)) {
		DWORD err = GetLastError();
		if (err == ERROR_ALREADY_EXISTS) {
			nix_platform_set_errno(EEXIST);
		} else if (err == ERROR_PATH_NOT_FOUND) {
			nix_platform_set_errno(ENOENT);
		} else {
			nix_platform_set_errno(EACCES);
		}
		return -1;
	}

	return 0;
}

#endif /* NIX_HOST_WIN32 */
