/*
 * nix-platform-win32-links-xattr.c
 *
 * Symbolic links, hard links, and extended attributes for Windows
 *
 * Features:
 * - Symbolic links via CreateSymbolicLink (Vista+) and junction points (NT+)
 * - Hard links via CreateHardLink (2000+)
 * - Extended attributes via Alternate Data Streams (NT 3.1+)
 * - Offset-based xattr read/write for large attributes (macOS compatibility)
 * - macOS resource fork emulation (com.apple.ResourceFork)
 */

#include "nix-platform.h"

#if defined(NIX_HOST_WIN32)

#include <windows.h>
#include <winioctl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

/*
 * ========================================================================
 * SYMBOLIC LINKS
 * ========================================================================
 */

/* Reparse point structure for junction points */
typedef struct {
	DWORD ReparseTag;
	WORD  ReparseDataLength;
	WORD  Reserved;
	WORD  SubstituteNameOffset;
	WORD  SubstituteNameLength;
	WORD  PrintNameOffset;
	WORD  PrintNameLength;
	WCHAR PathBuffer[1];
} REPARSE_MOUNTPOINT_DATA_BUFFER;

#define REPARSE_MOUNTPOINT_HEADER_SIZE 8

/* Symbolic link flags */
#ifndef SYMBOLIC_LINK_FLAG_DIRECTORY
#define SYMBOLIC_LINK_FLAG_DIRECTORY 0x1
#endif

#ifndef SYMBOLIC_LINK_FLAG_ALLOW_UNPRIVILEGED_CREATE
#define SYMBOLIC_LINK_FLAG_ALLOW_UNPRIVILEGED_CREATE 0x2
#endif

/* Function pointer for CreateSymbolicLink (Vista+) */
typedef BOOLEAN (WINAPI *CreateSymbolicLinkA_t)(LPCSTR, LPCSTR, DWORD);
static CreateSymbolicLinkA_t pCreateSymbolicLinkA = NULL;
static int symlink_initialized = 0;

static void
init_symlink(void)
{
	if (symlink_initialized)
		return;

	HMODULE kernel32 = GetModuleHandleA("kernel32.dll");
	if (kernel32) {
		pCreateSymbolicLinkA = (CreateSymbolicLinkA_t)GetProcAddress(kernel32, "CreateSymbolicLinkA");
	}

	symlink_initialized = 1;
}

/*
 * Convert path to wide string
 */
static WCHAR *
path_to_wide(const char *path)
{
	int len = MultiByteToWideChar(CP_UTF8, 0, path, -1, NULL, 0);
	if (len == 0)
		return NULL;

	WCHAR *wide = (WCHAR *)malloc(len * sizeof(WCHAR));
	if (wide == NULL)
		return NULL;

	MultiByteToWideChar(CP_UTF8, 0, path, -1, wide, len);
	return wide;
}

/*
 * Convert wide string to path
 */
static char *
wide_to_path(const WCHAR *wide)
{
	int len = WideCharToMultiByte(CP_UTF8, 0, wide, -1, NULL, 0, NULL, NULL);
	if (len == 0)
		return NULL;

	char *path = (char *)malloc(len);
	if (path == NULL)
		return NULL;

	WideCharToMultiByte(CP_UTF8, 0, wide, -1, path, len, NULL, NULL);
	return path;
}

/*
 * Create junction point (works on NT+)
 */
