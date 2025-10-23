/*
 * nix-platform-win32-mmap-prot.c
 *
 * Memory protection handling for mismatched guest/host page sizes
 *
 * Handles the case where host pages are larger than guest pages by:
 * - Tracking protection at guest page granularity
 * - Maintaining host page protection as union of guest sub-pages
 * - Supporting partial page protection changes
 *
 * Example: Host has 64KB pages, guest has 4KB pages
 * - Single 64KB host page contains 16 guest pages
 * - Each guest page can have different protection
 * - Host page protection = most permissive of all 16 guest pages
 *
 * Compatible with Windows NT 3.1 through Windows 11
 */

#if defined(NIX_HOST_WIN32)

#include "nix-platform-win32.h"
#include <windows.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

/* Default page sizes */
#ifndef GUEST_PAGE_SIZE
#define GUEST_PAGE_SIZE 4096    /* 4KB - common for x86/ARM guest */
#endif

#ifndef HOST_PAGE_SIZE
#define HOST_PAGE_SIZE 65536    /* 64KB - common for ARM64 Windows */
#endif

/* Protection bits (standard PROT_* values) */
#ifndef PROT_NONE
#define PROT_NONE  0x0
#define PROT_READ  0x1
#define PROT_WRITE 0x2
#define PROT_EXEC  0x4
#endif

/* Maximum number of guest pages per host page */
#define MAX_GUEST_PAGES_PER_HOST (HOST_PAGE_SIZE / GUEST_PAGE_SIZE)

/* Protection tracking entry for a host page */
typedef struct prot_entry {
	uintptr_t host_page_addr;           /* Host page base address */
	int guest_prot[MAX_GUEST_PAGES_PER_HOST];  /* Protection for each guest sub-page */
	int host_prot;                      /* Current host page protection */
	size_t guest_page_size;             /* Guest page size */
	size_t host_page_size;              /* Host page size */
	struct prot_entry *next;            /* Next in hash chain */
} prot_entry_t;

/* Hash table for protection tracking */
#define PROT_HASH_SIZE 4096
static prot_entry_t *g_prot_hash[PROT_HASH_SIZE];
static CRITICAL_SECTION g_prot_lock;
static int g_prot_init = 0;

/* Page size configuration */
static size_t g_guest_page_size = GUEST_PAGE_SIZE;
static size_t g_host_page_size = HOST_PAGE_SIZE;
static size_t g_alloc_granularity = 65536;  /* Windows allocation granularity */

/* Statistics */
static struct {
	size_t total_mappings;
	size_t total_protections;
	size_t subpage_protections;
	size_t host_prot_changes;
} g_prot_stats = {0};

/*
 * Initialize protection tracking subsystem
 */
static void init_prot_tracking(void)
{
	if (!g_prot_init) {
		InitializeCriticalSection(&g_prot_lock);
		memset(g_prot_hash, 0, sizeof(g_prot_hash));

		/* Get actual system page size and allocation granularity */
		SYSTEM_INFO si;
		GetSystemInfo(&si);

		g_host_page_size = si.dwPageSize;
		g_alloc_granularity = si.dwAllocationGranularity;

		/* Guest page size defaults to 4KB, can be configured */
		if (g_guest_page_size == 0 || g_guest_page_size > g_host_page_size) {
			g_guest_page_size = g_host_page_size;
		}

		g_prot_init = 1;
	}
}

/*
 * Hash function for address lookup
 */
static unsigned int prot_hash(uintptr_t addr)
{
	/* Use upper bits for better distribution */
	return ((unsigned int)(addr >> 16)) % PROT_HASH_SIZE;
}

/*
 * Find protection entry for host page
 */
static prot_entry_t *find_prot_entry(uintptr_t host_page_addr)
{
	unsigned int hash = prot_hash(host_page_addr);
	prot_entry_t *entry = g_prot_hash[hash];

	while (entry) {
		if (entry->host_page_addr == host_page_addr) {
			return entry;
		}
		entry = entry->next;
	}

	return NULL;
}

/*
 * Create new protection entry
 */
