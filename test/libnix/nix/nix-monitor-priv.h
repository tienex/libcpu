#ifndef __nix_monitor_priv_h
#define __nix_monitor_priv_h

#include "nix-host.h"
#include "nix-syscall.h"

#if 0
#define XEC_MONITOR_CACHELINE_COUNT 32

typedef struct _nix_monitor_cachelines
  {
    union {
      uintmax_t addr64;
      struct {
        uint32_t addr32_h;
        uint32_t addr32_l;
      };
    } addr[XEC_MONITOR_CACHELINE_COUNT];
    union {
      uintmax_t value64;
      struct {
        uint32_t value32_h;
        uint32_t value32_l;
      };
    } value[XEC_MONITOR_CACHELINE_COUNT];
    size_t    size[XEC_MONITOR_CACHELINE_COUNT];
  } xec_monitor_cachelines_t;

static __inline int __nix_monitor_cacheline_hash (uintmax_t p)
{ return ( (p >> 3) ^ p) & (XEC_MONITOR_CACHELINE_COUNT - 1); }
#endif

typedef struct _nix_cback_env {
	nix_cback_reason_t reason;
	nix_param_t        param1;
	nix_param_t        param2;
} nix_cback_env_t;

struct _nix_monitor {
	void         *opqctx;
	nix_mem_if_t *mem;

	nix_monitor_callback_t callback;
	nix_cback_env_t        cbe;
	nix_guest_info_t       guest_info;
#if 0
    xec_monitor_cachelines_t cache;
#endif
};

#endif /* !__nix_monitor_priv_h */
