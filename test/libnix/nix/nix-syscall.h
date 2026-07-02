#ifndef __nix_syscall_h
#define __nix_syscall_h

/*
 * LibCPU-native syscall-personality ABI for libnix -- the replacement for xec-compat's engine
 * headers (xec-param.h / xec-us-syscall-if.h / xec-monitor.h).
 *
 * A personality (UNIX V6, V7, System III, ...) implements nix_us_syscall_if_t; the sc2int tool
 * compiles a .sc table into a nix_us_syscall_desc_t[] dispatched through it. The monitor is the
 * per-process handle a personality marshals arguments against (it owns the guest memory interface).
 */

#include "nix-host.h"    /* nix_mem_if_t, nix_gaddr_t, the base integer types */
#include "nix-version.h" /* nix_version_t for per-call version gating */

/* ---- a marshalled syscall parameter (was xec_param_t) ---- */
typedef enum _nix_param_type {
	NIX_PARAM_INVALID = 0,

	NIX_PARAM_BYTE,
	NIX_PARAM_HALF,
	NIX_PARAM_WORD,
	NIX_PARAM_DWORD,
	NIX_PARAM_SINGLE,
	NIX_PARAM_DOUBLE,
	NIX_PARAM_EXTENDED,
	NIX_PARAM_VECTOR,
	NIX_PARAM_POINTER,
	NIX_PARAM_INTPTR /* guest-dependent width */
} nix_param_type_t;

typedef struct _nix_param {
	nix_param_type_t type;
	union {
		union {
#ifdef __LITTLE_ENDIAN__
			union {
				int64_t s8 : 8;
				int64_t : 56;
			};
			union {
				int64_t s16 : 16;
				int64_t : 48;
			};
			union {
				int64_t s32 : 32;
				int64_t : 32;
			};
#else
			union {
				int64_t : 56;
				int64_t s8 : 8;
			};
			union {
				int64_t : 48;
				int64_t s16 : 16;
			};
			union {
				int64_t : 32;
				int64_t s32 : 32;
			};
#endif
			int64_t s64;
		} tsign;

		union {
#ifdef __LITTLE_ENDIAN__
			union {
				uint64_t u8 : 8;
				uint64_t : 56;
			};
			union {
				uint64_t u16 : 16;
				uint64_t : 48;
			};
			union {
				uint64_t u32 : 32;
				uint64_t : 32;
			};
#else
			union {
				uint64_t : 56;
				uint64_t u8 : 8;
			};
			union {
				uint64_t : 48;
				uint64_t u16 : 16;
			};
			union {
				uint64_t : 32;
				uint64_t u32 : 32;
			};
#endif
			uint64_t u64;
		} tnosign;

		union {
			float       s;
			double      d;
			long double x;
		} fp;

		char  vector[16];
		void *pointer;
	} value;
} nix_param_t;

/* ---- guest description (was xec_guest_info_t / xec_endian_t) ---- */
typedef enum _nix_endian {
    NIX_ENDIAN_LITTLE,
    NIX_ENDIAN_BIG
} nix_endian_t;

#ifdef __LITTLE_ENDIAN__
#define NIX_ENDIAN_NATIVE NIX_ENDIAN_LITTLE
#else
#define NIX_ENDIAN_NATIVE NIX_ENDIAN_BIG
#endif

typedef struct _nix_guest_info {
    char const  *name;
    nix_endian_t endian;
    uint32_t     byte_size;
    uint32_t     word_size;
    uint32_t     page_size;
} nix_guest_info_t;

/* ---- the monitor (was xec_monitor_t): the per-process handle a personality dispatches against ---- */
typedef struct _nix_monitor nix_monitor_t;

typedef enum _nix_cback_reason {
	NIX_CBACK_TRAP
} nix_cback_reason_t;

typedef void (*nix_monitor_callback_t)(nix_monitor_t *mon, nix_cback_reason_t reason,
                                       nix_param_t *param1, nix_param_t *param2);

nix_monitor_t *nix_monitor_create (nix_guest_info_t const *guest_info, nix_mem_if_t *mem,
                                   void *context, nix_monitor_callback_t callback);