static prot_entry_t *create_prot_entry(uintptr_t host_page_addr, int initial_prot)
{
	prot_entry_t *entry = (prot_entry_t *)malloc(sizeof(prot_entry_t));
	if (!entry) {
		return NULL;
	}

	entry->host_page_addr = host_page_addr;
	entry->guest_page_size = g_guest_page_size;
	entry->host_page_size = g_host_page_size;
	entry->host_prot = initial_prot;

	/* Initialize all guest sub-pages with same protection */
	size_t num_guest_pages = g_host_page_size / g_guest_page_size;
	for (size_t i = 0; i < num_guest_pages && i < MAX_GUEST_PAGES_PER_HOST; i++) {
		entry->guest_prot[i] = initial_prot;
	}

	/* Add to hash table */
	unsigned int hash = prot_hash(host_page_addr);
	entry->next = g_prot_hash[hash];
	g_prot_hash[hash] = entry;

	return entry;
}

/*
 * Remove protection entry
 */
static void remove_prot_entry(uintptr_t host_page_addr)
{
	unsigned int hash = prot_hash(host_page_addr);
	prot_entry_t **prev = &g_prot_hash[hash];
	prot_entry_t *entry = g_prot_hash[hash];

	while (entry) {
		if (entry->host_page_addr == host_page_addr) {
			*prev = entry->next;
			free(entry);
			return;
		}
		prev = &entry->next;
		entry = entry->next;
	}
}

/*
 * Calculate required host protection from guest sub-page protections
 *
 * Host protection must be permissive enough for all guest sub-pages.
 * Returns the union (most permissive) of all guest protections.
 */
static int calculate_host_protection(prot_entry_t *entry)
{
	int host_prot = PROT_NONE;
	size_t num_guest_pages = entry->host_page_size / entry->guest_page_size;

	for (size_t i = 0; i < num_guest_pages && i < MAX_GUEST_PAGES_PER_HOST; i++) {
		host_prot |= entry->guest_prot[i];
	}

	return host_prot;
}

/*
 * Convert PROT_* flags to Windows PAGE_* flags
 */
static DWORD prot_to_windows(int prot)
{
	if (prot & PROT_EXEC) {
		if (prot & PROT_WRITE) {
			return PAGE_EXECUTE_READWRITE;
		} else if (prot & PROT_READ) {
			return PAGE_EXECUTE_READ;
		} else {
			return PAGE_EXECUTE;
		}
	} else if (prot & PROT_WRITE) {
		return PAGE_READWRITE;
	} else if (prot & PROT_READ) {
		return PAGE_READONLY;
	} else {
		return PAGE_NOACCESS;
	}
}

/*
 * Set guest page size (must be called before any mmap operations)
 */
void nix_platform_win32_mmap_set_guest_pagesize(size_t pagesize)
{
	init_prot_tracking();

	EnterCriticalSection(&g_prot_lock);

	if (pagesize > 0 && pagesize <= g_host_page_size) {
		/* Must be power of 2 */
		if ((pagesize & (pagesize - 1)) == 0) {
			g_guest_page_size = pagesize;
		}
	}

	LeaveCriticalSection(&g_prot_lock);
}

/*
 * Get guest page size
 */
size_t nix_platform_win32_mmap_get_guest_pagesize(void)
{
	init_prot_tracking();
	return g_guest_page_size;
}

/*
 * Get host page size
 */
size_t nix_platform_win32_mmap_get_host_pagesize(void)
{
	init_prot_tracking();
	return g_host_page_size;
}

/*
 * Get allocation granularity
 */
size_t nix_platform_win32_mmap_get_alloc_granularity(void)
{
	init_prot_tracking();
	return g_alloc_granularity;
}

/*
 * Track mmap region with initial protection
 *
 * Called after successful VirtualAlloc/MapViewOfFile.
 * Sets up protection tracking for the mapped region.
 */
int nix_platform_win32_mmap_track_region(void *addr, size_t length, int prot)
{
	if (!addr || length == 0) {
		errno = EINVAL;
		return -1;
	}

	init_prot_tracking();

	EnterCriticalSection(&g_prot_lock);

	/* Round to host page boundaries */
	uintptr_t start = (uintptr_t)addr & ~(g_host_page_size - 1);
	uintptr_t end = ((uintptr_t)addr + length + g_host_page_size - 1) & ~(g_host_page_size - 1);

	/* Create protection entries for each host page */
	for (uintptr_t page = start; page < end; page += g_host_page_size) {
		prot_entry_t *entry = find_prot_entry(page);
		if (!entry) {
			entry = create_prot_entry(page, prot);
			if (!entry) {
				LeaveCriticalSection(&g_prot_lock);
				errno = ENOMEM;
				return -1;
			}
		} else {
			/* Update existing entry */
			size_t num_guest_pages = g_host_page_size / g_guest_page_size;
			for (size_t i = 0; i < num_guest_pages && i < MAX_GUEST_PAGES_PER_HOST; i++) {
				entry->guest_prot[i] = prot;
			}
			entry->host_prot = prot;
		}
	}

	g_prot_stats.total_mappings++;

	LeaveCriticalSection(&g_prot_lock);

	return 0;
}

