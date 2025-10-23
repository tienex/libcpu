/*
 * nix-platform-win32-directory.c
 *
 * Directory operations emulation for Windows NT
 *
 * Implements POSIX/BSD directory functions using Windows APIs:
 * - opendir/readdir/closedir - directory stream operations
 * - rewinddir/seekdir/telldir - directory positioning
 * - scandir/alphasort - directory scanning and sorting
 * - dirfd/fdopendir - fd <-> DIR* conversion
 * - getdents/getdents64 - Linux directory entry retrieval
 *
 * Compatible with Windows NT 3.1 through Windows 11
 */

#if defined(NIX_HOST_WIN32)

#include "nix-platform-win32.h"
#include <windows.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <dirent.h>

/* Maximum path length for Windows */
#ifndef MAX_PATH_LEN
#define MAX_PATH_LEN 32768
#endif

/* Directory entry structure (private) */
struct nix_dir_entry {
	struct dirent entry;      /* Public dirent structure */
	char d_name_storage[MAX_PATH];  /* Storage for d_name */
};

/* Directory stream structure (opaque to user) */
struct nix_dir {
	HANDLE handle;                  /* Windows search handle */
	WIN32_FIND_DATAA find_data;     /* Current find data */
	struct nix_dir_entry current;   /* Current entry */
	long position;                  /* Current position */
	int first_read;                 /* First read flag */
	int eof;                        /* End of directory flag */
	char *path;                     /* Directory path */
	int fd;                         /* Associated file descriptor (-1 if none) */
};

/* Global directory table for fd tracking */
#define MAX_DIR_FDS 256
static struct {
	struct nix_dir *dir;
	int in_use;
} g_dir_fds[MAX_DIR_FDS];
static CRITICAL_SECTION g_dir_lock;
static int g_dir_init = 0;

/* Initialize directory subsystem */
static void init_dir_subsystem(void)
{
	if (!g_dir_init) {
		InitializeCriticalSection(&g_dir_lock);
		memset(g_dir_fds, 0, sizeof(g_dir_fds));
		g_dir_init = 1;
	}
}

/* Allocate directory fd */
static int alloc_dir_fd(struct nix_dir *dir)
{
	int fd = -1;

	init_dir_subsystem();

	EnterCriticalSection(&g_dir_lock);

	for (int i = 0; i < MAX_DIR_FDS; i++) {
		if (!g_dir_fds[i].in_use) {
			g_dir_fds[i].dir = dir;
			g_dir_fds[i].in_use = 1;
			fd = i + 1000;  /* Offset to avoid conflicts with regular fds */
			break;
		}
	}

	LeaveCriticalSection(&g_dir_lock);

	if (fd == -1) {
		errno = EMFILE;
	}

	return fd;
}

/* Get directory from fd */
static struct nix_dir *get_dir_from_fd(int fd)
{
	struct nix_dir *dir = NULL;

	if (fd < 1000 || fd >= 1000 + MAX_DIR_FDS) {
		errno = EBADF;
		return NULL;
	}

	init_dir_subsystem();

	EnterCriticalSection(&g_dir_lock);

	int idx = fd - 1000;
	if (g_dir_fds[idx].in_use) {
		dir = g_dir_fds[idx].dir;
	} else {
		errno = EBADF;
	}

	LeaveCriticalSection(&g_dir_lock);

	return dir;
}

/* Free directory fd */
static void free_dir_fd(int fd)
{
	if (fd < 1000 || fd >= 1000 + MAX_DIR_FDS) {
		return;
	}

	init_dir_subsystem();

	EnterCriticalSection(&g_dir_lock);

	int idx = fd - 1000;
	g_dir_fds[idx].dir = NULL;
	g_dir_fds[idx].in_use = 0;

	LeaveCriticalSection(&g_dir_lock);
}

/*
 * opendir - Open directory stream
 *
 * Opens a directory stream for reading entries.
 * Returns directory pointer on success, NULL on error.
 */