static int
create_junction(const char *target, const char *linkpath)
{
	HANDLE hDir;
	WCHAR *target_wide = NULL;
	WCHAR *link_wide = NULL;
	REPARSE_MOUNTPOINT_DATA_BUFFER *reparse = NULL;
	DWORD bytes_returned;
	int ret = -1;
	size_t target_len;

	/* Convert target to full path */
	char full_target[MAX_PATH];
	if (!GetFullPathNameA(target, sizeof(full_target), full_target, NULL)) {
		nix_platform_set_errno(EINVAL);
		return -1;
	}

	/* Convert paths to wide strings */
	target_wide = path_to_wide(full_target);
	link_wide = path_to_wide(linkpath);

	if (!target_wide || !link_wide) {
		nix_platform_set_errno(EINVAL);
		goto cleanup;
	}

	/* Create directory for junction */
	if (!CreateDirectoryW(link_wide, NULL)) {
		DWORD err = GetLastError();
		if (err == ERROR_ALREADY_EXISTS) {
			nix_platform_set_errno(EEXIST);
		} else {
			nix_platform_set_errno(EIO);
		}
		goto cleanup;
	}

	/* Open directory */
	hDir = CreateFileW(link_wide,
					   GENERIC_WRITE,
					   0,
					   NULL,
					   OPEN_EXISTING,
					   FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT,
					   NULL);

	if (hDir == INVALID_HANDLE_VALUE) {
		RemoveDirectoryW(link_wide);
		nix_platform_set_errno(EIO);
		goto cleanup;
	}

	/* Prepare reparse data */
	target_len = wcslen(target_wide);
	size_t reparse_size = sizeof(REPARSE_MOUNTPOINT_DATA_BUFFER) + (target_len + 1) * 2 * sizeof(WCHAR);
	reparse = (REPARSE_MOUNTPOINT_DATA_BUFFER *)malloc(reparse_size);

	if (!reparse) {
		CloseHandle(hDir);
		RemoveDirectoryW(link_wide);
		nix_platform_set_errno(ENOMEM);
		goto cleanup;
	}

	memset(reparse, 0, reparse_size);

	/* Build substitute name: \??\C:\path */
	WCHAR substitute[MAX_PATH + 10];
	swprintf(substitute, MAX_PATH + 10, L"\\??\\%s", target_wide);

	size_t substitute_len = wcslen(substitute);

	reparse->ReparseTag = IO_REPARSE_TAG_MOUNT_POINT;
	reparse->SubstituteNameOffset = 0;
	reparse->SubstituteNameLength = (WORD)(substitute_len * sizeof(WCHAR));
	reparse->PrintNameOffset = (WORD)((substitute_len + 1) * sizeof(WCHAR));
	reparse->PrintNameLength = (WORD)(target_len * sizeof(WCHAR));
	reparse->ReparseDataLength = (WORD)(reparse->SubstituteNameOffset + reparse->SubstituteNameLength +
										reparse->PrintNameOffset + reparse->PrintNameLength + sizeof(WCHAR) * 2);

	wcscpy(reparse->PathBuffer, substitute);
	wcscpy(reparse->PathBuffer + substitute_len + 1, target_wide);

	/* Set reparse point */
	if (!DeviceIoControl(hDir,
						 FSCTL_SET_REPARSE_POINT,
						 reparse,
						 reparse->ReparseDataLength + REPARSE_MOUNTPOINT_HEADER_SIZE,
						 NULL,
						 0,
						 &bytes_returned,
						 NULL)) {
		CloseHandle(hDir);
		RemoveDirectoryW(link_wide);
		nix_platform_set_errno(EIO);
		goto cleanup;
	}

	CloseHandle(hDir);
	ret = 0;

cleanup:
	if (target_wide) free(target_wide);
	if (link_wide) free(link_wide);
	if (reparse) free(reparse);

	return ret;
}

/*
 * Create symbolic link
 */