/*
 * Untrack mmap region
 *
 * Called before munmap to remove protection tracking.
 */
void nix_platform_win32_mmap_untrack_region(void *addr, size_t length)
{
	if (!addr || length == 0) {
		return;
	}

	init_prot_tracking();

	EnterCriticalSection(&g_prot_lock);

	/* Round to host page boundaries */
	uintptr_t start = (uintptr_t)addr & ~(g_host_page_size - 1);
	uintptr_t end = ((uintptr_t)addr + length + g_host_page_size - 1) & ~(g_host_page_size - 1);

	/* Remove protection entries */
	for (uintptr_t page = start; page < end; page += g_host_page_size) {
		remove_prot_entry(page);
	}

	LeaveCriticalSection(&g_prot_lock);
}

/*
 * Change protection with guest page granularity
 *
 * This is the key function that handles sub-page protection changes.
 * Updates guest page protections and recalculates host page protection.
 */
int nix_platform_win32_mprotect_subpage(void *addr, size_t length, int prot)
{
	if (!addr || length == 0) {
		errno = EINVAL;
		return -1;
	}

	init_prot_tracking();

	EnterCriticalSection(&g_prot_lock);

	/* Calculate guest page range */
	uintptr_t guest_start = (uintptr_t)addr;
	uintptr_t guest_end = guest_start + length;

	/* Process each affected host page */
	uintptr_t host_page_start = guest_start & ~(g_host_page_size - 1);
	uintptr_t host_page_end = (guest_end + g_host_page_size - 1) & ~(g_host_page_size - 1);

	for (uintptr_t host_page = host_page_start; host_page < host_page_end; host_page += g_host_page_size) {
		/* Find or create protection entry */
		prot_entry_t *entry = find_prot_entry(host_page);
		if (!entry) {
			entry = create_prot_entry(host_page, prot);
			if (!entry) {
				LeaveCriticalSection(&g_prot_lock);
				errno = ENOMEM;
				return -1;
			}
		}

		/* Update guest sub-page protections */
		uintptr_t page_end = host_page + g_host_page_size;
		uintptr_t range_start = (guest_start > host_page) ? guest_start : host_page;
		uintptr_t range_end = (guest_end < page_end) ? guest_end : page_end;

		size_t start_idx = (range_start - host_page) / g_guest_page_size;
		size_t end_idx = (range_end - host_page + g_guest_page_size - 1) / g_guest_page_size;

		for (size_t i = start_idx; i < end_idx && i < MAX_GUEST_PAGES_PER_HOST; i++) {
			entry->guest_prot[i] = prot;
		}

		/* Recalculate host page protection */
		int new_host_prot = calculate_host_protection(entry);

		/* Apply to host page if changed */
		if (new_host_prot != entry->host_prot) {
			DWORD win_prot = prot_to_windows(new_host_prot);
			DWORD old_prot;

			if (!VirtualProtect((void *)host_page, g_host_page_size, win_prot, &old_prot)) {
				LeaveCriticalSection(&g_prot_lock);
				errno = EACCES;
				return -1;
			}

			entry->host_prot = new_host_prot;
			g_prot_stats.host_prot_changes++;
		}

		if (range_start != host_page || range_end != page_end) {
			g_prot_stats.subpage_protections++;
		}
	}

	g_prot_stats.total_protections++;

	LeaveCriticalSection(&g_prot_lock);

	return 0;
}

/*
 * Get protection for address (at guest page granularity)
 */
int nix_platform_win32_mmap_get_protection(void *addr)
{
	if (!addr) {
		errno = EINVAL;
		return -1;
	}

	init_prot_tracking();

	EnterCriticalSection(&g_prot_lock);

	uintptr_t host_page = (uintptr_t)addr & ~(g_host_page_size - 1);
	prot_entry_t *entry = find_prot_entry(host_page);

	if (!entry) {
		LeaveCriticalSection(&g_prot_lock);
		errno = EINVAL;
		return -1;
	}

	/* Find guest sub-page index */
	size_t offset = (uintptr_t)addr - host_page;
	size_t guest_idx = offset / g_guest_page_size;

	int prot = PROT_NONE;
	if (guest_idx < MAX_GUEST_PAGES_PER_HOST) {
		prot = entry->guest_prot[guest_idx];
	}

	LeaveCriticalSection(&g_prot_lock);

	return prot;
}