DIR *nix_platform_win32_opendir(const char *dirname)
{
	if (!dirname) {
		errno = EINVAL;
		return NULL;
	}

	/* Allocate directory structure */
	struct nix_dir *dir = (struct nix_dir *)calloc(1, sizeof(struct nix_dir));
	if (!dir) {
		errno = ENOMEM;
		return NULL;
	}

	/* Store directory path */
	dir->path = _strdup(dirname);
	if (!dir->path) {
		free(dir);
		errno = ENOMEM;
		return NULL;
	}

	/* Build search pattern: dirname\* */
	char pattern[MAX_PATH_LEN];
	size_t len = strlen(dirname);

	/* Remove trailing slash if present */
	if (len > 0 && (dirname[len-1] == '\\' || dirname[len-1] == '/')) {
		snprintf(pattern, sizeof(pattern), "%.*s*", (int)(len-1), dirname);
	} else {
		snprintf(pattern, sizeof(pattern), "%s\\*", dirname);
	}

	/* Start directory search */
	dir->handle = FindFirstFileA(pattern, &dir->find_data);

	if (dir->handle == INVALID_HANDLE_VALUE) {
		DWORD err = GetLastError();
		free(dir->path);
		free(dir);

		if (err == ERROR_FILE_NOT_FOUND || err == ERROR_NO_MORE_FILES) {
			/* Empty directory is OK */
			errno = 0;
		} else if (err == ERROR_PATH_NOT_FOUND) {
			errno = ENOENT;
		} else if (err == ERROR_ACCESS_DENIED) {
			errno = EACCES;
		} else {
			errno = EINVAL;
		}
		return NULL;
	}

	dir->first_read = 1;
	dir->eof = 0;
	dir->position = 0;
	dir->fd = -1;

	return (DIR *)dir;
}

/*
 * readdir - Read directory entry
 *
 * Reads the next directory entry from the stream.
 * Returns pointer to entry on success, NULL at end or error.
 */
struct dirent *nix_platform_win32_readdir(DIR *dirp)
{
	if (!dirp) {
		errno = EBADF;
		return NULL;
	}

	struct nix_dir *dir = (struct nix_dir *)dirp;

	if (dir->eof) {
		return NULL;
	}

	/* On first read, we already have data from FindFirstFile */
	if (!dir->first_read) {
		if (!FindNextFileA(dir->handle, &dir->find_data)) {
			DWORD err = GetLastError();
			if (err == ERROR_NO_MORE_FILES) {
				dir->eof = 1;
			} else {
				errno = EIO;
			}
			return NULL;
		}
	}

	dir->first_read = 0;

	/* Fill in dirent structure */
	struct dirent *entry = &dir->current.entry;

	/* Copy filename */
	strncpy(dir->current.d_name_storage, dir->find_data.cFileName, MAX_PATH - 1);
	dir->current.d_name_storage[MAX_PATH - 1] = '\0';
	entry->d_name = dir->current.d_name_storage;

	/* Determine file type */
	if (dir->find_data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
		entry->d_type = DT_DIR;
	} else if (dir->find_data.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) {
		entry->d_type = DT_LNK;
	} else if (dir->find_data.dwFileAttributes & FILE_ATTRIBUTE_DEVICE) {
		entry->d_type = DT_CHR;
	} else {
		entry->d_type = DT_REG;
	}

	/* Set inode number (use file index on NTFS) */
	/* For simplicity, use a hash of the filename */
	unsigned long hash = 5381;
	const char *str = entry->d_name;
	while (*str) {
		hash = ((hash << 5) + hash) + (unsigned char)(*str++);
	}
	entry->d_ino = hash;

	/* Set record length */
	entry->d_reclen = sizeof(struct dirent);

	/* Increment position */
	dir->position++;

	return entry;
}

/*
 * closedir - Close directory stream
 *
 * Closes the directory stream and releases resources.
 * Returns 0 on success, -1 on error.
 */
