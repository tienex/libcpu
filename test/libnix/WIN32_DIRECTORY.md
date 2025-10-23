# Windows NT Directory Operations Emulation

Complete POSIX/BSD/Linux directory operations implementation for Windows NT 3.1 through Windows 11.

## Overview

This module provides full emulation of Unix directory operations on Windows using FindFirstFile/FindNextFile APIs. All functions maintain POSIX semantics while efficiently using native Windows APIs.

**Features:**
- Complete opendir/readdir/closedir implementation
- Directory stream positioning (telldir/seekdir/rewinddir)
- Directory scanning with filtering (scandir/alphasort)
- FD to DIR* conversion (dirfd/fdopendir)
- Linux getdents/getdents64 emulation

**File:** `nix-platform-win32-directory.c` (692 lines)

---

## Implementation Status

| Function | Status | Windows API | NT 3.1+ |
|----------|--------|-------------|---------|
| opendir | ✅ | FindFirstFileA | ✅ |
| readdir | ✅ | FindNextFileA | ✅ |
| closedir | ✅ | FindClose | ✅ |
| rewinddir | ✅ | FindFirstFileA | ✅ |
| telldir | ✅ | Position tracking | ✅ |
| seekdir | ✅ | Position tracking + readdir | ✅ |
| dirfd | ✅ | FD allocation | ✅ |
| fdopendir | ✅ | FD lookup | ✅ |
| scandir | ✅ | opendir + filter + qsort | ✅ |
| alphasort | ✅ | strcmp | ✅ |
| versionsort | ✅ | alphasort (simplified) | ✅ |
| getdents | ✅ | readdir loop | ✅ |
| getdents64 | ✅ | getdents wrapper | ✅ |

---

## Architecture

### Directory Stream Structure

```c
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
```

### Directory Entry Mapping

**Windows → POSIX:**
- `cFileName` → `d_name`
- `dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY` → `d_type = DT_DIR`
- `dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT` → `d_type = DT_LNK`
- File index hash → `d_ino`

### FD Tracking

Global table tracks DIR* to FD mappings:
- FD range: 1000-1255 (256 max directory FDs)
- Thread-safe with CRITICAL_SECTION
- Automatic allocation via dirfd()
- Cleanup on closedir()

---

## API Reference

### opendir

Opens a directory stream for reading entries.

```c
DIR *nix_platform_win32_opendir(const char *dirname);
```

**Parameters:**
- `dirname` - Path to directory

**Returns:**
- Directory pointer on success
- NULL on error (sets errno)

**Errors:**
- `EINVAL` - NULL dirname
- `ENOMEM` - Memory allocation failed
- `ENOENT` - Directory not found
- `EACCES` - Access denied

**Example:**
```c
DIR *dir = nix_platform_win32_opendir("C:\\Program Files");
if (!dir) {
    perror("opendir");
    return -1;
}
```

**Implementation:**
- Converts path to Windows format (backslashes)
- Appends `\*` wildcard for FindFirstFile
- Stores handle and path in nix_dir structure
- Handles empty directories gracefully

---

### readdir

Reads the next directory entry from the stream.

```c
struct dirent *nix_platform_win32_readdir(DIR *dirp);
```

**Parameters:**
- `dirp` - Directory stream from opendir

**Returns:**
- Pointer to dirent on success
- NULL at end of directory or on error

**Errors:**
- `EBADF` - Invalid directory stream
- `EIO` - Read error

**Example:**
```c
struct dirent *entry;
while ((entry = nix_platform_win32_readdir(dir)) != NULL) {
    printf("%s\n", entry->d_name);
}
```

**Fields:**
- `d_name` - Filename
- `d_type` - File type (DT_DIR, DT_REG, DT_LNK, etc.)
- `d_ino` - Inode number (hash of filename)
- `d_reclen` - Record length

**Implementation:**
- First call returns data from FindFirstFile
- Subsequent calls use FindNextFile
- Sets EOF flag when no more files
- Returns same buffer (not thread-safe per POSIX spec)

