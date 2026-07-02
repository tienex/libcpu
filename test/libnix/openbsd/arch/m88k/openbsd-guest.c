#include "openbsd-guest.h"
#include "m88k-context.h"
#include "LibCPU/LcLog.h"
#include "nix-host.h"
#include "nix-byte-order.h"

extern void *g_bsd_log;

/*
 * OpenBSD/m88k system call layout:
 *
 *   Trap: 128
 *
 *   r13 - System Call Number
 *
 *  From sys/arch/m88k/m88k/trap.c:
 *
 *   For 88k, all the arguments are passed in the registers (r2-r12)
 *   For syscall (and __syscall), r2 (and r3) has the actual code.
 *   __syscall  takes a quad syscall number, so that other
 *   arguments are at their natural alignments.
 *
 *  Succesful system calls increment by 1 instruction the
 *  program counter!
 */

void
openbsd_guest_get_syscall(openbsd_us_syscall_t *self,
                         nix_monitor_t       *xmon,
                         int                 *scno)
{
	m88k_context_t *ctx = nix_monitor_get_context(xmon);
	*scno = ctx->gpr[13];
}

int
openbsd_guest_get_next_param(void            *_self,
                            nix_monitor_t   *xmon,
                            unsigned         flags,
                            nix_param_type_t type,
                            nix_param_t     *param)
{
	openbsd_us_syscall_t *self = (openbsd_us_syscall_t *)_self;
	nix_mem_if_t        *mem = nix_monitor_get_memory(xmon);
	m88k_context_t      *ctx = nix_monitor_get_context(xmon);

	param->type = type;
	if (self->last_param < (10 - 2)) {
		switch (type) {
		case NIX_PARAM_BYTE:
			param->value.tnosign.u8 = ctx->gpr[2 + self->last_param];
			self->last_param++;
			break;

		case NIX_PARAM_HALF:
			param->value.tnosign.u16 = ctx->gpr[2 + self->last_param];
			self->last_param++;
			break;

		case NIX_PARAM_POINTER:
		case NIX_PARAM_WORD:
		case NIX_PARAM_INTPTR:
			param->value.tnosign.u32 = ctx->gpr[2 + self->last_param];
			self->last_param++;
			break;

		case NIX_PARAM_DWORD:
		case NIX_PARAM_DOUBLE:
			if (self->last_param + 2 < (10 - 2)) {
				param->value.tnosign.u64 = ctx->gpr[2 + self->last_param];
				param->value.tnosign.u64 <<= 32;
				self->last_param++;
			}
			if (self->last_param + 2 < (10 - 2)) {
				param->value.tnosign.u64 |= ctx->gpr[2 + self->last_param];
				self->last_param++;
			}
			break;

		default:
			LCBugCheck(g_bsd_log, 5010);
			return (-1);
		}
	} else {
		__nix_try
		{
			nix_memflg_t    mf = 0;
			m88k_uintptr_t *sp = (m88k_uintptr_t *)
			    nix_mem_gtoh(mem, ctx->gpr[31], &mf);

			LCAssert(g_bsd_log, mf == 0);
			sp += (self->last_param - 8);

			switch (type) {
			case NIX_PARAM_BYTE:
				param->value.tnosign.u8 = nix_byte_swap_big_to_host32(*sp);
				sp++, self->last_param++;
				break;

			case NIX_PARAM_HALF:
				param->value.tnosign.u16 = nix_byte_swap_big_to_host32(*sp);
				sp++, self->last_param++;
				break;

			case NIX_PARAM_POINTER:
			case NIX_PARAM_WORD:
			case NIX_PARAM_INTPTR:
				param->value.tnosign.u32 = nix_byte_swap_big_to_host32(*sp);
				sp++, self->last_param++;
				self->last_param++;
				break;

			case NIX_PARAM_DWORD:
			case NIX_PARAM_DOUBLE:
				param->value.tnosign.u64 = nix_byte_swap_big_to_host32(*sp);
				param->value.tnosign.u64 <<= 32;
				sp++, self->last_param++;
				param->value.tnosign.u64 |= ctx->gpr[2 + self->last_param];
				sp++, self->last_param++;
				break;

			default:
				LCBugCheck(g_bsd_log, 5011);
				return (-1);
			}
		}
		__nix_catch_any
		{
			LCBugCheck(g_bsd_log, 5020);
			return (-1);
		}
		__nix_end_try
	}

	return (0);
}

void
openbsd_guest_set_result(void              *self,
                        nix_monitor_t     *xmon,
                        int                error,
                        nix_param_t const *result)
{
	m88k_context_t *ctx = nix_monitor_get_context(xmon);

	/*
	 * The OpenBSD/m88k kernel signals a failed system call by SETTING the
	 * PSR carry (C) bit, with errno in r2; on success it clears the carry.
	 * libc's cerror stub branches on that carry.
	 */
	if (error != 0) {
		ctx->gpr[2] = error;
		ctx->psr |= (uint32_t)1 << 28;
		ctx->sxip += 4;
	} else {
		uint32_t hi, lo;
		hi = lo = 0;

		ctx->psr &= ~((uint32_t)1 << 28);

		if (result != NULL) {
			switch (result->type) {
			case NIX_PARAM_BYTE:
				lo = result->value.tnosign.u64 & 0xff;
				break;
			case NIX_PARAM_HALF:
				lo = result->value.tnosign.u64 & 0xffff;
				break;
			case NIX_PARAM_DWORD:
				lo = result->value.tnosign.u64 >> 32;
				hi = result->value.tnosign.u64 & 0xffffffff;
				break;
			case NIX_PARAM_POINTER:
			case NIX_PARAM_INTPTR:
			case NIX_PARAM_WORD:
				lo = result->value.tnosign.u64 & 0xffffffff;
				break;
			case NIX_PARAM_INVALID:
				/* a void-returning system call (e.g. sync(2)): no
				 * return value, r2 stays 0 */
				break;
			default:
				LCBugCheck(g_bsd_log, 5012);
			}
		}

		ctx->gpr[2] = lo;
		ctx->gpr[3] = hi;
		ctx->sxip += 4;
	}
}
