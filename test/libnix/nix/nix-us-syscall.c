/*
 * XEC - Optimizing Dynarec Engine
 *
 * Userspace System Call
 * Copyright (C) 2007 Orlando Bassotto. All rights reserved.
 *
 * $Id: xec-us-syscall.c 311 2007-06-30 23:21:47Z orlando $
 */
#include <stdlib.h>
#include <string.h>
#include <errno.h>

#include "nix-syscall.h"
#include "nix-host.h"
#include "LibCPU/LcLog.h"

void *g_nix_us_log = NULL;

bool
nix_us_syscall_desc_available(nix_us_syscall_desc_t const *desc,
                              nix_version_t                target)
{
	if (desc->since != NIX_VERSION_NONE && target < desc->since) {
		return false;
	}
	if (desc->until != NIX_VERSION_NONE && target >= desc->until) {
		return false;
	}
	return true;
}

void
nix_us_syscall_dispatch(nix_us_syscall_if_t *xus,
                        nix_monitor_t       *xmon)
{
	nix_param_t                  result;
	int                          err = 0;
	size_t                       n = 0;
	size_t                       nparams = 0;
	char const                  *fmt = NULL;
	nix_us_syscall_desc_t const *desc = NULL;
	nix_param_t                 *params = NULL;

	/* Extract the syscall */
	if (!nix_us_syscall_extract(xus, xmon, &desc)) {
		LCLog(g_nix_us_log, LCLogFatal, 0, "cannot extract system call number from guest context.", 0);

		err = ENOSYS;
		goto error;
	}

	if (desc->callback == NULL) {
		LCLog(g_nix_us_log, LCLogFatal, 0, "system call descriptor callback is NULL.", 0);

		err = ENOSYS;
		goto error;
	}

	if (!nix_us_syscall_desc_available(desc, nix_monitor_get_target_version(xmon))) {
		LCLog(g_nix_us_log, LCLogFatal, 0,
		      "system call \"%s\" (%d) does not exist in this guest-OS version.",
		      desc->name, desc->number);

		err = ENOSYS;
		goto error;
	}

	LCLog(g_nix_us_log, LCLogDebug, 0, "system call \"%s\" (%d) is being executed.", desc->name, desc->number);
	result.type = NIX_PARAM_INVALID;
	result.value.tnosign.u64 = 0;

	nparams = desc->nparams;
	if (desc->flags & NIX_US_SYSCALL_VARIADIC)
		nparams = nix_us_syscall_get_max_params(xus);

	if (nparams == 0)
		params = NULL;
	else {
		params = nix_alloc_ntype(nix_param_t, nparams, 0);
		if (params == NULL) {
			err = ENOMEM;
			goto error;
		}
	}

	for (fmt = desc->format, n = 0; fmt != NULL && *fmt != 0; fmt++) {
		bool             ellipsis = false;
		nix_param_type_t type = NIX_PARAM_INVALID;

		switch (*fmt) {
		case '*':
			ellipsis = true;
			break;
		case 'b':
			type = NIX_PARAM_BYTE;
			break;
		case 'h':
			type = NIX_PARAM_HALF;
			break;
		case 'w':
			type = NIX_PARAM_WORD;
			break;
		case 'd':
			type = NIX_PARAM_DWORD;
			break;
		case 'S':
			type = NIX_PARAM_SINGLE;
			break;
		case 'D':
			type = NIX_PARAM_DOUBLE;
			break;
		case 'X':
			type = NIX_PARAM_EXTENDED;
			break;
		case 'p':
			type = NIX_PARAM_POINTER;
			break;
		case 'v':
			type = NIX_PARAM_VECTOR;
			break;
		case 'l':
			type = NIX_PARAM_INTPTR;
			break;
		default:
			LCAssert(g_nix_us_log, 0);
			break;
		}

		if (ellipsis)
			break;

		err = nix_us_syscall_get_next_param(xus, xmon, desc->flags, type, params + n);
		if (err != 0)
			goto error;

		n++;
	}

	if (desc->flags & NIX_US_SYSCALL_VARIADIC) {
		for (; n < nparams; n++) {
			err = nix_us_syscall_get_next_param(xus, xmon, desc->flags, NIX_PARAM_WORD, params + n);
			if (err != 0)
				goto error;
		}
	}

	if (desc->rettype != NULL) {
		switch (*desc->rettype) {
		case 'b':
			result.type = NIX_PARAM_BYTE;
			break;
		case 'h':
			result.type = NIX_PARAM_HALF;
			break;
		case 'w':
			result.type = NIX_PARAM_WORD;
			break;
		case 'd':
			result.type = NIX_PARAM_DWORD;
			break;
		case 'S':
			result.type = NIX_PARAM_SINGLE;
			break;
		case 'D':
			result.type = NIX_PARAM_DOUBLE;
			break;
		case 'X':
			result.type = NIX_PARAM_EXTENDED;
			break;
		case 'p':
			result.type = NIX_PARAM_POINTER;
			break;
		case 'v':
			result.type = NIX_PARAM_VECTOR;
			break;
		case 'l':
			result.type = NIX_PARAM_INTPTR;
			break;
		default:
			LCBugCheck(g_nix_us_log, 9991);
			break;
		}
	}

	nix_us_syscall_set_retype(xus, NIX_PARAM_INVALID);

	err = (*(desc->callback))(xus, xmon, params, &result);

	if (params != NULL)
		nix_free(params);

	if (err == ENOSYS) {
		LCLog(g_nix_us_log, LCLogFatal, 0, "system call \"%s\" (%d) is not implemented.", desc->name, desc->number);
	}

error:
	if (nix_us_syscall_get_retype(xus) != NIX_PARAM_INVALID)
		result.type = nix_us_syscall_get_retype(xus);

	nix_us_syscall_set_result(xus, xmon, err, &result);
}

