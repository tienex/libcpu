#include "nix-host.h"
#include "LibCPU/LcLog.h"
#include "nix-syscall.h"

#include "obsd41-guest.h"

extern void *g_bsd_log;

void
obsd41_init(void)
{
	if (g_bsd_log == NULL)
		g_bsd_log = LCLogRegister("openbsd41");

	nix_init(256, 32);
}

static size_t
obsd41_us_syscall_get_max_params(void *_self)
{
	return 10;
}

extern nix_us_syscall_desc_t const *
obsd41_us_syscall_get(int scno);

static bool
obsd41_us_syscall_find(void *_self, int scno,
                       nix_us_syscall_desc_t const **desc)
{
	*desc = obsd41_us_syscall_get(scno);
	if (*desc == NULL)
		return (false);

	return (true);
}

static bool
obsd41_us_syscall_extract(void                         *_self,
                          nix_monitor_t                *xmon,
                          nix_us_syscall_desc_t const **desc)
{
	int                  scno;
	obsd41_us_syscall_t *self = (obsd41_us_syscall_t *)_self;

	obsd41_guest_get_syscall(self, xmon, &scno);

	*desc = obsd41_us_syscall_get(scno);
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
obsd41_us_syscall_get_retype(void *_self)
{
	obsd41_us_syscall_t *self = (obsd41_us_syscall_t *)_self;
	return self->retype;
}

void
obsd41_us_syscall_set_retype(void *_self, nix_param_type_t type)
{
	obsd41_us_syscall_t *self = (obsd41_us_syscall_t *)_self;
	self->retype = type;
}

static nix_us_syscall_if_vtbl_t const obsd41_us_syscall_impl_vtbl = {
    obsd41_us_syscall_get_max_params,
    obsd41_us_syscall_find,
    obsd41_us_syscall_extract,
    obsd41_guest_get_next_param,
    obsd41_guest_set_result,
    obsd41_us_syscall_get_retype,
    obsd41_us_syscall_set_retype};

nix_us_syscall_if_t *
obsd41_us_syscall_create(nix_mem_if_t *memif)
{
	obsd41_us_syscall_t *sc;

	obsd41_init();

	sc = nix_alloc_type(obsd41_us_syscall_t, 0);
	if (sc != NULL) {
		sc->vtbl = &obsd41_us_syscall_impl_vtbl;
		sc->env = nix_env_create(memif);
		sc->last_param = 0;
		sc->retype = NIX_PARAM_INVALID;
	}
	return (nix_us_syscall_if_t *)sc;
}
