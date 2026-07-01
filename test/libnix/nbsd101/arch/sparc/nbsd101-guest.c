#include "nbsd101-guest.h"
#include "sparc-context.h"
#include "LibCPU/LcLog.h"
#include "nix-host.h"
#include "nix-byte-order.h"

extern void *g_bsd_log;

/*
 * NetBSD/sparc system call layout:
 *
 *   %g2 - System Call Number
 *
 * From sys/arch/sparc/sparc/trap.c:
 *
 *   The first six system call arguments are in the six %o registers.
 *   Any arguments beyond that are in the `argument extension' area
 *   of the user's stack frame (see <machine/frame.h>).
 *
 *   Check for ``special'' codes that alter this, namely syscall and
 *   __syscall.  The latter takes a quad syscall number, so that other
 *   arguments are at their natural alignments. [omissis]
 *
 */

#if 0
void
nbsd101_guest_get_syscall (nbsd101_us_syscall_t *self,
                          nix_monitor_t *xmon,
                          int *scno)
{
  sparc_context_t *ctx = nix_monitor_get_context (xmon); 
  *scno = ctx->gregs[1];
}

int
nbsd101_guest_get_next_param (void                *_self,
                             nix_monitor_t       *xmon,
                             unsigned             flags,
                             nix_param_type_t     type,
                             nix_param_t         *param)
{
  nbsd101_us_syscall_t *self  = (nbsd101_us_syscall_t *)_self;
  nix_mem_if_t        *mem   = nix_monitor_get_memory (xmon);
  sparc_context_t     *ctx   = nix_monitor_get_context (xmon); 
  sparc_uintptr_t     *wregs = sparc_context_get_window (ctx);

  param->type = type;
  if (self->last_param < (10 - 2))
    {
      switch (type)
        {
        case NIX_PARAM_BYTE:
          param->value.tnosign.u8 = wregs[self->last_param];
          self->last_param++;
          break;

        case NIX_PARAM_HALF:
          param->value.tnosign.u16 = wregs[self->last_param];
          self->last_param++;
          break;

        case NIX_PARAM_POINTER:
        case NIX_PARAM_WORD:
        case NIX_PARAM_INTPTR:
          param->value.tnosign.u32 = wregs[self->last_param];
          self->last_param++;
          break;

        case NIX_PARAM_DWORD:
        case NIX_PARAM_DOUBLE:
          if (self->last_param + 2 < (10 - 2))
            {
              param->value.tnosign.u64 = wregs[self->last_param];
              param->value.tnosign.u64 <<= 32;
              self->last_param++;
            }
          if (self->last_param + 2 < (10 - 2))
            {
              param->value.tnosign.u64 |= wregs[self->last_param];
              self->last_param++;
            }                        
          break;

        default:
          LCBugCheck (g_bsd_log, 5010);
          return -1;
        }
    }
  else
    {
      __nix_try
        {
          nix_memflg_t   mf = 0;
          sparc_uintptr_t *sp = (sparc_uintptr_t *)nix_mem_gtoh (mem, ctx->regs[31], &mf);

          LCAssert (g_bsd_log, mf == 0);
          sp += (self->last_param - 8);

          switch (type)
            {
            case NIX_PARAM_BYTE:
              param->value.tnosign.u8 = nix_byte_swap_big_to_host32 (*sp);
              sp++, self->last_param++;
              break;

            case NIX_PARAM_HALF:
              param->value.tnosign.u16 = nix_byte_swap_big_to_host32 (*sp);
              sp++, self->last_param++;
              break;

            case NIX_PARAM_POINTER:
            case NIX_PARAM_WORD:
            case NIX_PARAM_INTPTR:
              param->value.tnosign.u32 = nix_byte_swap_big_to_host32 (*sp);
              sp++, self->last_param++;
              self->last_param++;
              break;

            case NIX_PARAM_DWORD:
            case NIX_PARAM_DOUBLE:
              param->value.tnosign.u64 = nix_byte_swap_big_to_host32 (*sp);
              param->value.tnosign.u64 <<= 32;
              sp++, self->last_param++;
              param->value.tnosign.u64 |= wregs[self->last_param];
              sp++, self->last_param++;
              break;

            default:
              LCBugCheck (g_bsd_log, 5011);
              return -1;
              }
        }
      __nix_catch_any
        {
          LCBugCheck (g_bsd_log, 5020);
          return -1;
        }
      __nix_end_try
    }

  return 0;
}

void
nbsd101_guest_set_result (void                *self,
                         nix_monitor_t       *xmon,
                         int                  error,
                         nix_param_t const   *result)
{
  sparc_context_t *ctx = nix_monitor_get_context (xmon); 

  if (error != 0)
    {
      ctx->psr |= (1 << 20); /* Set Carry */
      ctx->regs[2] = error;
    }
  else
    {
      uint32_t hi, lo;

      ctx->psr &= ~(1 << 20); /* Clear Carry */
      
      hi = lo = 0;

      if (result != NULL)
        {
          switch (result->type)
            {
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
            default:
              LCBugCheck (g_bsd_log, 5012);
            }
        }

      wregs[2] = lo;
      wregs[3] = hi;
    }
}
#endif