int nix_platform_win32_closedir(DIR *dirp)
{
	if (!dirp) {
		errno = EBADF;
		return -1;
	}

	struct nix_dir *dir = (struct nix_dir *)dirp;

	/* Close Windows search handle */
	if (dir->handle != INVALID_HANDLE_VALUE) {
		FindClose(dir->handle);
	}

	/* Free directory fd if allocated */
	if (dir->fd != -1) {
		free_dir_fd(dir->fd);
	}

	/* Free path */
	if (dir->path) {
		free(dir->path);
	}

	/* Free directory structure */
	free(dir);

	return 0;
}

/*
 * rewinddir - Reset directory stream to beginning
 *
 * Resets the directory stream to the first entry.
 */
void nix_platform_win32_rewinddir(DIR *dirp)
{
	if (!dirp) {
		return;
	}

	struct nix_dir *dir = (struct nix_dir *)dirp;

	/* Close existing search handle */
	if (dir->handle != INVALID_HANDLE_VALUE) {
		FindClose(dir->handle);
	}

	/* Rebuild search pattern */
	char pattern[MAX_PATH_LEN];
	size_t len = strlen(dir->path);

	if (len > 0 && (dir->path[len-1] == '\\' || dir->path[len-1] == '/')) {
		snprintf(pattern, sizeof(pattern), "%.*s*", (int)(len-1), dir->path);
	} else {
		snprintf(pattern, sizeof(pattern), "%s\\*", dir->path);
	}

	/* Restart directory search */
	dir->handle = FindFirstFileA(pattern, &dir->find_data);
	dir->first_read = 1;
	dir->eof = 0;
	dir->position = 0;
}

/*
 * telldir - Get current position in directory stream
 *
 * Returns the current position in the directory stream.
 */
long nix_platform_win32_telldir(DIR *dirp)
{
	if (!dirp) {
		errno = EBADF;
		return -1;
	}

	struct nix_dir *dir = (struct nix_dir *)dirp;
	return dir->position;
}

/*
 * seekdir - Set position in directory stream
 *
 * Sets the position in the directory stream.
 * Note: Only forward seeking is efficiently supported on Windows.
 */
void nix_platform_win32_seekdir(DIR *dirp, long loc)
{
	if (!dirp) {
		return;
	}

	struct nix_dir *dir = (struct nix_dir *)dirp;

	/* If seeking backwards or to beginning, rewind */
	if (loc <= dir->position) {
		nix_platform_win32_rewinddir(dirp);

		if (loc == 0) {
			return;
		}
	}

	/* Skip forward to desired position */
	while (dir->position < loc) {
		if (!nix_platform_win32_readdir(dirp)) {
			break;
		}
	}
}

/*
 * dirfd - Get file descriptor from directory stream
 *
 * Returns the file descriptor associated with the directory stream.
 * Creates one if it doesn't exist.
 */
int nix_platform_win32_dirfd(DIR *dirp)
{
	if (!dirp) {
		errno = EINVAL;
		return -1;
	}

	struct nix_dir *dir = (struct nix_dir *)dirp;

	/* If fd already allocated, return it */
	if (dir->fd != -1) {
		return dir->fd;
	}

	/* Allocate new fd */
	dir->fd = alloc_dir_fd(dir);
	return dir->fd;
}

/*
 * fdopendir - Open directory stream from file descriptor
 *
 * Opens a directory stream from a file descriptor.
 * Returns directory pointer on success, NULL on error.
 */
DIR *nix_platform_win32_fdopendir(int fd)
{
	/* Check if fd is a directory fd */
	struct nix_dir *dir = get_dir_from_fd(fd);

	if (dir) {
		/* Already a directory fd, return it */
		return (DIR *)dir;
	}

	/* Try to get path from regular fd */
	/* This is a simplified implementation - in a real scenario,
	 * we would need to get the path from the fd using GetFinalPathNameByHandle */

	errno = ENOTDIR;
	return NULL;
}

/*
 * scandir - Scan directory for matching entries
 *
 * Scans the directory and selects entries matching a filter function.
 * Returns number of entries selected, -1 on error.
 */
