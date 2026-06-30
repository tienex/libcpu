#ifndef __obsd41_guest_h
#define __obsd41_guest_h

#include "nix-syscall.h"
#include "obsd41-us-syscall-priv.h"

void
obsd41_guest_get_syscall (obsd41_us_syscall_t *self,
                          nix_monitor_t *xmon,
                          int *scno);

int
obsd41_guest_get_next_param (void                *self,
                             nix_monitor_t       *xmon,
                             unsigned             flags,
                             nix_param_type_t     type,
                             nix_param_t         *param);

void
obsd41_guest_set_result (void                *self,
                         nix_monitor_t       *xmon,
                         int                  error,
                         nix_param_t const   *result);

#endif  /* !__obsd41_guest_h */