---

### closedir

Closes the directory stream and releases resources.

```c
int nix_platform_win32_closedir(DIR *dirp);
```

**Parameters:**
- `dirp` - Directory stream to close

**Returns:**
- 0 on success
- -1 on error (sets errno)

**Errors:**
- `EBADF` - Invalid directory stream

**Example:**
```c
nix_platform_win32_closedir(dir);
```

**Implementation:**
- Closes Windows search handle with FindClose
- Frees directory FD if allocated
- Frees path string
- Frees nix_dir structure

---

### rewinddir

Resets the directory stream to the beginning.

```c
void nix_platform_win32_rewinddir(DIR *dirp);
```

**Parameters:**
- `dirp` - Directory stream to rewind

**Example:**
```c
// First pass
while (readdir(dir)) { /* ... */ }

// Second pass
nix_platform_win32_rewinddir(dir);
while (readdir(dir)) { /* ... */ }
```

**Implementation:**
- Closes existing search handle
- Restarts FindFirstFile with original path
- Resets position to 0
- Clears EOF flag

---

### telldir / seekdir

Get and set position in directory stream.

```c
long nix_platform_win32_telldir(DIR *dirp);
void nix_platform_win32_seekdir(DIR *dirp, long loc);
```

**Parameters:**
- `dirp` - Directory stream
- `loc` - Position to seek to

**Returns:**
- telldir: Current position (0-based index)
- seekdir: void

**Example:**
```c
long pos = nix_platform_win32_telldir(dir);
// Read some entries...
nix_platform_win32_seekdir(dir, pos);  // Go back
```

**Implementation:**
- telldir: Returns current position counter
- seekdir:
  - Backward: Rewind + skip forward
  - Forward: Skip entries with readdir
  - Note: Forward seeking is more efficient

---

### dirfd / fdopendir

Convert between directory stream and file descriptor.

```c
int nix_platform_win32_dirfd(DIR *dirp);
DIR *nix_platform_win32_fdopendir(int fd);
```

**Parameters:**
- `dirp` - Directory stream
- `fd` - File descriptor

**Returns:**
- dirfd: File descriptor (1000-1255 range)
- fdopendir: Directory pointer

**Errors:**
- `EINVAL` - Invalid parameter
- `EBADF` - Invalid FD
- `EMFILE` - Too many directory FDs open
- `ENOTDIR` - FD is not a directory

**Example:**
```c
DIR *dir = opendir(".");
int fd = nix_platform_win32_dirfd(dir);
fstatat(fd, "file.txt", &st, 0);  // Use with *at functions

DIR *dir2 = nix_platform_win32_fdopendir(fd);
```

**Implementation:**
- dirfd: Allocates FD on first call, caches in nix_dir
- fdopendir: Looks up DIR* from global FD table
- Thread-safe FD allocation with CRITICAL_SECTION

---

### scandir

Scans directory and selects entries matching a filter.

```c
int nix_platform_win32_scandir(
    const char *dirname,
    struct dirent ***namelist,
    int (*filter)(const struct dirent *),
    int (*compar)(const struct dirent **, const struct dirent **)
);
```

**Parameters:**
- `dirname` - Directory to scan
- `namelist` - Output array of entries (caller must free)
- `filter` - Optional filter function (NULL = all entries)
- `compar` - Optional comparison function for sorting

**Returns:**
- Number of entries selected
- -1 on error (sets errno)

**Errors:**
- `EINVAL` - NULL dirname or namelist
- `ENOMEM` - Memory allocation failed

**Example:**
```c
int filter_c_files(const struct dirent *entry) {
    size_t len = strlen(entry->d_name);
    return len > 2 && strcmp(entry->d_name + len - 2, ".c") == 0;
}

struct dirent **list;
int n = nix_platform_win32_scandir(".", &list, filter_c_files, alphasort);

for (int i = 0; i < n; i++) {
    printf("%s\n", list[i]->d_name);
    free(list[i]);
}
free(list);
```

