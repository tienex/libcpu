#ifndef __nix_host_h
#define __nix_host_h

/*
 * LibCPU-native runtime support for libnix -- the replacement for the former xec-compat shim.
 *
 * Self-contained, portable C with no dependency on the LibCPU framework, so the nix core still
 * cross-builds standalone for the non-host targets (win32/haiku/os2/vms). It provides exactly what
 * the nix core needs: a guest-memory interface, zeroing allocation, host page allocation, a host
 * trap hook, and logging.
 */

#include <sys/types.h>
#include <stddef.h>
#include <stdarg.h>
#include <inttypes.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

/* ---- addressing (were nix_gaddr_t / nix_haddr_t) ---- */
typedef uintptr_t     nix_haddr_t;  /* a host address (native pointer width) */
typedef uint64_t      nix_gaddr_t;  /* a guest address */
typedef unsigned long nix_memflg_t; /* memory-access result/permission flags */

/*
 * The guest-memory interface (was nix_mem_if_t): a small vtable the host (lcx, a test harness)
 * implements so the nix_ layer can reach guest memory. lcx supplies a flat-RAM implementation.
 */
typedef struct _nix_mem_if nix_mem_if_t;

struct _nix_mem_if_vtbl {
	nix_gaddr_t (*gmap)(nix_mem_if_t *self, nix_haddr_t addr, size_t len, unsigned flags);
	nix_gaddr_t (*htog)(nix_mem_if_t *self, nix_haddr_t addr, nix_memflg_t *mf);
	nix_haddr_t (*gtoh)(nix_mem_if_t *self, nix_gaddr_t addr, nix_memflg_t *mf);
	nix_memflg_t (*read)(nix_mem_if_t *self, nix_gaddr_t gaddr, uint8_t *buf, size_t sz);
	nix_memflg_t (*write)(nix_mem_if_t *self, nix_gaddr_t gaddr, uint8_t const *buf, size_t sz);
};

struct _nix_mem_if {
	struct _nix_mem_if_vtbl const *vtbl;
};

#define nix_mem_gmap(self, ...)      (self)->vtbl->gmap((self), __VA_ARGS__)
#define nix_mem_htog(self, addr, mf) (self)->vtbl->htog((self), (addr), (mf))
#define nix_mem_gtoh(self, addr, mf) (self)->vtbl->gtoh((self), (addr), (mf))
#define nix_mem_read(self, a, b, n)  (self)->vtbl->read((self), (a), (b), (n))
#define nix_mem_write(self, a, b, n) (self)->vtbl->write((self), (a), (b), (n))

/* ---- allocation (were nix_alloc_type / _ntype / nix_free); always zeroed ---- */
#define NIX_MEM_ZERO                    0x1
#define nix_alloc(size, flags)          calloc(1, (size))
#define nix_alloc_type(type, flags)     ((type *)calloc(1, sizeof(type)))
#define nix_alloc_ntype(type, n, flags) ((type *)calloc((size_t)(n), sizeof(type)))
#define nix_free(ptr)                   free(ptr)

#ifndef NIX_MIN
#define NIX_MIN(a, b) ((a) < (b) ? (a) : (b))
#define NIX_MAX(a, b) ((a) > (b) ? (a) : (b))
#endif

/* ---- host page allocation (was xec_mmap_*): the backing store for nix_mmap ---- */
typedef struct _nix_host_mmap nix_host_mmap_t;

#define NIX_HOST_MMAP_READ   0x1
#define NIX_HOST_MMAP_WRITE  0x2
#define NIX_HOST_MMAP_EXEC   0x4
#define NIX_HOST_MMAP_SHARED 0x8

nix_host_mmap_t *nix_host_mmap_create(size_t size, unsigned flags);
void            *nix_host_mmap_bytes(nix_host_mmap_t const *mm);
void             nix_host_mmap_free(nix_host_mmap_t *mm);

/* ---- host trap hook (was nix_xcpt_set_handler): a SIGSEGV/SIGBUS handler for guest faults ---- */
typedef int (*nix_xcpt_handler_t)(int signo, void *context);

nix_xcpt_handler_t nix_xcpt_set_handler(nix_xcpt_handler_t handler);

/* ---- logging: LibCPU's shared logging API (LCLogImpl / LCLog / LCAssert / LCBugCheck) ---- */
#include "LibCPU/LcLog.h"

#endif /* !__nix_host_h */
