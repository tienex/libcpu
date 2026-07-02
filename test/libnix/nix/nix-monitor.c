/*
 * XEC - Optimizing Dynarec Engine
 *
 * Translation/Execution Monitor
 * Copyright (C) 2007 Orlando Bassotto. All rights reserved.
 * Copyright (C) 2007 Gianluca Guida. All rights reserved.
 *
 * $Id: xec-monitor.c 311 2007-06-30 23:21:47Z orlando $
 */

#include <stdio.h>
#include <stdlib.h>

#include "nix-monitor-priv.h"
#include "nix-host.h"
#include "nix-host.h"
#include "LibCPU/LcLog.h"

void *g_nix_mon_log = NULL;

nix_monitor_t *
nix_monitor_create(nix_guest_info_t const *guest_info,
                   nix_mem_if_t           *mem,
                   void                   *context,
                   nix_monitor_callback_t  callback)
{
	nix_monitor_t *xmon;

	xmon = nix_alloc_type(nix_monitor_t, 0);
	if (xmon != NULL) {
		xmon->mem = mem;
		xmon->opqctx = context;
		xmon->callback = callback;
		xmon->guest_info = *guest_info;
		xmon->target_version = NIX_VERSION_LATEST; /* nothing gated until a personality sets it */
	}

	return xmon;
}

nix_mem_if_t *
nix_monitor_get_memory(nix_monitor_t const *xmon)
{
	return xmon->mem;
}

void *
nix_monitor_get_context(nix_monitor_t const *xmon)
{
	return xmon->opqctx;
}

void
nix_monitor_event(nix_monitor_t     *xmon,
                  nix_cback_reason_t reason,
                  nix_param_t       *param1,
                  nix_param_t       *param2)
{
	// XMON_LOCK (xmon);

	(*(xmon->callback))(xmon, reason, param1, param2);

	// XMON_UNLOCK (xmon);
}

void
nix_monitor_get_guest_info(nix_monitor_t const *xmon,
                           nix_guest_info_t    *guest_info)
{
	*guest_info = xmon->guest_info;
}

void
nix_monitor_set_target_version(nix_monitor_t *xmon,
                               nix_version_t  version)
{
	xmon->target_version = (version == NIX_VERSION_NONE) ? NIX_VERSION_LATEST : version;
}

nix_version_t
nix_monitor_get_target_version(nix_monitor_t const *xmon)
{
	return xmon->target_version;
}
