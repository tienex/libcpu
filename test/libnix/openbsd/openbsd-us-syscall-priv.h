#ifndef __openbsd_us_syscall_priv_h
#define __openbsd_us_syscall_priv_h

#include "nix-syscall.h"
#include "nix.h"

typedef struct _openbsd_us_syscall openbsd_us_syscall_t;

struct _openbsd_us_syscall {
	nix_us_syscall_if_vtbl_t const *vtbl;
	nix_env_t					   *env;
	uint32_t						last_param;
	nix_param_type_t				retype;
};

static __inline nix_env_t *
openbsd_us_syscall_get_nix_env(nix_us_syscall_if_t *xus)
{ return ((openbsd_us_syscall_t *)xus)->env; }

#endif  /* !__openbsd_us_syscall_priv_h */