int
nix_platform_win32_symlink(const char *target, const char *linkpath)
{
	DWORD attrs;
	DWORD flags = 0;

	init_symlink();

	/* Check if target is a directory */
	attrs = GetFileAttributesA(target);
	if (attrs != INVALID_FILE_ATTRIBUTES && (attrs & FILE_ATTRIBUTE_DIRECTORY)) {
		flags |= SYMBOLIC_LINK_FLAG_DIRECTORY;
	}

	/* Try CreateSymbolicLink first (Vista+) */
	if (pCreateSymbolicLinkA != NULL) {
		/* Try with unprivileged flag (Windows 10 1703+) */
		if (pCreateSymbolicLinkA(linkpath, target, flags | SYMBOLIC_LINK_FLAG_ALLOW_UNPRIVILEGED_CREATE)) {
			return 0;
		}

		/* Try without unprivileged flag */
		if (pCreateSymbolicLinkA(linkpath, target, flags)) {
			return 0;
		}

		/* If failed due to privilege, try junction for directories */
		DWORD err = GetLastError();
		if (err == ERROR_PRIVILEGE_NOT_HELD && (flags & SYMBOLIC_LINK_FLAG_DIRECTORY)) {
			/* Fall through to junction creation */
		} else {
			if (err == ERROR_ALREADY_EXISTS) {
				nix_platform_set_errno(EEXIST);
			} else if (err == ERROR_PRIVILEGE_NOT_HELD) {
				nix_platform_set_errno(EPERM);
			} else {
				nix_platform_set_errno(EIO);
			}
			return -1;
		}
	}

	/* Fall back to junction for directories (NT+) */
	if (flags & SYMBOLIC_LINK_FLAG_DIRECTORY) {
		return create_junction(target, linkpath);
	}

	/* Can't create file symlink without CreateSymbolicLink */
	nix_platform_set_errno(EOPNOTSUPP);
	return -1;
}

/*
 * Read symbolic link target
 */
ssize_t
nix_platform_win32_readlink(const char *path, char *buf, size_t bufsiz)
{
	HANDLE hFile;
	WCHAR *path_wide = NULL;
	BYTE reparse_buffer[MAXIMUM_REPARSE_DATA_BUFFER_SIZE];
	REPARSE_DATA_BUFFER *reparse = (REPARSE_DATA_BUFFER *)reparse_buffer;
	DWORD bytes_returned;
	ssize_t ret = -1;

	path_wide = path_to_wide(path);
	if (!path_wide) {
		nix_platform_set_errno(EINVAL);
		return -1;
	}

	/* Open the reparse point */
	hFile = CreateFileW(path_wide,
						0,
						FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
						NULL,
						OPEN_EXISTING,
						FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT,
						NULL);

	if (hFile == INVALID_HANDLE_VALUE) {
		free(path_wide);
		nix_platform_set_errno(ENOENT);
		return -1;
	}

	/* Get reparse point data */
	if (!DeviceIoControl(hFile,
						 FSCTL_GET_REPARSE_POINT,
						 NULL,
						 0,
						 reparse_buffer,
						 sizeof(reparse_buffer),
						 &bytes_returned,
						 NULL)) {
		CloseHandle(hFile);
		free(path_wide);
		nix_platform_set_errno(EINVAL);
		return -1;
	}

	CloseHandle(hFile);

	/* Extract target path */
	WCHAR *target = NULL;
	size_t target_len = 0;

	if (reparse->ReparseTag == IO_REPARSE_TAG_SYMLINK) {
		/* Symbolic link */
		target = (WCHAR *)((BYTE *)reparse->SymbolicLinkReparseBuffer.PathBuffer +
						   reparse->SymbolicLinkReparseBuffer.PrintNameOffset);
		target_len = reparse->SymbolicLinkReparseBuffer.PrintNameLength / sizeof(WCHAR);
	} else if (reparse->ReparseTag == IO_REPARSE_TAG_MOUNT_POINT) {
		/* Junction point */
		REPARSE_MOUNTPOINT_DATA_BUFFER *mp = (REPARSE_MOUNTPOINT_DATA_BUFFER *)reparse;
		target = mp->PathBuffer + mp->PrintNameOffset / sizeof(WCHAR);
		target_len = mp->PrintNameLength / sizeof(WCHAR);
	} else {
		free(path_wide);
		nix_platform_set_errno(EINVAL);
		return -1;
	}

	/* Convert to UTF-8 */
	WCHAR target_copy[MAX_PATH];
	if (target_len >= MAX_PATH) {
		target_len = MAX_PATH - 1;
	}
	wcsncpy(target_copy, target, target_len);
	target_copy[target_len] = L'\0';

	char *target_utf8 = wide_to_path(target_copy);
	if (!target_utf8) {
		free(path_wide);
		nix_platform_set_errno(EINVAL);
		return -1;
	}

	/* Copy to buffer */
	size_t len = strlen(target_utf8);
	if (len > bufsiz - 1) {
		len = bufsiz - 1;
	}
	memcpy(buf, target_utf8, len);
	buf[len] = '\0';
	ret = len;

	free(target_utf8);
	free(path_wide);

	return ret;
}