/*
 * Check if address range has uniform protection
 */
int nix_platform_win32_mmap_check_uniform_protection(void *addr, size_t length, int *prot_out)
{
	if (!addr || length == 0 || !prot_out) {
		errno = EINVAL;
		return -1;
	}

	init_prot_tracking();

	EnterCriticalSection(&g_prot_lock);

	uintptr_t start = (uintptr_t)addr;
	uintptr_t end = start + length;

	/* Get protection of first guest page */
	uintptr_t host_page = start & ~(g_host_page_size - 1);
	prot_entry_t *entry = find_prot_entry(host_page);

	if (!entry) {
		LeaveCriticalSection(&g_prot_lock);
		errno = EINVAL;
		return -1;
	}

	size_t offset = start - host_page;
	size_t guest_idx = offset / g_guest_page_size;
	int first_prot = (guest_idx < MAX_GUEST_PAGES_PER_HOST) ? entry->guest_prot[guest_idx] : PROT_NONE;

	/* Check all guest pages in range */
	for (uintptr_t addr_check = start; addr_check < end; addr_check += g_guest_page_size) {
		host_page = addr_check & ~(g_host_page_size - 1);
		entry = find_prot_entry(host_page);

		if (!entry) {
			LeaveCriticalSection(&g_prot_lock);
			errno = EINVAL;
			return -1;
		}

		offset = addr_check - host_page;
		guest_idx = offset / g_guest_page_size;

		int current_prot = (guest_idx < MAX_GUEST_PAGES_PER_HOST) ? entry->guest_prot[guest_idx] : PROT_NONE;

		if (current_prot != first_prot) {
			LeaveCriticalSection(&g_prot_lock);
			*prot_out = -1;  /* Non-uniform */
			return 0;
		}
	}

	LeaveCriticalSection(&g_prot_lock);

	*prot_out = first_prot;
	return 0;
}

/*
 * Get protection tracking statistics
 */
void nix_platform_win32_mmap_get_stats(
	size_t *total_mappings,
	size_t *total_protections,
	size_t *subpage_protections,
	size_t *host_prot_changes
)
{
	init_prot_tracking();

	EnterCriticalSection(&g_prot_lock);

	if (total_mappings) *total_mappings = g_prot_stats.total_mappings;
	if (total_protections) *total_protections = g_prot_stats.total_protections;
	if (subpage_protections) *subpage_protections = g_prot_stats.subpage_protections;
	if (host_prot_changes) *host_prot_changes = g_prot_stats.host_prot_changes;

	LeaveCriticalSection(&g_prot_lock);
}

/*
 * Dump protection map (for debugging)
 */
void nix_platform_win32_mmap_dump_protection_map(void)
{
	init_prot_tracking();

	EnterCriticalSection(&g_prot_lock);

	printf("Protection Map (Guest page size: %zu, Host page size: %zu):\n",
		   g_guest_page_size, g_host_page_size);

	size_t total_entries = 0;

	for (int i = 0; i < PROT_HASH_SIZE; i++) {
		prot_entry_t *entry = g_prot_hash[i];
		while (entry) {
			printf("  Host page 0x%p (prot=%d):\n", (void *)entry->host_page_addr, entry->host_prot);

			size_t num_guest_pages = entry->host_page_size / entry->guest_page_size;
			for (size_t j = 0; j < num_guest_pages && j < MAX_GUEST_PAGES_PER_HOST; j++) {
				uintptr_t guest_addr = entry->host_page_addr + j * entry->guest_page_size;
				printf("    Guest page 0x%p: prot=%d\n", (void *)guest_addr, entry->guest_prot[j]);
			}

			total_entries++;
			entry = entry->next;
		}
	}

	printf("Total entries: %zu\n", total_entries);
	printf("Statistics:\n");
	printf("  Total mappings: %zu\n", g_prot_stats.total_mappings);
	printf("  Total protections: %zu\n", g_prot_stats.total_protections);
	printf("  Sub-page protections: %zu\n", g_prot_stats.subpage_protections);
	printf("  Host prot changes: %zu\n", g_prot_stats.host_prot_changes);

	LeaveCriticalSection(&g_prot_lock);
}

#endif /* NIX_HOST_WIN32 */
