/*
 * nix-platform-win32-mmap.c
 *
 * Windows-specific memory mapping with full POSIX semantics
 * Maps mmap/munmap/mprotect/msync/madvise to Windows APIs
 */

#include "nix-platform.h"

#if defined(NIX_HOST_WIN32)

#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

/* Memory mapping tracking */
typedef struct mmap_region {
	void *addr;
	size_t length;
	int prot;
	int flags;
	int fd;
	off_t offset;
	HANDLE file_handle;
	HANDLE mapping_handle;
	struct mmap_region *next;
} mmap_region_t;

static mmap_region_t *g_mmap_regions = NULL;
static CRITICAL_SECTION g_mmap_lock;
static int g_mmap_initialized = 0;

/* Initialize mmap subsystem */
static void
win32_mmap_init(void)
{
	if (g_mmap_initialized)
		return;

	InitializeCriticalSection(&g_mmap_lock);
	g_mmap_regions = NULL;
	g_mmap_initialized = 1;
}

/* Convert POSIX protection flags to Windows */
static DWORD
prot_to_win32(int prot)
{
	/* Map PROT_* to PAGE_* */
	if (prot & 0x04) {  /* PROT_EXEC */
		if (prot & 0x02) {  /* PROT_WRITE */
			return PAGE_EXECUTE_READWRITE;
		} else if (prot & 0x01) {  /* PROT_READ */
			return PAGE_EXECUTE_READ;
		} else {
			return PAGE_EXECUTE;
		}
	} else {
		if (prot & 0x02) {  /* PROT_WRITE */
			if (prot & 0x01) {  /* PROT_READ */
				return PAGE_READWRITE;
			} else {
				return PAGE_READWRITE;  /* Write implies read on Windows */
			}
		} else if (prot & 0x01) {  /* PROT_READ */
			return PAGE_READONLY;
		} else {
			return PAGE_NOACCESS;
		}
	}
}

/* Convert POSIX protection to file mapping access */
static DWORD
prot_to_file_access(int prot)
{
	if (prot & 0x04) {  /* PROT_EXEC */
		if (prot & 0x02) {  /* PROT_WRITE */
			return FILE_MAP_WRITE | FILE_MAP_EXECUTE;
		} else {
			return FILE_MAP_READ | FILE_MAP_EXECUTE;
		}
	} else {
		if (prot & 0x02) {  /* PROT_WRITE */
			return FILE_MAP_WRITE;
		} else {
			return FILE_MAP_READ;
		}
	}
}

/* Convert POSIX flags to file mapping protection */
static DWORD
flags_to_mapping_prot(int prot, int flags)
{
	/* For shared mappings, use SEC_COMMIT */
	/* For private mappings, use copy-on-write */

	DWORD base;

	if (prot & 0x04) {  /* PROT_EXEC */
		if (prot & 0x02) {  /* PROT_WRITE */
			base = PAGE_EXECUTE_READWRITE;
		} else if (prot & 0x01) {  /* PROT_READ */
			base = PAGE_EXECUTE_READ;
		} else {
			base = PAGE_EXECUTE;
		}
	} else {
		if (prot & 0x02) {  /* PROT_WRITE */
			if (flags & 0x01) {  /* MAP_SHARED */
				base = PAGE_READWRITE;
			} else {  /* MAP_PRIVATE */
				base = PAGE_WRITECOPY;
			}
		} else if (prot & 0x01) {  /* PROT_READ */
			base = PAGE_READONLY;
		} else {
			base = PAGE_NOACCESS;
		}
	}

	return base | SEC_COMMIT;
}

/* Track a memory mapping */
static void
win32_mmap_track(void *addr, size_t length, int prot, int flags, int fd,
				 off_t offset, HANDLE file_handle, HANDLE mapping_handle)
{
	win32_mmap_init();

	mmap_region_t *region = malloc(sizeof(mmap_region_t));
	if (region == NULL)
		return;

	region->addr = addr;
	region->length = length;
	region->prot = prot;
	region->flags = flags;
	region->fd = fd;
	region->offset = offset;
	region->file_handle = file_handle;
	region->mapping_handle = mapping_handle;

	EnterCriticalSection(&g_mmap_lock);
	region->next = g_mmap_regions;
	g_mmap_regions = region;
	LeaveCriticalSection(&g_mmap_lock);
}