/*
 * ========================================================================
 * HARD LINKS
 * ========================================================================
 */

/* Function pointer for CreateHardLink (2000+) */
typedef BOOL (WINAPI *CreateHardLinkA_t)(LPCSTR, LPCSTR, LPSECURITY_ATTRIBUTES);
static CreateHardLinkA_t pCreateHardLinkA = NULL;
static int hardlink_initialized = 0;

static void
init_hardlink(void)
{
	if (hardlink_initialized)
		return;

	HMODULE kernel32 = GetModuleHandleA("kernel32.dll");
	if (kernel32) {
		pCreateHardLinkA = (CreateHardLinkA_t)GetProcAddress(kernel32, "CreateHardLinkA");
	}

	hardlink_initialized = 1;
}

/*
 * Create hard link
 */
int
nix_platform_win32_link(const char *oldpath, const char *newpath)
{
	init_hardlink();

	if (pCreateHardLinkA == NULL) {
		nix_platform_set_errno(EOPNOTSUPP);
		return -1;
	}

	if (pCreateHardLinkA(newpath, oldpath, NULL)) {
		return 0;
	}

	DWORD err = GetLastError();
	if (err == ERROR_ALREADY_EXISTS) {
		nix_platform_set_errno(EEXIST);
	} else if (err == ERROR_FILE_NOT_FOUND) {
		nix_platform_set_errno(ENOENT);
	} else if (err == ERROR_ACCESS_DENIED) {
		nix_platform_set_errno(EACCES);
	} else {
		nix_platform_set_errno(EIO);
	}

	return -1;
}

/*
 * ========================================================================
 * EXTENDED ATTRIBUTES (via Alternate Data Streams)
 * ========================================================================
 */

/* Maximum xattr name length */
#define XATTR_NAME_MAX 127

/* Maximum xattr value size (64KB default, can be larger with offset) */
#define XATTR_SIZE_MAX (64 * 1024)

/* macOS xattr names */
#define XATTR_RESOURCEFORK_NAME "com.apple.ResourceFork"
#define XATTR_FINDERINFO_NAME   "com.apple.FinderInfo"

/*
 * Build ADS name from xattr name
 */
static void
xattr_to_ads(const char *name, char *ads_name, size_t ads_size)
{
	/* Format: :xattr_name:$DATA */
	snprintf(ads_name, ads_size, ":xattr_%s", name);
}

/*
 * Set extended attribute with offset support
 */
