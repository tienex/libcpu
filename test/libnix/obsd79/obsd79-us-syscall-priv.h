#ifndef __obsd79_us_syscall_priv_h
#define __obsd79_us_syscall_priv_h

#include "nix-syscall.h"
#include "nix.h"

typedef struct _obsd79_us_syscall obsd79_us_syscall_t;

struct _obsd79_us_syscall {
	nix_us_syscall_if_vtbl_t const *vtbl;
	nix_env_t					   *env;
	uint32_t						last_param;
	nix_param_type_t				retype;
};

static __inline nix_env_t *
obsd79_us_syscall_get_nix_env(nix_us_syscall_if_t *xus)
{ return ((obsd79_us_syscall_t *)xus)->env; }

#endif  /* !__obsd79_us_syscall_priv_h */
