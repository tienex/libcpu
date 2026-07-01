#include "nix-host.h"
#include "LibCPU/LcLog.h"
#include "nix-syscall.h"

#include "nbsd101-guest.h"

extern void *g_bsd_log;

void
nbsd101_init(void)
{
	if (g_bsd_log == NULL)
		g_bsd_log = LCLogRegister("netbsd101");

	nix_init(256, 32);
}

static size_t
nbsd101_us_syscall_get_max_params(void *_self)
{
	return 10;
}

extern nix_us_syscall_desc_t const *
nbsd101_us_syscall_get(int scno);

static bool
nbsd101_us_syscall_find(void *_self, int scno,
                       nix_us_syscall_desc_t const **desc)
{
	*desc = nbsd101_us_syscall_get(scno);
	if (*desc == NULL)
		return (false);

	return (true);
}

static bool
nbsd101_us_syscall_extract(void                         *_self,
                          nix_monitor_t                *xmon,
                          nix_us_syscall_desc_t const **desc)
{
	int                  scno;
	nbsd101_us_syscall_t *self = (nbsd101_us_syscall_t *)_self;

	nbsd101_guest_get_syscall(self, xmon, &scno);

	*desc = nbsd101_us_syscall_get(scno);
	if (*desc == NULL) {
		LCLog(g_bsd_log, LCLogFatal, 0,
		      "system call %d descriptor not found", scno);
		return (false);
	}

	/* Reset last param */
	self->last_param = 0;

	return (true);
}

static nix_param_type_t
nbsd101_us_syscall_get_retype(void *_self)
{
	nbsd101_us_syscall_t *self = (nbsd101_us_syscall_t *)_self;
	return self->retype;
}

void
nbsd101_us_syscall_set_retype(void *_self, nix_param_type_t type)
{
	nbsd101_us_syscall_t *self = (nbsd101_us_syscall_t *)_self;
	self->retype = type;
}

static nix_us_syscall_if_vtbl_t const nbsd101_us_syscall_impl_vtbl = {
    nbsd101_us_syscall_get_max_params,
    nbsd101_us_syscall_find,
    nbsd101_us_syscall_extract,
    nbsd101_guest_get_next_param,
    nbsd101_guest_set_result,
    nbsd101_us_syscall_get_retype,
    nbsd101_us_syscall_set_retype};

nix_us_syscall_if_t *
nbsd101_us_syscall_create(nix_mem_if_t *memif)
{
	nbsd101_us_syscall_t *sc;

	nbsd101_init();

	sc = nix_alloc_type(nbsd101_us_syscall_t, 0);
	if (sc != NULL) {
		sc->vtbl = &nbsd101_us_syscall_impl_vtbl;
		sc->env = nix_env_create(memif);
		sc->last_param = 0;
		sc->retype = NIX_PARAM_INVALID;
	}
	return (nix_us_syscall_if_t *)sc;
}