int
nix_platform_win32_setxattr(const char *path, const char *name, const void *value,
							 size_t size, size_t offset, int flags)
{
	char ads_path[MAX_PATH + 256];
	char ads_name[256];
	HANDLE hFile;
	DWORD written;
	DWORD creation_disposition;

	if (strlen(name) > XATTR_NAME_MAX) {
		nix_platform_set_errno(ENAMETOOLONG);
		return -1;
	}

	/* Build ADS path */
	xattr_to_ads(name, ads_name, sizeof(ads_name));
	snprintf(ads_path, sizeof(ads_path), "%s%s", path, ads_name);

	/* Determine creation disposition based on flags */
	if (flags & 0x01) {  /* XATTR_CREATE */
		creation_disposition = CREATE_NEW;
	} else if (flags & 0x02) {  /* XATTR_REPLACE */
		creation_disposition = OPEN_EXISTING;
	} else {
		creation_disposition = OPEN_ALWAYS;
	}

	/* Open/create ADS */
	hFile = CreateFileA(ads_path,
						GENERIC_WRITE,
						FILE_SHARE_READ,
						NULL,
						creation_disposition,
						FILE_ATTRIBUTE_NORMAL,
						NULL);

	if (hFile == INVALID_HANDLE_VALUE) {
		DWORD err = GetLastError();
		if (err == ERROR_FILE_EXISTS) {
			nix_platform_set_errno(EEXIST);
		} else if (err == ERROR_FILE_NOT_FOUND) {
			nix_platform_set_errno(ENOATTR);
		} else {
			nix_platform_set_errno(EIO);
		}
		return -1;
	}

	/* Seek to offset if specified */
	if (offset > 0) {
		LARGE_INTEGER li;
		li.QuadPart = offset;
		if (!SetFilePointerEx(hFile, li, NULL, FILE_BEGIN)) {
			CloseHandle(hFile);
			nix_platform_set_errno(EINVAL);
			return -1;
		}
	}

	/* Write data */
	if (!WriteFile(hFile, value, (DWORD)size, &written, NULL) || written != size) {
		CloseHandle(hFile);
		nix_platform_set_errno(EIO);
		return -1;
	}

	CloseHandle(hFile);
	return 0;
}

/*
 * Get extended attribute with offset support
 */
ssize_t
nix_platform_win32_getxattr(const char *path, const char *name, void *value,
							 size_t size, size_t offset)
{
	char ads_path[MAX_PATH + 256];
	char ads_name[256];
	HANDLE hFile;
	DWORD read_bytes;
	LARGE_INTEGER file_size;

	if (strlen(name) > XATTR_NAME_MAX) {
		nix_platform_set_errno(ENAMETOOLONG);
		return -1;
	}

	/* Build ADS path */
	xattr_to_ads(name, ads_name, sizeof(ads_name));
	snprintf(ads_path, sizeof(ads_path), "%s%s", path, ads_name);

	/* Open ADS */
	hFile = CreateFileA(ads_path,
						GENERIC_READ,
						FILE_SHARE_READ | FILE_SHARE_WRITE,
						NULL,
						OPEN_EXISTING,
						FILE_ATTRIBUTE_NORMAL,
						NULL);

	if (hFile == INVALID_HANDLE_VALUE) {
		DWORD err = GetLastError();
		if (err == ERROR_FILE_NOT_FOUND) {
			nix_platform_set_errno(ENOATTR);
		} else {
			nix_platform_set_errno(EIO);
		}
		return -1;
	}

	/* Get file size */
	if (!GetFileSizeEx(hFile, &file_size)) {
		CloseHandle(hFile);
		nix_platform_set_errno(EIO);
		return -1;
	}

	/* If size is 0, return the attribute size */
	if (size == 0) {
		CloseHandle(hFile);
		return (ssize_t)file_size.QuadPart;
	}

	/* Seek to offset if specified */
	if (offset > 0) {
		LARGE_INTEGER li;
		li.QuadPart = offset;
		if (!SetFilePointerEx(hFile, li, NULL, FILE_BEGIN)) {
			CloseHandle(hFile);
			nix_platform_set_errno(EINVAL);
			return -1;
		}
	}

	/* Adjust read size based on available data */
	size_t available = (size_t)(file_size.QuadPart - offset);
	if (size > available) {
		size = available;
	}

	/* Read data */
	if (!ReadFile(hFile, value, (DWORD)size, &read_bytes, NULL)) {
		CloseHandle(hFile);
		nix_platform_set_errno(EIO);
		return -1;
	}

	CloseHandle(hFile);
	return (ssize_t)read_bytes;
}

/*
 * List extended attributes
 */