/* Find a tracked mapping */
static mmap_region_t *
win32_mmap_find(void *addr)
{
	win32_mmap_init();

	EnterCriticalSection(&g_mmap_lock);

	mmap_region_t *region = g_mmap_regions;
	while (region != NULL) {
		if (region->addr == addr) {
			LeaveCriticalSection(&g_mmap_lock);
			return region;
		}
		region = region->next;
	}

	LeaveCriticalSection(&g_mmap_lock);
	return NULL;
}

/* Find mapping containing address */
static mmap_region_t *
win32_mmap_find_containing(void *addr)
{
	win32_mmap_init();

	EnterCriticalSection(&g_mmap_lock);

	mmap_region_t *region = g_mmap_regions;
	while (region != NULL) {
		if (addr >= region->addr &&
			addr < (void *)((char *)region->addr + region->length)) {
			LeaveCriticalSection(&g_mmap_lock);
			return region;
		}
		region = region->next;
	}

	LeaveCriticalSection(&g_mmap_lock);
	return NULL;
}

/* Untrack a mapping */
static void
win32_mmap_untrack(void *addr)
{
	win32_mmap_init();

	EnterCriticalSection(&g_mmap_lock);

	mmap_region_t *region = g_mmap_regions;
	mmap_region_t *prev = NULL;

	while (region != NULL) {
		if (region->addr == addr) {
			if (prev == NULL) {
				g_mmap_regions = region->next;
			} else {
				prev->next = region->next;
			}

			free(region);
			LeaveCriticalSection(&g_mmap_lock);
			return;
		}

		prev = region;
		region = region->next;
	}

	LeaveCriticalSection(&g_mmap_lock);
}

/* mmap implementation */
void *
nix_platform_win32_mmap(void *addr, size_t length, int prot, int flags,
						int fd, off_t offset)
{
	win32_mmap_init();

	/* Validate parameters */
	if (length == 0) {
		errno = EINVAL;
		return (void *)-1;
	}

	/* Round length up to page boundary */
	SYSTEM_INFO si;
	GetSystemInfo(&si);
	size_t page_size = si.dwPageSize;
	size_t aligned_length = (length + page_size - 1) & ~(page_size - 1);

	HANDLE file_handle = INVALID_HANDLE_VALUE;
	HANDLE mapping_handle = NULL;
	void *mapped_addr = NULL;

	/* Check if this is a file-backed mapping */
	if (fd != -1 && !(flags & 0x20)) {  /* Not MAP_ANONYMOUS */
		/* Get Windows file handle from fd */
		file_handle = (HANDLE)_get_osfhandle(fd);
		if (file_handle == INVALID_HANDLE_VALUE) {
			errno = EBADF;
			return (void *)-1;
		}

		/* Create file mapping object */
		DWORD mapping_prot = flags_to_mapping_prot(prot, flags);
		DWORD size_high = (DWORD)((aligned_length + offset) >> 32);
		DWORD size_low = (DWORD)((aligned_length + offset) & 0xFFFFFFFF);

		mapping_handle = CreateFileMappingA(
			file_handle,
			NULL,
			mapping_prot,
			size_high,
			size_low,
			NULL
		);

		if (mapping_handle == NULL) {
			DWORD err = GetLastError();
			if (err == ERROR_ACCESS_DENIED) {
				errno = EACCES;
			} else if (err == ERROR_DISK_FULL) {
				errno = ENOSPC;
			} else {
				errno = EINVAL;
			}
			return (void *)-1;
		}

		/* Map view of file */
		DWORD file_access = prot_to_file_access(prot);
		DWORD offset_high = (DWORD)(offset >> 32);
		DWORD offset_low = (DWORD)(offset & 0xFFFFFFFF);

		if (flags & 0x10) {  /* MAP_FIXED */
			mapped_addr = MapViewOfFileEx(
				mapping_handle,
				file_access,
				offset_high,
				offset_low,
				aligned_length,
				addr
			);

			if (mapped_addr == NULL || mapped_addr != addr) {
				CloseHandle(mapping_handle);
				errno = EINVAL;
				return (void *)-1;
			}
		} else {
			mapped_addr = MapViewOfFile(
				mapping_handle,
				file_access,
				offset_high,
				offset_low,
				aligned_length
			);

			if (mapped_addr == NULL) {
				CloseHandle(mapping_handle);
				errno = ENOMEM;
				return (void *)-1;
			}
		}

	} else {
		/* Anonymous mapping - use VirtualAlloc */
		DWORD alloc_type = MEM_COMMIT | MEM_RESERVE;
		DWORD page_prot = prot_to_win32(prot);

		if (flags & 0x10) {  /* MAP_FIXED */
			/* Try to allocate at specific address */
			mapped_addr = VirtualAlloc(addr, aligned_length, alloc_type, page_prot);

			if (mapped_addr == NULL || mapped_addr != addr) {
				errno = EINVAL;
				return (void *)-1;
			}
		} else {
			/* Let system choose address */
			mapped_addr = VirtualAlloc(NULL, aligned_length, alloc_type, page_prot);

			if (mapped_addr == NULL) {
				errno = ENOMEM;
				return (void *)-1;
			}
		}

		mapping_handle = NULL;
	}

	/* Track this mapping */
	win32_mmap_track(mapped_addr, aligned_length, prot, flags, fd, offset,
					 file_handle, mapping_handle);

	return mapped_addr;
}