**Implementation:**
- Opens directory with opendir
- Reads all entries, applies filter
- Allocates array (starts at 32, doubles as needed)
- Sorts with qsort if comparator provided
- Returns allocated array (caller must free)

---

### alphasort / versionsort

Comparison functions for scandir.

```c
int nix_platform_win32_alphasort(const struct dirent **a, const struct dirent **b);
int nix_platform_win32_versionsort(const struct dirent **a, const struct dirent **b);
```

**Parameters:**
- `a`, `b` - Pointers to dirent pointers

**Returns:**
- Negative if a < b
- Zero if a == b
- Positive if a > b

**Example:**
```c
struct dirent **list;
int n = scandir(".", &list, NULL, nix_platform_win32_alphasort);
```

**Implementation:**
- alphasort: Uses strcmp on d_name
- versionsort: Falls back to alphasort (Windows doesn't have strverscmp)

---

### getdents / getdents64

Linux-specific syscalls for reading directory entries.

```c
ssize_t nix_platform_win32_getdents(int fd, void *buf, size_t count);
ssize_t nix_platform_win32_getdents64(int fd, void *buf, size_t count);
```

**Parameters:**
- `fd` - Directory file descriptor (from dirfd)
- `buf` - Output buffer
- `count` - Buffer size

**Returns:**
- Number of bytes read
- 0 at EOF
- -1 on error (sets errno)

**Errors:**
- `EBADF` - Invalid FD
- `EINVAL` - Invalid buffer or count

**Example:**
```c
char buf[8192];
ssize_t bytes;

int fd = open(".", O_RDONLY | O_DIRECTORY);
while ((bytes = nix_platform_win32_getdents(fd, buf, sizeof(buf))) > 0) {
    for (size_t pos = 0; pos < bytes; ) {
        struct dirent *entry = (struct dirent *)(buf + pos);
        printf("%s\n", entry->d_name);
        pos += entry->d_reclen;
    }
}
close(fd);
```

**Implementation:**
- Looks up DIR* from FD
- Fills buffer with dirent structures via readdir
- Stops when buffer cannot fit another entry
- getdents64: Same as getdents (no 64-bit inodes on Windows)

---

## Usage Examples

### List All Files

```c
#include "nix-platform-win32.h"

void list_directory(const char *path) {
    DIR *dir = nix_platform_win32_opendir(path);
    if (!dir) {
        perror("opendir");
        return;
    }

    struct dirent *entry;
    while ((entry = nix_platform_win32_readdir(dir)) != NULL) {
        const char *type;
        switch (entry->d_type) {
        case DT_DIR:  type = "DIR "; break;
        case DT_REG:  type = "FILE"; break;
        case DT_LNK:  type = "LINK"; break;
        default:      type = "????"; break;
        }

        printf("%s  %s\n", type, entry->d_name);
    }

    nix_platform_win32_closedir(dir);
}
```

### Scan for Specific Files

```c
int is_header_file(const struct dirent *entry) {
    size_t len = strlen(entry->d_name);
    return len > 2 && strcmp(entry->d_name + len - 2, ".h") == 0;
}

void list_headers(const char *path) {
    struct dirent **list;
    int n = nix_platform_win32_scandir(
        path, &list, is_header_file, nix_platform_win32_alphasort
    );

    if (n < 0) {
        perror("scandir");
        return;
    }

    for (int i = 0; i < n; i++) {
        printf("%s\n", list[i]->d_name);
        free(list[i]);
    }
    free(list);
}
```

### Directory with Seek/Tell

```c
void process_with_checkpoints(const char *path) {
    DIR *dir = nix_platform_win32_opendir(path);
    if (!dir) return;

    long checkpoint = 0;
    struct dirent *entry;

    while ((entry = nix_platform_win32_readdir(dir)) != NULL) {
        if (strcmp(entry->d_name, "important.txt") == 0) {
            checkpoint = nix_platform_win32_telldir(dir);
        }

        process_file(entry->d_name);

        if (error_occurred) {
            // Go back to checkpoint
            nix_platform_win32_seekdir(dir, checkpoint);
        }
    }

    nix_platform_win32_closedir(dir);
}
```

---

## Performance Characteristics

### Time Complexity

| Operation | Complexity | Notes |
|-----------|-----------|-------|
| opendir | O(1) | Single FindFirstFile |
| readdir | O(1) | Single FindNextFile |
| closedir | O(1) | Single FindClose |
| rewinddir | O(1) | Close + reopen |
| telldir | O(1) | Return counter |
| seekdir forward | O(n) | Skip n entries |
| seekdir backward | O(n) | Rewind + skip n |
| scandir | O(n log n) | Read all + sort |
| getdents | O(k) | Read k entries |

### Memory Usage

- Directory stream: ~350 bytes per DIR*
- Entry buffer: 260 bytes (MAX_PATH)
- scandir: n * sizeof(struct dirent) for n entries
- FD table: 256 * 8 bytes = 2KB global

### Windows Version Compatibility

- **NT 3.1+**: All functions supported
- **2000+**: No changes
- **Vista+**: No changes
- **Win10+**: No changes

All functions use Win32 APIs available since NT 3.1.

---

## Limitations

1. **Inode Numbers**: Not true inodes, uses filename hash
2. **seekdir Efficiency**: Backward seeking requires rewind + forward
3. **Thread Safety**: readdir returns pointer to shared buffer (per POSIX)
4. **File Types**: Limited type detection (no socket/FIFO detection)
5. **64-bit inodes**: getdents64 identical to getdents
6. **versionsort**: Falls back to alphasort (no strverscmp on Windows)

---

## Error Handling

All functions follow POSIX error conventions:

```c
DIR *dir = nix_platform_win32_opendir(path);
if (!dir) {
    switch (errno) {
    case ENOENT:
        fprintf(stderr, "Directory not found\n");
        break;
    case EACCES:
        fprintf(stderr, "Access denied\n");
        break;
    case ENOMEM:
        fprintf(stderr, "Out of memory\n");
        break;
    default:
        perror("opendir");
        break;
    }
    return;
}
```

---

## Windows-Specific Notes

### Path Handling

- Accepts both `/` and `\\` as separators
- Automatically converts to Windows format
- Removes trailing slashes
- Maximum path length: 260 characters (MAX_PATH)

### Hidden Files

Windows hidden files (`FILE_ATTRIBUTE_HIDDEN`) are:
- Returned by readdir (like Unix `.` prefix files)
- Can be filtered by checking `d_type` or using filter in scandir

### Case Sensitivity

Windows filesystems are case-insensitive by default:
- Comparisons match Windows behavior
- Same file may appear with different cases
- Use `_stricmp` for case-insensitive comparison

---

## Integration

These functions integrate with other libnix Win32 modules:

```c
// Use with *at functions
DIR *dir = opendir(".");
int dirfd = nix_platform_win32_dirfd(dir);
fstatat(dirfd, "file.txt", &st, 0);

// Use with fd operations
int fd = nix_platform_win32_dirfd(dir);
fsync(fd);  // Sync directory (Windows ignores)

// Scan and process
struct dirent **list;
int n = scandir(".", &list, NULL, alphasort);
for (int i = 0; i < n; i++) {
    char path[MAX_PATH];
    snprintf(path, sizeof(path), "./%s", list[i]->d_name);

    struct stat st;
    if (lstat(path, &st) == 0) {
        printf("%s: %lld bytes\n", list[i]->d_name, st.st_size);
    }
    free(list[i]);
}
free(list);
```

---

**Implementation:** `nix-platform-win32-directory.c`
**Header:** `nix-platform-win32.h`
**Build:** Automatically included in libnix when `NIX_HOST_WIN32` defined