ssize_t
nix_platform_win32_listxattr(const char *path, char *list, size_t size)
{
	WIN32_FIND_STREAM_DATA stream_data;
	HANDLE hFind;
	size_t total_size = 0;
	size_t offset = 0;

	/* Find first stream */
	hFind = FindFirstStreamW(path_to_wide(path), FindStreamInfoStandard, &stream_data, 0);

	if (hFind == INVALID_HANDLE_VALUE) {
		/* No streams or file doesn't exist */
		if (size == 0) {
			return 0;
		}
		if (list) {
			list[0] = '\0';
		}
		return 0;
	}

	do {
		/* Convert stream name to UTF-8 */
		char *stream_name = wide_to_path(stream_data.cStreamName);
		if (!stream_name)
			continue;

		/* Skip default data stream */
		if (strcmp(stream_name, "::$DATA") == 0) {
			free(stream_name);
			continue;
		}

		/* Extract xattr name from ":xattr_name:$DATA" */
		if (strncmp(stream_name, ":xattr_", 7) == 0) {
			char *end = strrchr(stream_name, ':');
			if (end && strcmp(end, ":$DATA") == 0) {
				*end = '\0';
				const char *xattr_name = stream_name + 7;
				size_t name_len = strlen(xattr_name) + 1;

				total_size += name_len;

				if (list && offset + name_len <= size) {
					strcpy(list + offset, xattr_name);
					offset += name_len;
				}
			}
		}

		free(stream_name);

	} while (FindNextStreamW(hFind, &stream_data));

	FindClose(hFind);

	/* If size is 0, return required size */
	if (size == 0) {
		return (ssize_t)total_size;
	}

	/* Check if buffer was too small */
	if (total_size > size) {
		nix_platform_set_errno(ERANGE);
		return -1;
	}

	return (ssize_t)offset;
}

/*
 * Remove extended attribute
 */
int
nix_platform_win32_removexattr(const char *path, const char *name)
{
	char ads_path[MAX_PATH + 256];
	char ads_name[256];

	if (strlen(name) > XATTR_NAME_MAX) {
		nix_platform_set_errno(ENAMETOOLONG);
		return -1;
	}

	/* Build ADS path */
	xattr_to_ads(name, ads_name, sizeof(ads_name));
	snprintf(ads_path, sizeof(ads_path), "%s%s", path, ads_name);

	/* Delete ADS */
	if (!DeleteFileA(ads_path)) {
		DWORD err = GetLastError();
		if (err == ERROR_FILE_NOT_FOUND) {
			nix_platform_set_errno(ENOATTR);
		} else {
			nix_platform_set_errno(EIO);
		}
		return -1;
	}

	return 0;
}

/*
 * ========================================================================
 * macOS COMPATIBILITY
 * ========================================================================
 */

/*
 * Get macOS-style resource fork (stored as xattr)
 */
ssize_t
nix_platform_win32_getresourcefork(const char *path, void *data, size_t size, size_t offset)
{
	return nix_platform_win32_getxattr(path, XATTR_RESOURCEFORK_NAME, data, size, offset);
}

/*
 * Set macOS-style resource fork (stored as xattr)
 */
int
nix_platform_win32_setresourcefork(const char *path, const void *data, size_t size, size_t offset)
{
	return nix_platform_win32_setxattr(path, XATTR_RESOURCEFORK_NAME, data, size, offset, 0);
}

/*
 * Get macOS Finder info (32 bytes)
 */
ssize_t
nix_platform_win32_getfinderinfo(const char *path, void *info)
{
	ssize_t ret = nix_platform_win32_getxattr(path, XATTR_FINDERINFO_NAME, info, 32, 0);
	if (ret < 0 && errno == ENOATTR) {
		/* Return zeros if no Finder info */
		memset(info, 0, 32);
		return 32;
	}
	return ret;
}

/*
 * Set macOS Finder info (32 bytes)
 */
int
nix_platform_win32_setfinderinfo(const char *path, const void *info)
{
	return nix_platform_win32_setxattr(path, XATTR_FINDERINFO_NAME, info, 32, 0, 0);
}

#endif /* NIX_HOST_WIN32 */