/* munmap implementation */
int
nix_platform_win32_munmap(void *addr, size_t length)
{
	win32_mmap_init();

	mmap_region_t *region = win32_mmap_find(addr);
	if (region == NULL) {
		errno = EINVAL;
		return -1;
	}

	/* Unmap the region */
	if (region->mapping_handle != NULL) {
		/* File-backed mapping */
		UnmapViewOfFile(addr);
		CloseHandle(region->mapping_handle);
	} else {
		/* Anonymous mapping */
		VirtualFree(addr, 0, MEM_RELEASE);
	}

	/* Untrack */
	win32_mmap_untrack(addr);

	return 0;
}

/* mprotect implementation */
int
nix_platform_win32_mprotect(void *addr, size_t length, int prot)
{
	win32_mmap_init();

	mmap_region_t *region = win32_mmap_find_containing(addr);
	if (region == NULL) {
		errno = ENOMEM;
		return -1;
	}

	/* Change protection */
	DWORD new_prot = prot_to_win32(prot);
	DWORD old_prot;

	if (!VirtualProtect(addr, length, new_prot, &old_prot)) {
		errno = EACCES;
		return -1;
	}

	/* Update tracked protection */
	if (addr == region->addr && length == region->length) {
		region->prot = prot;
	}

	return 0;
}

/* msync implementation */
int
nix_platform_win32_msync(void *addr, size_t length, int flags)
{
	win32_mmap_init();

	mmap_region_t *region = win32_mmap_find_containing(addr);
	if (region == NULL) {
		errno = ENOMEM;
		return -1;
	}

	/* Only meaningful for file-backed mappings */
	if (region->mapping_handle == NULL) {
		return 0;  /* Anonymous mappings - nothing to sync */
	}

	/* Flush to disk */
	if (!FlushViewOfFile(addr, length)) {
		errno = EIO;
		return -1;
	}

	/* If MS_SYNC, also flush file buffers */
	if (flags & 0x04) {  /* MS_SYNC */
		if (region->file_handle != INVALID_HANDLE_VALUE) {
			FlushFileBuffers(region->file_handle);
		}
	}

	return 0;
}