int
nix_us_syscall_redispatch(nix_us_syscall_if_t *xus,
                          nix_monitor_t       *xmon,
                          int                  scno,
                          nix_param_t const   *params,
                          void                *result)
{
	nix_us_syscall_desc_t const *desc;
	nix_param_t                 *newparams;
	char const                  *fmt;
	size_t                       n, m, nnewparams;
	nix_param_t                  retval;
	int                          err;

	/* Find the syscall */
	if (!nix_us_syscall_find(xus, scno, &desc) || desc->callback == NULL) {
		return ENOSYS;
	}

	if (!nix_us_syscall_desc_available(desc, nix_monitor_get_target_version(xmon))) {
		return ENOSYS;
	}

	nnewparams = desc->nparams;
	if (desc->flags & NIX_US_SYSCALL_VARIADIC)
		nnewparams = nix_us_syscall_get_max_params(xus);

	if (nnewparams == 0)
		newparams = NULL;
	else {
		newparams = nix_alloc_ntype(nix_param_t, nnewparams, 0);
		if (newparams == NULL) {
			return ENOMEM;
		}
	}

	for (fmt = desc->format, m = n = 0; fmt != NULL && *fmt != 0; fmt++) {
		bool             ellipsis = false;
		nix_param_type_t type = NIX_PARAM_INVALID;

		switch (*fmt) {
		case '*':
			ellipsis = true;
			break;
		case 'b':
			type = NIX_PARAM_BYTE;
			break;
		case 'h':
			type = NIX_PARAM_HALF;
			break;
		case 'w':
			type = NIX_PARAM_WORD;
			break;
		case 'd':
			type = NIX_PARAM_DWORD;
			break;
		case 'S':
			type = NIX_PARAM_SINGLE;
			break;
		case 'D':
			type = NIX_PARAM_DOUBLE;
			break;
		case 'X':
			type = NIX_PARAM_EXTENDED;
			break;
		case 'p':
			type = NIX_PARAM_POINTER;
			break;
		case 'v':
			type = NIX_PARAM_VECTOR;
			break;
		case 'l':
			type = NIX_PARAM_INTPTR;
			break;
		default:
			LCAssert(g_nix_us_log, 0);
			break;
		}

		if (ellipsis)
			break;

		LCAssert(g_nix_us_log, params[n].type == NIX_PARAM_WORD);

		newparams[m].type = type;
		if (type == NIX_PARAM_DWORD) {
			/* XXX THIS IS GUEST DEPENDANT! */
			newparams[m].value.tnosign.u64 = ((uint64_t)params[n].value.tnosign.u32 << 32) | (uint64_t)params[n + 1].value.tnosign.u32;
			m++, n += 2;
		} else {
			newparams[m++] = params[n++];
		}
	}

	err = (*(desc->callback))(xus, xmon, newparams, &retval);

	nix_us_syscall_set_retype(xus, retval.type);

	if (err == 0)
		*(uint64_t *)result = retval.value.tnosign.u64;

	if (newparams != NULL)
		nix_free(newparams);

	return err;
}