nix_mem_if_t  *nix_monitor_get_memory (nix_monitor_t const *mon);
void          *nix_monitor_get_context (nix_monitor_t const *mon);
void           nix_monitor_get_guest_info (nix_monitor_t const *mon, nix_guest_info_t *guest_info);
/* The guest-OS version being emulated -- the syscall dispatcher gates each call against it.
 * Defaults to NIX_VERSION_LATEST (nothing gated) until a personality sets it. */
void          nix_monitor_set_target_version (nix_monitor_t *mon, nix_version_t version);
nix_version_t nix_monitor_get_target_version (nix_monitor_t const *mon);
void           nix_monitor_event (nix_monitor_t *mon, nix_cback_reason_t reason,
                                  nix_param_t *param1, nix_param_t *param2);

/* ---- the user-space syscall interface (was xec_us_syscall_if_t) ---- */
typedef struct _nix_us_syscall_if nix_us_syscall_if_t;

typedef int (*nix_us_syscall_callback_t)(nix_us_syscall_if_t *us,
                                         nix_monitor_t       *mon,
                                         nix_param_t const   *args,
                                         nix_param_t         *retval);

typedef struct _nix_us_syscall_desc {
	int         number;
	char const *name;
	char const *format;
	char const *rettype;
	unsigned    flags;
#define NIX_US_SYSCALL_VARIADIC 1
	size_t                    nparams;
	nix_us_syscall_callback_t callback;
	/* Version range this call exists in; placed last so a legacy 7-field descriptor initializer
	 * zero-fills both to NONE (= always available), keeping the gate backward compatible. */
	nix_version_t             since;    /* first guest-OS version this call exists in (NONE = from the start) */
	nix_version_t             until;    /* first version it is GONE (NONE = never removed) */
} nix_us_syscall_desc_t;

typedef struct _nix_us_syscall_if_vtbl nix_us_syscall_if_vtbl_t;

struct _nix_us_syscall_if_vtbl {
	size_t (*get_max_params)(void *self);
	bool (*find)(void *self, int scno, nix_us_syscall_desc_t const **desc);
	bool (*extract)(void *self, nix_monitor_t *mon, nix_us_syscall_desc_t const **desc);
	int (*get_next_param)(void *self, nix_monitor_t *mon, unsigned flags,
	                      nix_param_type_t type, nix_param_t *param);
	void (*set_result)(void *self, nix_monitor_t *mon, int error, nix_param_t const *result);
	nix_param_type_t (*get_retype)(void *self);
	void (*set_retype)(void *self, nix_param_type_t type);
};

struct _nix_us_syscall_if {
	struct _nix_us_syscall_if_vtbl const *vtbl;
};

/* True iff this call exists in the given target version: since <= target && (until == NONE || target < until). */
bool nix_us_syscall_desc_available (nix_us_syscall_desc_t const *desc, nix_version_t target);

/* The dispatch driver (in nix-us-syscall.c): run one syscall, or re-enter for an indirect call. */
void nix_us_syscall_dispatch (nix_us_syscall_if_t *us, nix_monitor_t *mon);
int  nix_us_syscall_redispatch (nix_us_syscall_if_t *us, nix_monitor_t *mon, int scno,
                                nix_param_t const *params, void *result);

#define nix_us_syscall_get_max_params(self)                 (self)->vtbl->get_max_params(self)
#define nix_us_syscall_find(self, scno, desc)               (self)->vtbl->find(self, scno, desc)
#define nix_us_syscall_extract(self, mon, desc)             (self)->vtbl->extract(self, mon, desc)
#define nix_us_syscall_get_next_param(self, mon, fl, t, p)  (self)->vtbl->get_next_param(self, mon, fl, t, p)
#define nix_us_syscall_set_result(self, mon, error, result) (self)->vtbl->set_result(self, mon, error, result)
#define nix_us_syscall_get_retype(self)                     (self)->vtbl->get_retype(self)
#define nix_us_syscall_set_retype(self, type)               (self)->vtbl->set_retype(self, type)

#endif /* !__nix_syscall_h */