/* madvise implementation */
int
nix_platform_win32_madvise(void *addr, size_t length, int advice)
{
	win32_mmap_init();

	mmap_region_t *region = win32_mmap_find_containing(addr);
	if (region == NULL) {
		errno = ENOMEM;
		return -1;
	}

	/* Windows doesn't have direct equivalents for all madvise operations */
	/* We can implement some of them */

	switch (advice) {
	case 0:  /* MADV_NORMAL */
	case 1:  /* MADV_RANDOM */
	case 2:  /* MADV_SEQUENTIAL */
		/* No-op on Windows - these are hints */
		return 0;

	case 3:  /* MADV_WILLNEED */
		/* Prefetch pages - use VirtualLock to force into memory */
		if (!VirtualLock(addr, length)) {
			/* Not critical if this fails */
			return 0;
		}
		VirtualUnlock(addr, length);  /* Unlock immediately */
		return 0;

	case 4:  /* MADV_DONTNEED */
		/* Tell system we don't need these pages */
		/* On Windows, use VirtualAlloc with MEM_RESET */
		{
			SYSTEM_INFO si;
			GetSystemInfo(&si);
			size_t page_size = si.dwPageSize;

			/* Align to page boundaries */
			void *aligned_addr = (void *)(((uintptr_t)addr) & ~(page_size - 1));
			size_t aligned_length = (length + page_size - 1) & ~(page_size - 1);

			/* Reset pages */
			VirtualAlloc(aligned_addr, aligned_length, MEM_RESET, PAGE_NOACCESS);
		}
		return 0;

	case 8:  /* MADV_FREE */
		/* Similar to DONTNEED but keep dirty pages */
		return 0;  /* No direct equivalent */

	default:
		errno = EINVAL;
		return -1;
	}
}

/* mlock implementation */
int
nix_platform_win32_mlock(void *addr, size_t length)
{
	win32_mmap_init();

	if (!VirtualLock(addr, length)) {
		DWORD err = GetLastError();
		if (err == ERROR_WORKING_SET_QUOTA) {
			errno = ENOMEM;
		} else {
			errno = EPERM;
		}
		return -1;
	}

	return 0;
}

/* munlock implementation */
int
nix_platform_win32_munlock(void *addr, size_t length)
{
	win32_mmap_init();

	if (!VirtualUnlock(addr, length)) {
		errno = ENOMEM;
		return -1;
	}

	return 0;
}

/* mlockall implementation */
int
nix_platform_win32_mlockall(int flags)
{
	/* Windows doesn't support locking all memory */
	/* Would need to track all allocations and lock individually */
	errno = ENOSYS;
	return -1;
}

/* munlockall implementation */
int
nix_platform_win32_munlockall(void)
{
	/* Windows doesn't support unlocking all memory */
	errno = ENOSYS;
	return -1;
}

/* mincore implementation */
int
nix_platform_win32_mincore(void *addr, size_t length, unsigned char *vec)
{
	win32_mmap_init();

	if (vec == NULL) {
		errno = EFAULT;
		return -1;
	}

	SYSTEM_INFO si;
	GetSystemInfo(&si);
	size_t page_size = si.dwPageSize;
	size_t num_pages = (length + page_size - 1) / page_size;

	/* Query memory pages */
	for (size_t i = 0; i < num_pages; i++) {
		void *page_addr = (void *)((char *)addr + i * page_size);
		MEMORY_BASIC_INFORMATION mbi;

		if (VirtualQuery(page_addr, &mbi, sizeof(mbi)) == 0) {
			errno = ENOMEM;
			return -1;
		}

		/* Check if page is committed (in core) */
		vec[i] = (mbi.State == MEM_COMMIT) ? 1 : 0;
	}

	return 0;
}

/* Get page size */
size_t
nix_platform_win32_getpagesize(void)
{
	SYSTEM_INFO si;
	GetSystemInfo(&si);
	return si.dwPageSize;
}

#endif /* NIX_HOST_WIN32 */