int nix_platform_win32_scandir(
	const char *dirname,
	struct dirent ***namelist,
	int (*filter)(const struct dirent *),
	int (*compar)(const struct dirent **, const struct dirent **)
)
{
	if (!dirname || !namelist) {
		errno = EINVAL;
		return -1;
	}

	/* Open directory */
	DIR *dirp = nix_platform_win32_opendir(dirname);
	if (!dirp) {
		return -1;
	}

	/* Allocate initial array */
	size_t capacity = 32;
	size_t count = 0;
	struct dirent **entries = (struct dirent **)malloc(capacity * sizeof(struct dirent *));

	if (!entries) {
		nix_platform_win32_closedir(dirp);
		errno = ENOMEM;
		return -1;
	}

	/* Read all entries */
	struct dirent *entry;
	while ((entry = nix_platform_win32_readdir(dirp)) != NULL) {
		/* Apply filter if provided */
		if (filter && !filter(entry)) {
			continue;
		}

		/* Expand array if needed */
		if (count >= capacity) {
			capacity *= 2;
			struct dirent **new_entries = (struct dirent **)realloc(
				entries, capacity * sizeof(struct dirent *)
			);

			if (!new_entries) {
				for (size_t i = 0; i < count; i++) {
					free(entries[i]);
				}
				free(entries);
				nix_platform_win32_closedir(dirp);
				errno = ENOMEM;
				return -1;
			}
			entries = new_entries;
		}

		/* Allocate and copy entry */
		entries[count] = (struct dirent *)malloc(sizeof(struct dirent));
		if (!entries[count]) {
			for (size_t i = 0; i < count; i++) {
				free(entries[i]);
			}
			free(entries);
			nix_platform_win32_closedir(dirp);
			errno = ENOMEM;
			return -1;
		}

		memcpy(entries[count], entry, sizeof(struct dirent));
		count++;
	}

	nix_platform_win32_closedir(dirp);

	/* Sort entries if comparison function provided */
	if (compar && count > 0) {
		qsort(entries, count, sizeof(struct dirent *),
			  (int (*)(const void *, const void *))compar);
	}

	*namelist = entries;
	return (int)count;
}

/*
 * alphasort - Compare directory entries alphabetically
 *
 * Comparison function for scandir to sort entries alphabetically.
 */
int nix_platform_win32_alphasort(const struct dirent **a, const struct dirent **b)
{
	return strcmp((*a)->d_name, (*b)->d_name);
}

/*
 * versionsort - Compare directory entries by version
 *
 * Comparison function for scandir to sort entries by version number.
 */
int nix_platform_win32_versionsort(const struct dirent **a, const struct dirent **b)
{
	/* For Windows, just use alphasort since version sorting is complex */
	return nix_platform_win32_alphasort(a, b);
}

/*
 * getdents - Get directory entries (Linux syscall)
 *
 * Reads directory entries into a buffer.
 * Returns number of bytes read, 0 at EOF, -1 on error.
 *
 * Note: This is a Linux-specific syscall. We provide basic emulation.
 */
ssize_t nix_platform_win32_getdents(int fd, void *buf, size_t count)
{
	struct nix_dir *dir = get_dir_from_fd(fd);

	if (!dir) {
		errno = EBADF;
		return -1;
	}

	if (!buf || count == 0) {
		errno = EINVAL;
		return -1;
	}

	/* Read entries into buffer */
	char *ptr = (char *)buf;
	size_t bytes_written = 0;

	while (bytes_written + sizeof(struct dirent) <= count) {
		struct dirent *entry = nix_platform_win32_readdir((DIR *)dir);

		if (!entry) {
			break;
		}

		/* Copy entry to buffer */
		memcpy(ptr, entry, sizeof(struct dirent));
		ptr += sizeof(struct dirent);
		bytes_written += sizeof(struct dirent);
	}

	return bytes_written;
}

/*
 * getdents64 - Get directory entries with 64-bit inodes (Linux syscall)
 *
 * Same as getdents but with 64-bit inode numbers.
 */
ssize_t nix_platform_win32_getdents64(int fd, void *buf, size_t count)
{
	/* For simplicity, just use getdents since we don't have true 64-bit inodes */
	return nix_platform_win32_getdents(fd, buf, count);
}

#endif /* NIX_HOST_WIN32 */
