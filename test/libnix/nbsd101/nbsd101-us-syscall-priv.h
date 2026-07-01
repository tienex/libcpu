#ifndef __nbsd101_us_syscall_priv_h
#define __nbsd101_us_syscall_priv_h

#include "nix-syscall.h"
#include "nix.h"

typedef struct _nbsd101_us_syscall nbsd101_us_syscall_t;

struct _nbsd101_us_syscall {
	nix_us_syscall_if_vtbl_t const *vtbl;
	nix_env_t					   *env;
	uint32_t						last_param;
	nix_param_type_t				retype;
};

static __inline nix_env_t *
nbsd101_us_syscall_get_nix_env(nix_us_syscall_if_t *xus)
{ return ((nbsd101_us_syscall_t *)xus)->env; }

#endif  /* !__nbsd101_us_syscall_priv_h */
